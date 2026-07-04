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
#ifndef RTKIT_H
#define RTKIT_H

#include <stdbool.h>

/* SCHED_RR priority requested by Rockbox's own audio-critical threads
 * (codec, playback engine, buffering, and the SDL audio callback
 * thread). Chosen to sit comfortably above a desktop compositor's own
 * realtime priority (typically 1) and below the audio server's
 * hardware-facing thread (PipeWire's data-loop is 88 by default) --
 * high enough that this process's own audio pipeline can't be starved
 * by its UI/compositor, low enough it never contends with the thread
 * that actually owns the hardware. */
#define RTKIT_AUDIO_PRIORITY 15

/* Ask the OS to grant the calling thread SCHED_RR at the given priority.
 * Tries a direct sched_setscheduler() first (succeeds outright when the
 * process already has CAP_SYS_NICE or an RLIMIT_RTPRIO grant -- e.g. the
 * usual "runs as root" case for hosted kiosk-style targets), then falls
 * back to asking the desktop RealtimeKit D-Bus service (the same
 * mechanism PipeWire/PulseAudio use) for setups where it isn't.
 *
 * Best-effort and always safe to call: returns false (leaving scheduling
 * untouched) if neither route is available/permitted, if no D-Bus system
 * bus is reachable, or on non-Linux hosted platforms. Never blocks longer
 * than a couple of socket round-trips (internally timeout-bounded), never
 * aborts the caller on failure. */
bool rtkit_make_thread_realtime(int priority);

#endif /* RTKIT_H */
