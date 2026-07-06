/*
 * This config file is for Retro Handhelds.
 * 
 * Currently supported CPU/Firmwares:
 *    - H700 devices (28XX, 34XX, 35XX, 40XX, CubeXX, ...)
 *        - MuOS
 *    - a133p devices (TrimUI Brick/Smart Pro, MagicX Zero28, ...)
 *        - MuoS
 *        - NextUI
 */

/* We don't run on hardware directly */
#define CONFIG_PLATFORM (PLATFORM_HOSTED|PLATFORM_SDL)
#define HAVE_FPU

/* For Rolo and boot loader */
#define MODEL_NUMBER 100

#define MODEL_NAME   "Retro Handheld"

#define USB_NONE

/* define this if you have a colour LCD */
#define HAVE_LCD_COLOR

/* define this if you want album art for this target */
#define HAVE_ALBUMART

/* define this to enable bitmap scaling */
#define HAVE_BMP_SCALING

/* define this to enable JPEG decoding */
#define HAVE_JPEG

/* define this if you have access to the quickscreen */
#define HAVE_QUICKSCREEN

/* define this if you would like tagcache to build on this target */
#define HAVE_TAGCACHE

/* LCD dimensions */
#define LCD_WIDTH  320
#define LCD_HEIGHT 240
#define LCD_DEPTH  24
#define LCD_PIXELFORMAT RGB888

/* define this to indicate your device's keypad */
#define HAVE_BUTTON_DATA
#define HAVE_VOLUME_IN_LIST

/* define this if you have a real-time clock */
#define CONFIG_RTC APPLICATION

/* The number of bytes reserved for loadable codecs */
#define CODEC_SIZE 0x400000

/* The number of bytes reserved for loadable plugins */
#define PLUGIN_BUFFER_SIZE 0x800000

#define AB_REPEAT_ENABLE

/* Battery stuff */
#define CONFIG_BATTERY_MEASURE PERCENTAGE_MEASURE
#define CONFIG_CHARGING CHARGING_MONITOR
#define HAVE_POWEROFF_WHILE_CHARGING

/* Define this for LCD backlight available */
#define HAVE_BACKLIGHT
#define HAVE_BACKLIGHT_BRIGHTNESS
#define CONFIG_BACKLIGHT_FADING BACKLIGHT_FADING_SW_SETTING

/* Main LCD backlight brightness range and defaults, steps of 15... revisit this for other chipsets */
#define MIN_BRIGHTNESS_SETTING      1
#define MAX_BRIGHTNESS_SETTING      17
#define DEFAULT_BRIGHTNESS_SETTING  6

#define CONFIG_KEYPAD RETRO_HANDHELD_PAD

/* Use SDL audio/pcm in a SDL app build */
#define HAVE_SDL
#define HAVE_SDL_AUDIO

/* Advertise the full up-to-192kHz rate set so the core can output 48kHz and
 * higher; this also auto-enables HAVE_PLAY_FREQ (the Frequency menu). */
#define HW_SAMPR_CAPS       SAMPR_CAP_ALL_192

/* Prefer 48000 Hz out of the box (many USB-C DACs reject 44100). Users can
 * still choose Auto/44.1/96/192 in the Frequency menu. Expanded to SAMPR_48
 * at setting-definition time (pcm_sampr.h is included by then). */
#define PLAY_FREQ_DEFAULT   SAMPR_48

/* Expose the Output-bit-depth override (SDL<->device wire format only). */
#define HAVE_OUTPUT_BIT_DEPTH

#define HAVE_SW_TONE_CONTROLS

/* Define this to the CPU frequency */
/*
#define CPU_FREQ 48000000
*/

#define CONFIG_LCD LCD_COWOND2

/* Define this if a programmable hotkey is mapped */
#define HAVE_HOTKEY

#define BOOTDIR "/tmp/rockbox"

/* No special storage */
#define CONFIG_STORAGE STORAGE_HOSTFS
#define HAVE_STORAGE_FLUSH
