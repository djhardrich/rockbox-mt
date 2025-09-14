#!/bin/bash
# HELP: Rockbox
# ICON: music
# GRID: Rockbox

. /opt/muos/script/var/func.sh
echo app >/tmp/act_go

GOV_GO="/tmp/gov_go"
[ -e "$GOV_GO" ] && cat "$GOV_GO" >"$(GET_VAR "device" "cpu/governor")"

SETUP_SDL_ENVIRONMENT

# Set paths
RBDIR="$(GET_VAR "device" "storage/rom/mount")/MUOS/application/Rockbox"
RBDIR_BIND="/tmp/rockbox"
GPTOKEYB2="/opt/muos/share/emulator/gptokeyb/gptokeyb2"
# Get what the current device is.
DEVICE_ID="$(GET_VAR "device" "board/name")"

# Store current idle input/display settings.
IDLEDISP="$(GET_VAR "config" "settings/power/idle_display")"
IDLEMUTE="$(GET_VAR "config" "settings/power/idle_mute")"
IDLESLEEP="$(GET_VAR "config" "settings/power/idle_sleep")"
if [[ "$DEVICE_ID" == "rg35xx-sp" || "$DEVICE_ID" == "rg34xx-sp" ]]; then
  LIDSWITCHSET="$(GET_VAR "config" "settings/advanced/lidswitch")"
fi
BACKLIGHT_VAL="$(DISPLAY_READ lcd0 getbl)"

# Override idle settings.
SET_VAR "config" "settings/power/idle_display" 0
SET_VAR "config" "settings/power/idle_mute" 0
SET_VAR "config" "settings/power/idle_sleep" 0
if [[ "$DEVICE_ID" == "rg35xx-sp" || "$DEVICE_ID" == "rg34xx-sp" ]]; then
  SET_VAR "config" "settings/advanced/lidswitch" 0
fi
HOTKEY restart

# bind RBDIR to /tmp/rockbox.
if [ ! -f "$RBDIR_BIND/rockbox" ]; then
  mkdir -p "$RBDIR_BIND"
  mount --bind "$RBDIR" "$RBDIR_BIND"
fi

# -------
# ROCKBOX
# -------

# Load device file to set env variables.
if [[ "$DEVICE_ID" == "rg28xx-h" || "$DEVICE_ID" == "rg34xx"* || 
      "$DEVICE_ID" == "rg35xx"* || "$DEVICE_ID" == "rg40xx"* || 
      "$DEVICE_ID" == "rgcubexx-h" ]]; then
  . "$RBDIR/systems/${DEVICE_ID%%-*}.sys"
elif [[ "$DEVICE_ID" == "mgx-zero28" || "$DEVICE_ID" == "tui-spoon" ||
      "$DEVICE_ID" == "tui-brick" || "$DEVICE_ID" == "rk-g350-v" ||
      "$DEVICE_ID" == "rk-r36s-v" ]]; then
  . "$RBDIR/systems/${DEVICE_ID}.sys"
else
  . "$RBDIR/systems/fallback.sys"
fi

cd "$RBDIR" || exit

SET_VAR "system" "foreground_process" "rockbox"

# Check and patch themes to use /tmp/rockbox path.
for theme in "themes/*.cfg"; do
  sed -i 's#/.rockbox#/tmp/rockbox#g' $theme
done

> "$RBDIR/log.txt" && exec > >(tee "$RBDIR/log.txt") 2>&1

unset SDL_HQ_SCALER SDL_ROTATION SDL_BLITTER_DISABLED

$GPTOKEYB2 "./rockbox" -c "./rockbox.gptk" &
/opt/muos/bin/toybox sleep 1 # Seems like TUI Smart Pro needs a 1 sec delay... for reasons...
./rockbox --zoom $ZOOMVAL

# Restore idle settings
SET_VAR "config" "settings/power/idle_display" $IDLEDISP
SET_VAR "config" "settings/power/idle_mute" $IDLEMUTE
SET_VAR "config" "settings/power/idle_sleep" $IDLESLEEP
if [[ "$DEVICE_ID" == "rg35xx-sp" || "$DEVICE_ID" == "rg34xx-sp" ]]; then
  SET_VAR "config" "settings/advanced/lidswitch" $LIDSWITCHSET
fi
HOTKEY restart
# Restore backlight value
DISPLAY_WRITE lcd0 setbl $BACKLIGHT_VAL

kill -9 "$(pidof gptokeyb2)"

