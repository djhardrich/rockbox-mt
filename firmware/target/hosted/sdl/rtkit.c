/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include "rtkit.h"

#if defined(__linux__)

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/syscall.h>

#include "debug.h"

/* ---------------------------------------------------------------------
 * Minimal, self-contained D-Bus client: just enough wire protocol to
 * call org.freedesktop.RealtimeKit1.MakeThreadRealtime. No libdbus
 * dependency, so this never adds a build- or link-time requirement to
 * the SDL hosted target -- most hosted/SDL targets don't run D-Bus at
 * all, and this degrades to "connect() fails, return false" there.
 * This is only a fallback anyway (see rtkit_make_thread_realtime): it
 * is also gated behind PolicyKit on the daemon side, which is commonly
 * absent on minimal embedded images, in which case the daemon itself
 * reports failure and we return false, same as if it weren't running.
 * ------------------------------------------------------------------- */

struct buf {
    uint8_t *data;
    size_t len, cap;
};

static void buf_init(struct buf *b) { b->data = NULL; b->len = 0; b->cap = 0; }
static void buf_free(struct buf *b) { free(b->data); b->data = NULL; }

static void buf_reserve(struct buf *b, size_t extra)
{
    if (b->len + extra <= b->cap)
        return;
    size_t ncap = b->cap ? b->cap * 2 : 256;
    while (ncap < b->len + extra)
        ncap *= 2;
    b->data = realloc(b->data, ncap);
    b->cap = ncap;
}

