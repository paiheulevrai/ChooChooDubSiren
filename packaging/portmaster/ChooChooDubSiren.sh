#!/bin/bash
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d /opt/system/Tools/PortMaster ]; then
  controlfolder=/opt/system/Tools/PortMaster
elif [ -d /opt/tools/PortMaster ]; then
  controlfolder=/opt/tools/PortMaster
elif [ -d "$XDG_DATA_HOME/PortMaster" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder=/roms/ports/PortMaster
fi
source "$controlfolder/control.txt" || exit 1
[ -f "$controlfolder/mod_${CFW_NAME}.txt" ] && source "$controlfolder/mod_${CFW_NAME}.txt"
get_controls
GAMEDIR="/$directory/ports/choochoodubsiren"
cd "$GAMEDIR" || exit 1
exec >"$GAMEDIR/log.txt" 2>&1
export XDG_DATA_HOME="$GAMEDIR/userdata"
export DUBSIREN_HANDHELD=1
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export HOTKEY=guide
if [ "$CFW_NAME" = "ArkOS" ] && [ -z "${AUDIODEV:-}" ]; then
  export DUBSIREN_AUDIODEV_FALLBACK="plughw:0,0"
fi
if [ "$CFW_NAME" = "ROCKNIX" ]; then
  $GPTOKEYB2 "dubsiren.${DEVICE_ARCH}" -c "./dubsiren.gptk" &
else
  $GPTOKEYB "dubsiren.${DEVICE_ARCH}" -c "./dubsiren.gptk" &
fi
pm_platform_helper "$GAMEDIR/dubsiren.${DEVICE_ARCH}"
"./dubsiren.${DEVICE_ARCH}"
pm_finish
