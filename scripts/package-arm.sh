#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig
make -B -j4 BUILD=build/arm64 CXX=aarch64-linux-gnu-g++ LDFLAGS="-static-libstdc++ -static-libgcc" all
mkdir -p build/portmaster/choochoodubsiren build/nextui
cp build/arm64/dubsiren build/portmaster/choochoodubsiren/dubsiren.aarch64
cp build/arm64/dubsiren build/nextui/dubsiren.aarch64
cp packaging/portmaster/ChooChooDubSiren.sh build/portmaster/
cp packaging/portmaster/dubsiren.gptk packaging/portmaster/port.json README.md LICENSE.tracker build/portmaster/choochoodubsiren/
cp packaging/nextui/launch.sh packaging/nextui/pak.json README.md LICENSE.tracker build/nextui/
chmod +x build/portmaster/ChooChooDubSiren.sh build/portmaster/choochoodubsiren/dubsiren.aarch64 build/nextui/launch.sh build/nextui/dubsiren.aarch64
rm -f build/choochoodubsiren.zip build/ChooChooDubSiren.pak.zip
(cd build/portmaster && zip -qr ../choochoodubsiren.zip ChooChooDubSiren.sh choochoodubsiren)
(cd build/nextui && zip -qr ../ChooChooDubSiren.pak.zip .)
