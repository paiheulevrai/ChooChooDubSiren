#!/bin/sh
PAK_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$PAK_DIR" || exit 1
LOG_DIR=${LOGS_PATH:-$PAK_DIR}
mkdir -p "$LOG_DIR" || exit 1
exec >>"$LOG_DIR/ChooChooDubSiren.txt" 2>&1
export XDG_DATA_HOME="$PAK_DIR/userdata"
export DUBSIREN_NEXTUI=1
export DUBSIREN_HANDHELD=1
if [ -z "${AUDIODEV:-}" ] && [ -L /sys/kernel/config/usb_gadget/g1/configs/c.1/uac1.usb0 ]; then
  export AUDIODEV=plughw:CARD=audiocodec,DEV=0
fi
export SDL_GAMECONTROLLERCONFIG="${SDL_GAMECONTROLLERCONFIG:+$SDL_GAMECONTROLLERCONFIG
}030003f05e0400008e02000014010000,TRIMUI Player1,a:b1,b:b0,x:b3,y:b2,back:b6,start:b7,guide:b8,leftshoulder:b4,rightshoulder:b5,leftstick:b9,rightstick:b10,lefttrigger:a2,righttrigger:a5,leftx:a0,lefty:a1,rightx:a3,righty:a4,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,platform:Linux,"
exec ./dubsiren.aarch64
