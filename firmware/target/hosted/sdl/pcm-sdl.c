/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2005 by Nick Lanham
 * Copyright (C) 2010 by Thomas Martitz
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

#include "autoconf.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <SDL.h>
#include "config.h"
#include "debug.h"
#include "sound.h"
#include "audiohw.h"
#include "system.h"
#include "panic.h"

#ifdef HAVE_OUTPUT_BIT_DEPTH
#include "settings.h"
#endif

#ifdef HAVE_RECORDING
#include "audiohw.h"
#ifdef HAVE_SPDIF_IN
#include "spdif.h"
#endif
#endif

#include "pcm.h"
#include "pcm-internal.h"
#include "pcm_sampr.h"
#include "pcm_mixer.h"
#include "pcm_sink.h"

/*#define LOGF_ENABLE*/
#include "logf.h"

#if defined(HAVE_SDL_AUDIO) || defined(SIMULATOR)
#include "rtkit.h"
#endif

#ifdef DEBUG
extern bool debug_audio;
#endif

extern const char      *audiodev;


static int cvt_status = -1;

static unsigned long pcm_sampr;

#ifdef HAVE_OUTPUT_BIT_DEPTH
/* Remember the last freq index passed to sink_set_freq_nolock so the setting
 * callback can reopen the device with the same rate but a new wire format. */
static uint16_t sdl_cur_freq_index;
#endif

static const void *pcm_data;
static size_t pcm_data_size;
static size_t pcm_sample_bytes;
static size_t pcm_channel_bytes;
#if SDL_MAJOR_VERSION > 1
static SDL_AudioDeviceID pcm_devid = 0;
#endif

static struct pcm_udata
{
    Uint8 *stream;
    Uint32 num_in;
    Uint32 num_out;
#ifdef DEBUG
    FILE  *debug;
#endif
} udata;

static SDL_AudioSpec obtained;
static SDL_AudioCVT cvt;
static int audio_locked = 0;
static SDL_mutex *audio_lock;

static void sink_lock(void)
{
    if (++audio_locked == 1)
        SDL_LockMutex(audio_lock);
}

static void sink_unlock(void)
{
    if (--audio_locked == 0)
        SDL_UnlockMutex(audio_lock);
}

#ifndef SDL_AUDIO_ALLOW_SAMPLES_CHANGE
#define SDL_AUDIO_ALLOW_SAMPLES_CHANGE 0
#endif

static void sdl_audio_callback(void *handle, Uint8 *stream, int len);

