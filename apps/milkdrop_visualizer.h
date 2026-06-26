/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Milkdrop-style music visualizer (projectM), shown from the Now Playing
 * screen. See apps/milkdrop_visualizer.c.
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
#ifndef MILKDROP_VISUALIZER_H
#define MILKDROP_VISUALIZER_H

#include <stdbool.h>

/* Run the fullscreen Milkdrop visualizer (auto-cycling presets) until the user
 * presses Back (B). Blocks the calling (UI) thread; audio keeps playing. */
void milkdrop_visualizer_run(void);

/* Fade the current screen to black (visualizer entry), then arm a fade-in so the
 * loaded visualizer fades up from black -- hides the load.  Returns true if a
 * keypress cancelled the fade (caller should NOT launch the visualizer). */
bool milkdrop_visualizer_fade_to_black(void);

/* Settings -> Visualizers: a flat on/off toggle list of the shipped presets.
 * (Reimplemented with stock Rockbox UI in Task 6.) */
int  milkdrop_visualizer_menu(void);

#endif /* MILKDROP_VISUALIZER_H */
