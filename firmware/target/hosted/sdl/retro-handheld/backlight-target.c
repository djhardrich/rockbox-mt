/***************************************************************************
 *             __________               __   ___
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2017 Marcin Bukat
 * Copyright (C) 2019 by Roman Stolyarov
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "backlight-target.h"
#include "sysfs.h"
#include "panic.h"
#include "lcd.h"

typedef enum {
  BL_TYPE1 = 0,
  BL_TYPE2 = 1,
} backlight_t;

static char* get_bl_env(const char *env_key)
{
    char *p = getenv(env_key);
    return (p && *p) ? p : NULL;
}

static void set_type1_brightness(int value) {
  sysfs_set_string(get_bl_env("SYSFS_BL_COMMAND"), get_bl_env("BL_COMMAND"));
  sysfs_set_string(get_bl_env("SYSFS_BL_NAME"), get_bl_env("BL_NAME"));
  sysfs_set_int(get_bl_env("SYSFS_BL_PARAM"), value);
  sysfs_set_int(get_bl_env("SYSFS_BL_START"), 1);
}

static void set_type2_brightness(int value) {
  sysfs_set_int(get_bl_env("SYSFS_BL_BRIGHTNESS"), value);
}

static int get_type2_bl_step(void) {
  int max = 0;

  if (!sysfs_get_int(get_bl_env("SYSFS_BL_MAX"), &max)) {
    return 0;
  }

  return max / 17;
}

static backlight_t get_type(void)
{
    char *bl_type = getenv("BL_TYPE");
    if (!bl_type) return BL_TYPE1;

    if (strcmp(bl_type, "TYPE1") == 0) return BL_TYPE1;
    if (strcmp(bl_type, "TYPE2") == 0) return BL_TYPE2;

    return BL_TYPE1;
}

bool backlight_hw_init(void)
{
    backlight_hw_on();
    backlight_hw_brightness(DEFAULT_BRIGHTNESS_SETTING);
#ifdef HAVE_BUTTON_LIGHT
    buttonlight_hw_on();
#ifdef HAVE_BUTTONLIGHT_BRIGHTNESS
    buttonlight_hw_brightness(DEFAULT_BRIGHTNESS_SETTING);
#endif
#endif
    return true;
}

static int last_bl = -1;

void backlight_hw_on(void)
{
    if (last_bl != 1) {
#ifdef HAVE_LCD_ENABLE
        lcd_enable(true);
#endif
    
    switch (get_type()) {
        case BL_TYPE1: set_type1_brightness(15); break;        
        case BL_TYPE2: sysfs_set_int(get_bl_env("SYSFS_BL_POWER"), 0); break;
    }

	  last_bl = 1;
    }
}

void backlight_hw_off(void)
{
    if (last_bl != 0) {

    switch (get_type()) {
        case BL_TYPE1: set_type1_brightness(0); break;        
        case BL_TYPE2: sysfs_set_int(get_bl_env("SYSFS_BL_POWER"), 1); break;
    }

#ifdef HAVE_LCD_ENABLE
        lcd_enable(false);
#endif
	last_bl = 0;
    }
}

void backlight_hw_brightness(int brightness)
{
    /* cap range, just in case */
    if (brightness > MAX_BRIGHTNESS_SETTING)
        brightness = MAX_BRIGHTNESS_SETTING;
    if (brightness < MIN_BRIGHTNESS_SETTING)
        brightness = MIN_BRIGHTNESS_SETTING;

    switch (get_type()) {
          case BL_TYPE1: set_type1_brightness(brightness * 15); break;        
          case BL_TYPE2: set_type2_brightness(brightness * get_type2_bl_step()); break;
    }

}