static void sink_set_freq_nolock(uint16_t freq)
{
    pcm_sampr = hw_freq_sampr[freq];

    SDL_AudioSpec wanted_spec;
    wanted_spec.freq = pcm_sampr;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = 2;

#ifdef HAVE_OUTPUT_BIT_DEPTH
    sdl_cur_freq_index = freq;
    /* Output-bit-depth override selects only the SDL<->device wire format;
     * Rockbox core audio is 16-bit. SDL2 has no 24-bit sample type, so both
     * 24 and 32 request S32 (the ALSA layer below picks the real packing).
     * Auto and 16 keep S16 so SDL converts to the device internally, which
     * preserves the S16 fast path and the visualizer PCM tap below. */
    if (global_settings.output_bit_depth == 2 ||   /* 24 */
        global_settings.output_bit_depth == 3)      /* 32 */
        wanted_spec.format = AUDIO_S32SYS;
#endif

    wanted_spec.samples = MIX_FRAME_SAMPLES * 2;  /* Should be 2048, ie ~5ms @44KHz */
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = &udata;

#if SDL_MAJOR_VERSION > 1
    if (pcm_devid)
        SDL_CloseAudioDevice(pcm_devid);

    /* pulseaudio seems to be happier with smaller buffers */
    if (!strcmp("pulseaudio", SDL_GetCurrentAudioDriver()))
        wanted_spec.samples = MIX_FRAME_SAMPLES;
#endif

    /* Open the audio device and start playing sound! */
#if SDL_MAJOR_VERSION > 1
    /* ALLOW_FREQUENCY_CHANGE: obtained.freq reflects the device's actual
     * supported rate instead of being forced to the requested rate. The cvt
     * path below (SDL_BuildAudioCVT + write_to_soundcard) resamples our stream
     * to obtained.freq, so the device is never driven at an unsupported rate.
     * Renegotiated on every reopen -> hotplug-safe. */
    if((pcm_devid = SDL_OpenAudioDevice(audiodev, 0, &wanted_spec, &obtained,
                        SDL_AUDIO_ALLOW_SAMPLES_CHANGE | SDL_AUDIO_ALLOW_FREQUENCY_CHANGE)) == 0) {
#else
    if(SDL_OpenAudio(&wanted_spec, &obtained) < 0) {
#endif
        panicf("Unable to open audio: %s", SDL_GetError());
        return;
    }

    switch (obtained.format)
    {
    case AUDIO_U8:
    case AUDIO_S8:
        pcm_channel_bytes = 1;
        break;
    case AUDIO_U16LSB:
    case AUDIO_S16LSB:
    case AUDIO_U16MSB:
    case AUDIO_S16MSB:
        pcm_channel_bytes = 2;
        break;
#if SDL_MAJOR_VERSION > 1 /* Not supported by SDL 1.2 */
    case AUDIO_S32MSB:
    case AUDIO_S32LSB:
    case AUDIO_F32MSB:
    case AUDIO_F32LSB:
        pcm_channel_bytes = 4;
        break;
#endif
    default:
        panicf("Unknown sample format obtained: %u",
                (unsigned)obtained.format);
        return;
    }
    pcm_sample_bytes = obtained.channels * pcm_channel_bytes;

    cvt_status = SDL_BuildAudioCVT(&cvt, AUDIO_S16SYS, 2, pcm_sampr,
                    obtained.format, obtained.channels, obtained.freq);

    if (cvt_status < 0) {
        cvt.len_ratio = (double)obtained.freq / (double)pcm_sampr;
    }
}

static void sink_set_freq(uint16_t freq)
{
    sink_lock();
    sink_set_freq_nolock(freq);
    sink_unlock();
}

#ifdef HAVE_OUTPUT_BIT_DEPTH
/* Called from the Output-bit-depth setting callback (apps/settings_list.c).
 * Reopens the device with the current rate and the newly-selected wire format.
 * Uses the same lock+reopen pattern as sink_set_freq, which is already
 * exercised during normal rate changes. */
void pcm_sdl_reopen_device(void)
{
    /* Nothing to reopen until the device has been opened at least once; the
     * new wire format is applied by the first sink_set_freq_nolock() then. */
    if (!pcm_devid)
        return;

    /* Quiesce the SDL callback BEFORE taking audio_lock and closing the
     * device, so we can't block inside SDL_CloseAudioDevice waiting for a
     * callback that is itself blocked on audio_lock. */
    bool was_playing = pcm_is_playing();
    SDL_PauseAudioDevice(pcm_devid, 1);

    sink_lock();
    sink_set_freq_nolock(sdl_cur_freq_index);
    sink_unlock();

    /* SDL opens devices paused; the normal path relies on sink_dma_start to
     * unpause, which we bypass here -- so resume if we were playing. */
    if (was_playing)
        SDL_PauseAudioDevice(pcm_devid, 0);
}
#endif

static void sink_dma_start(const void *addr, size_t size)
{
    pcm_data = addr;
    pcm_data_size = size;

#if SDL_MAJOR_VERSION > 1
    SDL_PauseAudioDevice(pcm_devid, 0);
#else
    SDL_PauseAudio(0);
#endif
}

static void sink_dma_stop(void)
{
#if SDL_MAJOR_VERSION > 1
    SDL_PauseAudioDevice(pcm_devid, 1);
#else
    SDL_PauseAudio(1);
#endif

#ifdef DEBUG
    if (udata.debug != NULL) {
        fclose(udata.debug);
        udata.debug = NULL;
        DEBUGF("Audio debug file closed\n");
    }
#endif
}

static void write_to_soundcard(struct pcm_udata *udata)
{
#ifdef DEBUG
    if (debug_audio && (udata->debug == NULL)) {
        udata->debug = fopen("audiodebug.raw", "abe");
        DEBUGF("Audio debug file open\n");
    }
#endif
    if (cvt.needed) {
        Uint32 rd = udata->num_in;
        Uint32 wr = (double)rd * cvt.len_ratio;

        if (wr > udata->num_out) {
            wr = udata->num_out;
            rd = (double)wr / cvt.len_ratio;

            if (rd > udata->num_in)
            {
                rd = udata->num_in;
                wr = (double)rd * cvt.len_ratio;
            }
        }

        if (wr == 0 || rd == 0)
        {
            udata->num_out = udata->num_in = 0;
            return;
        }

        if (cvt_status > 0) {
            cvt.len = rd * pcm_sample_bytes;
            cvt.buf = (Uint8 *) malloc(cvt.len * cvt.len_mult);

            pcm_copy_buffer(cvt.buf, pcm_data, cvt.len);

            SDL_ConvertAudio(&cvt);
            memcpy(udata->stream, cvt.buf, cvt.len_cvt);

            udata->num_in = cvt.len / pcm_sample_bytes;
            udata->num_out = cvt.len_cvt / pcm_sample_bytes;

#ifdef DEBUG
            if (udata->debug != NULL) {
               fwrite(cvt.buf, sizeof(Uint8), cvt.len_cvt, udata->debug);
            }
#endif
            free(cvt.buf);
        } else {
            /* Convert is bad, so do silence */
            Uint32 num = wr*obtained.channels;
            udata->num_in = rd;
            udata->num_out = wr;

            switch (pcm_channel_bytes)
            {
            case 1:
            {
                Uint8 *stream = udata->stream;
                while (num-- > 0)
                    *stream++ = obtained.silence;
                break;
                }
            case 2:
            {
                Uint16 *stream = (Uint16 *)udata->stream;
                while (num-- > 0)
                    *stream++ = obtained.silence;
                break;
                }
            }
#ifdef DEBUG
            if (udata->debug != NULL) {
               fwrite(udata->stream, sizeof(Uint8), wr, udata->debug);
            }
#endif
        }
    } else {
        udata->num_in = udata->num_out = MIN(udata->num_in, udata->num_out);
        pcm_copy_buffer(udata->stream, pcm_data,
                        udata->num_out * pcm_sample_bytes);
#ifdef DEBUG
        if (udata->debug != NULL) {
           fwrite(pcm_data, sizeof(Uint8), udata->num_out * pcm_sample_bytes,
                  udata->debug);
        }
#endif
    }
}

/* --- Visualizer PCM tap -------------------------------------------------
 * The Milkdrop/projectM visualizer needs the audio waveform to react to the
 * beat.  We mirror the PRE-volume Rockbox S16 stereo source into a lock-free
 * ring buffer (single producer = audio callback, single consumer = render
 * thread) so the visuals are independent of the volume setting.
 * Torn reads are harmless for a visualizer. */
#define VIZ_PCM_FRAMES 8192            /* must be a power of two */
static int16_t viz_pcm_ring[VIZ_PCM_FRAMES * 2];
static volatile unsigned viz_pcm_w;    /* total stereo frames ever written */

static void viz_pcm_push(const int16_t *src, unsigned frames)
{
    unsigned w = viz_pcm_w;
    for (unsigned i = 0; i < frames; i++)
    {
        unsigned idx = (w & (VIZ_PCM_FRAMES - 1)) * 2;
        viz_pcm_ring[idx]     = src[2 * i];
        viz_pcm_ring[idx + 1] = src[2 * i + 1];
        w++;
    }
    viz_pcm_w = w;
}

/* Copy the most recent up-to max_frames stereo frames into out (LRLR).
 * Returns the number of frames written. Called by the visualizer plugin. */
unsigned pcm_sdl_viz_latest(int16_t *out, unsigned max_frames)
{
    unsigned w = viz_pcm_w;
    unsigned n = max_frames < VIZ_PCM_FRAMES ? max_frames : VIZ_PCM_FRAMES;
    if (n > w)
        n = w;
    unsigned start = w - n;
    for (unsigned i = 0; i < n; i++)
    {
        unsigned idx = ((start + i) & (VIZ_PCM_FRAMES - 1)) * 2;
        out[2 * i]     = viz_pcm_ring[idx];
        out[2 * i + 1] = viz_pcm_ring[idx + 1];
    }
    return n;
}

static void sdl_audio_callback(void *handle, Uint8 *stream, int len)
{
    struct pcm_udata *udata = handle;

    logf("sdl_audio_callback: len %d, pcm %zd", len, pcm_data_size);

#if defined(HAVE_SDL_AUDIO) || defined(SIMULATOR)
    /* SDL owns this thread; elevate it the first time it runs. Harmless
     * if it fails -- see rtkit.h. */
    static bool rt_tried = false;
    if (!rt_tried)
    {
        rt_tried = true;
        rtkit_make_thread_realtime(RTKIT_AUDIO_PRIORITY);
    }
#endif

    bool new_buffer = false;
    udata->stream = stream;

    SDL_LockMutex(audio_lock);

    /* Write what we have in the PCM buffer */
    if (pcm_data_size > 0)
        goto start;

    /* Audio card wants more? Get some more then. */
    while (len > 0) {
        new_buffer = pcm_play_dma_complete_callback(PCM_DMAST_OK, &pcm_data,
                                                    &pcm_data_size);

        if (!new_buffer) {
            DEBUGF("sdl_audio_callback: No Data.\n");
            break;
        }
        logf("audio_callback_cont: len %d, pcm %zd", len, pcm_data_size);

    start:
        udata->num_in  = pcm_data_size / pcm_sample_bytes;
        udata->num_out = len / pcm_sample_bytes;

        write_to_soundcard(udata);

        /* Mirror the PRE-volume source to the visualizer ring so the spectrum
         * and visualizer react to the music independently of the volume setting.
         * udata->num_in is still in frames here; the guard ensures S16 stereo. */
        if (pcm_channel_bytes == 2 && obtained.channels == 2)
            viz_pcm_push((const int16_t *)pcm_data, udata->num_in);

        udata->num_in  *= pcm_sample_bytes;
        udata->num_out *= pcm_sample_bytes;

        if (new_buffer)
        {
            new_buffer = false;
            pcm_play_dma_status_callback(PCM_DMAST_STARTED);

            if ((size_t)len > udata->num_out)
            {
                int delay = pcm_data_size*250 / pcm_sampr - 1;

                if (delay > 0)
                {
                    SDL_Delay(delay);

                    if (!pcm_is_playing())
                        break;
                }
            }
        }

        pcm_data      += udata->num_in;
        pcm_data_size -= udata->num_in;
        udata->stream += udata->num_out;
        len           -= udata->num_out;
    }

    SDL_UnlockMutex(audio_lock);
}

#ifdef HAVE_RECORDING
void pcm_rec_lock(void)
{
}

void pcm_rec_unlock(void)
{
}

void pcm_rec_dma_init(void)
{
}

void pcm_rec_dma_close(void)
{
}

void pcm_rec_dma_start(void *start, size_t size)
{
    (void)start;
    (void)size;
}

void pcm_rec_dma_stop(void)
{
}

const void * pcm_rec_dma_get_peak_buffer(void)
{
    return NULL;
}

void audiohw_set_recvol(int left, int right, int type)
{
    (void)left;
    (void)right;
    (void)type;
}

#ifdef HAVE_SPDIF_IN
unsigned long spdif_measure_frequency(void)
{
    return 0;
}
#endif

#endif /* HAVE_RECORDING */

static void sink_dma_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        panicf("Could not initialize SDL audio subsystem!");
        return;
    }

