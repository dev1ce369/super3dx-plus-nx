#!/bin/bash
# Sets up cross-compiling for the Pi 5 on an Ubuntu 24.04 (WSL) host, as root: the aarch64 compiler and a Debian trixie arm64
# sysroot (the base of current Raspberry Pi OS, so its glibc is what the Pi has) with the development files of SDL2, Mesa's
# EGL/GLES and libdrm in /opt/pi-sysroot. Takes a while (the second stage runs under qemu). Then:
#
#   SYSROOT=/opt/pi-sysroot platform/sdl2/build.sh        -> build/pi/super3dx (an arm64 executable)
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run as root (sudo)" >&2; exit 1; }
export DEBIAN_FRONTEND=noninteractive
SYSROOT="${SYSROOT:-/opt/pi-sysroot}"
apt-get update
apt-get install -y gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu debootstrap qemu-user-static debian-archive-keyring pkg-config
if [ ! -f "$SYSROOT/.done" ]; then
  rm -rf "$SYSROOT"
  debootstrap --arch=arm64 --variant=minbase --foreign --include=ca-certificates trixie "$SYSROOT" http://deb.debian.org/debian
  cp /usr/bin/qemu-aarch64-static "$SYSROOT/usr/bin/"
  chroot "$SYSROOT" /debootstrap/debootstrap --second-stage
  chroot "$SYSROOT" apt-get install -y --no-install-recommends libsdl2-dev libdrm-dev libgbm-dev libegl-dev libgles-dev \
    libegl1-mesa-dev zlib1g-dev libc6-dev libgcc-14-dev libasound2-dev libudev-dev
  touch "$SYSROOT/.done"
fi
echo "sysroot ready in $SYSROOT"
