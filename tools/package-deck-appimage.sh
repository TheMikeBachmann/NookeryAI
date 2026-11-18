#!/bin/bash
# Builds the "deck" preset (CMakeUserPresets.json) and packages it as a self-contained
# AppImage that runs on SteamOS without the build environment's libraries.
# Works inside a Flatpak sandbox (no FUSE needed). Output: build/appimage/Nookery-<version>-x86_64.AppImage
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/build/appimage"
version="${APP_VERSION:-$(git -C "$root" rev-parse --abbrev-ref HEAD | tr '/' '-')}"
lib_dir="/usr/lib/$(uname -m)-linux-gnu"

cmake --build --preset deck -j"$(nproc)"
cmake --install "$root/build/deck"

mkdir -p "$out"
linuxdeploy="$out/linuxdeploy"
if [[ ! -x "$linuxdeploy" ]]; then
  curl -fL "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$(uname -m).AppImage" -o "$linuxdeploy"
  chmod +x "$linuxdeploy"
fi

rm -rf "$out/appdir" "$out"/*.AppImage
mkdir -p "$out/appdir/usr/"{bin,share/{applications,icons/hicolor}}
cp "$root/build/install/deck/Nookery" "$out/appdir/usr/bin/"
cp -r "$root/build/install/deck/res" "$out/appdir/usr/bin/"
cp -r "$root/platforms/freedesktop/"{16x16,32x32,48x48,64x64,128x128,256x256} "$out/appdir/usr/share/icons/hicolor"
cp "$root/platforms/freedesktop/com.flyngmt.nookery.desktop" "$out/appdir/usr/share/applications"

# SDL loads libusb at runtime, so linuxdeploy can't discover it. The Vulkan loader is left to the
# host so it matches the host's GPU drivers.
cd "$out"
APPIMAGE_EXTRACT_AND_RUN=1 VERSION="$version" NO_STRIP=1 "$linuxdeploy" \
  -l "$lib_dir/libusb-1.0.so" --exclude-library 'libvulkan.so*' --appdir "$out/appdir" --output appimage

ls -la "$out"/*.AppImage
