#!/bin/sh
APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$APP_DIR" || exit 1
exec >"$APP_DIR/log.txt" 2>&1
export LD_LIBRARY_PATH="$APP_DIR/libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export XDG_DATA_HOME="$APP_DIR/userdata"
export SDL_VIDEODRIVER=mmiyoo
export SDL_AUDIODRIVER=mmiyoo
export DUBSIREN_HANDHELD=1
: "${miyoodir:=/mnt/SDCARD/miyoo}"
export miyoodir
if [ -f /mnt/SDCARD/.tmp_update/script/stop_audioserver.sh ]; then
  sh /mnt/SDCARD/.tmp_update/script/stop_audioserver.sh
  sleep 1
fi
unset LD_PRELOAD
exec ./dubsiren