static void buf_bytes(struct buf *b, const void *p, size_t n)
{
    buf_reserve(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void buf_pad(struct buf *b, size_t align)
{
    size_t pad = (align - (b->len % align)) % align;
    if (!pad)
        return;
    buf_reserve(b, pad);
    memset(b->data + b->len, 0, pad);
    b->len += pad;
}

static void buf_u8(struct buf *b, uint8_t v) { buf_bytes(b, &v, 1); }

static void buf_u32(struct buf *b, uint32_t v)
{
    buf_pad(b, 4);
    buf_bytes(b, &v, 4);
}

static void buf_u64(struct buf *b, uint64_t v)
{
    buf_pad(b, 8);
    buf_bytes(b, &v, 8);
}

/* STRING and OBJECT_PATH share the same wire encoding: UINT32 length
 * (bytes, not incl. NUL) + bytes + NUL terminator. Alignment 4. */
static void buf_string(struct buf *b, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    buf_u32(b, n);
    buf_bytes(b, s, n + 1);
}

/* SIGNATURE: BYTE length + bytes + NUL. Alignment 1. */
static void buf_signature(struct buf *b, const char *s)
{
    uint8_t n = (uint8_t)strlen(s);
    buf_u8(b, n);
    buf_bytes(b, s, n + 1);
}

/* A header field is STRUCT(BYTE code, VARIANT value) -- struct alignment
 * is always 8 regardless of contents. VARIANT wire form is: SIGNATURE
 * (of the contained value) followed by the value itself. */
static void buf_header_field_string(struct buf *b, uint8_t code, char type, const char *s)
{
    buf_pad(b, 8);
    buf_u8(b, code);
    char sig[2] = { type, 0 };
    buf_signature(b, sig);
    buf_string(b, s); /* valid for both 's' and 'o' -- identical encoding */
}

static void buf_header_field_signature(struct buf *b, uint8_t code, const char *sig_value)
{
    buf_pad(b, 8);
    buf_u8(b, code);
    buf_signature(b, "g");       /* variant holds a SIGNATURE-typed value */
    buf_signature(b, sig_value); /* the value itself */
}

/* Build a full METHOD_CALL message (fixed header + header fields array,
 * padded to 8, + pre-marshaled body). body_sig/body may be NULL/empty
 * for a no-argument call (e.g. Hello). */
static void build_method_call(struct buf *out, uint32_t serial,
                               const char *path, const char *iface,
                               const char *member, const char *dest,
                               const char *body_sig,
                               const uint8_t *body, size_t body_len)
{
    buf_init(out);
    buf_u8(out, 'l');   /* little-endian */
    buf_u8(out, 1);     /* METHOD_CALL */
    buf_u8(out, 0);     /* flags: none, reply expected */
    buf_u8(out, 1);     /* protocol version */
    buf_u32(out, (uint32_t)body_len);
    buf_u32(out, serial);

    buf_u32(out, 0); /* header fields array length -- patched below */
    size_t arr_len_off = out->len - 4;
    buf_pad(out, 8);
    size_t arr_start = out->len;

    if (path)     buf_header_field_string(out, 1, 'o', path);
    if (iface)    buf_header_field_string(out, 2, 's', iface);
    if (member)   buf_header_field_string(out, 3, 's', member);
    if (dest)     buf_header_field_string(out, 6, 's', dest);
    if (body_sig) buf_header_field_signature(out, 8, body_sig);

    uint32_t arr_len = (uint32_t)(out->len - arr_start);
    memcpy(out->data + arr_len_off, &arr_len, 4);

    buf_pad(out, 8); /* header must end on an 8-byte boundary */

    if (body_len)
        buf_bytes(out, body, body_len);
}

static int write_full(int fd, const void *p, size_t n)
{
    const uint8_t *b = p;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, b + off, n - off);
        if (w <= 0) {
            if (w < 0 && errno == EINTR)
                continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static int read_full(int fd, void *p, size_t n)
{
    uint8_t *b = p;
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, b + off, n - off);
        if (r <= 0) {
            if (r < 0 && errno == EINTR)
                continue;
            return -1;
        }
        off += (size_t)r;
    }
    return 0;
}

static void hex_encode(char *dst, const char *src, size_t n)
{
    static const char hexd[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        dst[2 * i]     = hexd[(unsigned char)src[i] >> 4];
        dst[2 * i + 1] = hexd[(unsigned char)src[i] & 0xf];
    }
    dst[2 * n] = 0;
}

/* Connects, authenticates, and returns a ready-for-messages fd, or -1. */
static int dbus_system_bus_connect(void)
{
    const char *paths[] = {
        "/run/dbus/system_bus_socket",
        "/var/run/dbus/system_bus_socket",
    };

    int fd = -1;
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, paths[i], sizeof(addr.sun_path) - 1);

        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            break;

        close(fd);
        fd = -1;
    }
    if (fd < 0)
        return -1;

    struct timeval tv = { .tv_sec = 0, .tv_usec = 300000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* SASL EXTERNAL handshake: authorize as our own uid. */
    char uid_dec[32];
    snprintf(uid_dec, sizeof(uid_dec), "%lu", (unsigned long)getuid());
    char uid_hex[64];
    hex_encode(uid_hex, uid_dec, strlen(uid_dec));

    /* SASL requires a leading NUL byte before the first command. */
    char cmd[128];
    cmd[0] = '\0';
    int m = snprintf(cmd + 1, sizeof(cmd) - 1, "AUTH EXTERNAL %s\r\n", uid_hex);
    int n = 1 + m;

    if (write_full(fd, cmd, (size_t)n) < 0)
        goto fail;

    char reply[256];
    ssize_t rn = read(fd, reply, sizeof(reply) - 1);
    if (rn <= 0)
        goto fail;
    reply[rn] = 0;
    if (strncmp(reply, "OK ", 3) != 0)
        goto fail;

    static const char begin[] = "BEGIN\r\n";
    if (write_full(fd, begin, strlen(begin)) < 0)
        goto fail;

    return fd;

fail:
    close(fd);
    return -1;
}

/* Reads exactly one full D-Bus message off the wire and discards its
 * contents, returning its message type (2=METHOD_RETURN, 3=ERROR,
 * 4=SIGNAL, ...) or -1 on I/O error. Needed because the bus can and does
 * interleave asynchronous SIGNAL messages (e.g. NameAcquired right after
 * Hello) with the METHOD_RETURN/ERROR we're actually waiting for. */
static int read_and_drain_message(int fd)
{
    uint8_t hdr[16];
    if (read_full(fd, hdr, sizeof(hdr)) < 0)
        return -1;

    if (hdr[0] != 'l') /* only handle little-endian replies */
        return -1;

    uint8_t msg_type = hdr[1];

    uint32_t body_length, field_len;
    memcpy(&body_length, hdr + 4, 4);
    memcpy(&field_len, hdr + 12, 4);

    /* field_len bytes of header fields, then pad to 8 from offset 16
     * (which is itself already a multiple of 8, so this pads field_len
     * up to the next multiple of 8). */
    size_t header_rest = (field_len + 7u) & ~7u;

    size_t drain = header_rest + body_length;
    uint8_t tmp[512];
    while (drain > 0) {
        size_t chunk = drain < sizeof(tmp) ? drain : sizeof(tmp);
        if (read_full(fd, tmp, chunk) < 0)
            break; /* best-effort drain; we already have msg_type */
        drain -= chunk;
    }

    return msg_type;
}

