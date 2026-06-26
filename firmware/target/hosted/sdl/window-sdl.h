/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
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

#ifndef __WINDOWSDL_H__
#define __WINDOWSDL_H__

#include "SDL.h"
#include <stdbool.h>

extern SDL_Surface *sim_lcd_surface; /* LCD content */

extern SDL_mutex *window_mutex;  /* prevent concurrent drawing from event thread &
                                    main thread on MS Windows */

/* GL bridge: the SDL window is presented through a shared GLES context so the
 * Milkdrop visualizer (projectM) can render into the same window. */
extern volatile bool trimpod_viz_active;  /* visualizer owns the GL context */
void  sdl_gl_make_current(void);          /* re-bind the shared GL context to the UI thread */
void *sdl_gl_get_context(void);           /* the SDL_GLContext, for the render thread */
void  sdl_gl_present_lcd_fade(float fade); /* present current LCD * fade (entry fade) */

/* Renders GUI texture. Sets up new texture, if necessary */
void sdl_window_render(void);

/* Updates size, aspect ratio, and re-renders window content */
bool sdl_window_adjust(void);

/* Needs to be called when window size, scale quality, or background should change */
void sdl_window_adjustment_needed(bool destroy_texture);

/* Creates window, renderer, and LCD surface when app launches */
void sdl_window_setup(void);

#endif /* #ifndef __WINDOWSDL_H__ */
