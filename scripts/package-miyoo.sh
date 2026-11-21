#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${MIYOO_TOOLCHAIN:?Set MIYOO_TOOLCHAIN to the Miyoo toolchain directory}"
: "${MIYOO_SDL2_ROOT:?Set MIYOO_SDL2_ROOT to the Miyoo SDL2 SDK directory}"
compiler="$MIYOO_TOOLCHAIN/bin/arm-linux-gnueabihf-g++"
sysroot="$MIYOO_TOOLCHAIN/arm-linux-gnueabihf/libc"
output=build/miyooports
app="$output/Roms/PORTS/Games/ChooChooDubSiren"
shortcut="$output/Roms/PORTS/Shortcuts"
mkdir -p "$app/libs" "$app/docs" "$shortcut"
cp -R licenses "$app/"
"$compiler" -std=c++17 -O2 -Wall -Wextra --sysroot="$sysroot" \
  -I"$MIYOO_SDL2_ROOT/include" -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
  src/main.cpp src/siren.cpp src/fonts16x24.cpp -L"$MIYOO_SDL2_ROOT/lib" \
  -lSDL2 -Wl,--allow-shlib-undefined -lstdc++fs -static-libstdc++ -static-libgcc \
  -ldl -lpthread -lm -o "$app/dubsiren"
"$MIYOO_TOOLCHAIN/bin/arm-linux-gnueabihf-strip" "$app/dubsiren"
cp "$MIYOO_SDL2_ROOT/lib/libSDL2-2.0.so.0" "$MIYOO_SDL2_ROOT/lib/libneonarmmiyoo.so" "$app/libs/"
cp "$MIYOO_SDL2_ROOT/COPYING.txt" "$app/libs/SDL2-COPYING.txt"
cp "$MIYOO_SDL2_ROOT/MIYOO-LICENSE.txt" "$app/libs/MIYOO-LICENSE.txt"
cp packaging/miyooports/launch.sh README.md LICENSE.tracker "$app/"
cp docs/*.png "$app/docs/"
cp packaging/miyooports/ChooChooDubSiren.port "$shortcut/"
chmod +x "$app/dubsiren" "$app/launch.sh" "$shortcut/ChooChooDubSiren.port"
rm -f build/ChooChooDubSiren-MiyooPorts.zip
(cd "$output" && zip -qr ../ChooChooDubSiren-MiyooPorts.zip Roms)