/* Sends a method call and returns true iff the (eventual, possibly after
 * skipping interleaved signals) reply was METHOD_RETURN. */
static bool dbus_call_and_check(int fd, uint32_t serial,
                                 const char *path, const char *iface,
                                 const char *member, const char *dest,
                                 const char *body_sig,
                                 const uint8_t *body, size_t body_len)
{
    struct buf msg;
    build_method_call(&msg, serial, path, iface, member, dest,
                       body_sig, body, body_len);
    int ok = write_full(fd, msg.data, msg.len);
    buf_free(&msg);
    if (ok < 0)
        return false;

    /* Skip any interleaved SIGNALs (or anything else that isn't a reply)
     * until we see METHOD_RETURN/ERROR or run out of patience. */
    for (int i = 0; i < 8; i++) {
        int msg_type = read_and_drain_message(fd);
        if (msg_type < 0)
            return false;
        if (msg_type == 2)
            return true;
        if (msg_type == 3)
            return false;
        /* else: signal or method call we don't care about -- keep reading */
    }
    return false;
}

/* Ask rtkit-daemon over D-Bus (the org.freedesktop.RealtimeKit1 service
 * PipeWire/PulseAudio also use). This is a POLKIT-gated call: it will
 * fail wherever polkit isn't installed, which is common on minimal
 * embedded images. Kept as a fallback for setups where the direct
 * syscall below isn't privileged (see rtkit_make_thread_realtime). */
static bool rtkit_dbus_make_thread_realtime(int priority)
{
    int fd = dbus_system_bus_connect();
    if (fd < 0)
        return false;

    uint32_t serial = 1;
    bool ok = dbus_call_and_check(fd, serial++,
                                   "/org/freedesktop/DBus",
                                   "org.freedesktop.DBus",
                                   "Hello",
                                   "org.freedesktop.DBus",
                                   NULL, NULL, 0);
    if (!ok) {
        close(fd);
        return false;
    }

    uint64_t tid = (uint64_t)syscall(SYS_gettid);

    for (int attempt = 0; attempt < 2; attempt++) {
        int prio = attempt == 0 ? priority : 1;

        struct buf body;
        buf_init(&body);
        buf_u64(&body, tid);
        buf_u32(&body, (uint32_t)prio);

        ok = dbus_call_and_check(fd, serial++,
                                  "/org/freedesktop/RealtimeKit1",
                                  "org.freedesktop.RealtimeKit1",
                                  "MakeThreadRealtime",
                                  "org.freedesktop.RealtimeKit1",
                                  "tu", body.data, body.len);
        buf_free(&body);

        if (ok)
            break;
    }

    close(fd);
    return ok;
}

/* Try to become SCHED_RR directly via the kernel, no D-Bus/rtkit needed.
 * This succeeds outright when the process has CAP_SYS_NICE (e.g. runs
 * as root, as Rockbox does on these kiosk-style images) or an
 * RLIMIT_RTPRIO grant from /etc/security/limits.d -- exactly how
 * PipeWire itself gets its own realtime thread on this same image
 * (rtkit is documented, in PipeWire's own rlimits.conf, as only a
 * fallback for when this direct route isn't privileged). */
static bool direct_make_thread_realtime(int priority)
{
    int min = sched_get_priority_min(SCHED_RR);
    int max = sched_get_priority_max(SCHED_RR);
    if (priority < min)
        priority = min;
    if (priority > max)
        priority = max;

    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = priority;
    return pthread_setschedparam(pthread_self(), SCHED_RR, &sp) == 0;
}

bool rtkit_make_thread_realtime(int priority)
{
    if (direct_make_thread_realtime(priority))
        return true;

    if (rtkit_dbus_make_thread_realtime(priority))
        return true;

    DEBUGF("rtkit_make_thread_realtime(%d): failed, staying SCHED_OTHER\n", priority);
    return false;
}

#else /* !__linux__ */

bool rtkit_make_thread_realtime(int priority)
{
    (void)priority;
    return false;
}

#endif