#ifdef SIMULATOR
    int cnt = SDL_GetNumAudioDrivers();
    printf("SDL Audio Drivers supported:\n");
    for (int i = 0 ; i < cnt ; i++) {
        printf("   %s %s\n", SDL_GetAudioDriver(i), SDL_GetAudioDriver(i) == SDL_GetCurrentAudioDriver() ? "(active)" : "");
    }
    cnt = SDL_GetNumAudioDevices(0);
    printf("SDL Audio Devices present:\n");
    for (int i = 0 ; i < cnt ; i++) {
            printf("  '%s'\n", SDL_GetAudioDeviceName(i, 0));
    }
#endif

    audio_lock = SDL_CreateMutex();

    if (!audio_lock)
    {
        panicf("Could not create audio_lock");
        return;
    }

#ifdef DEBUG
    udata.debug = NULL;
    if (debug_audio) {
        udata.debug = fopen("audiodebug.raw", "wbe");
        DEBUGF("Audio debug file open\n");
    }
#endif
}

static void sink_dma_postinit(void)
{
}

struct pcm_sink builtin_pcm_sink = {
    .caps = {
        .samprs       = hw_freq_sampr,
        .num_samprs   = HW_NUM_FREQ,
        .default_freq = HW_FREQ_DEFAULT,
    },
    .ops = {
        .init     = sink_dma_init,
        .postinit = sink_dma_postinit,
        .set_freq = sink_set_freq,
        .lock     = sink_lock,
        .unlock   = sink_unlock,
        .play     = sink_dma_start,
        .stop     = sink_dma_stop,
    },
};
