/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Milkdrop/projectM visualizer settings submenu: the preset switch interval
 * and the per-preset on/off list.
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

#include <stddef.h>
#include "config.h"
#include "lang.h"
#include "settings.h"
#include "menu.h"
#include "milkdrop_visualizer.h"

MENUITEM_SETTING(viz_transition_item, &global_settings.viz_transition, NULL);
MENUITEM_SETTING(viz_resolution_item, &global_settings.viz_resolution, NULL);
MENUITEM_FUNCTION(viz_presets_item, 0, ID2P(LANG_VIZ_PRESETS),
                  milkdrop_visualizer_menu, NULL, Icon_Playback_menu);

MAKE_MENU(visualizer_settings_menu, ID2P(LANG_VISUALIZER), NULL,
          Icon_Playback_menu,
          &viz_transition_item, &viz_resolution_item, &viz_presets_item);
