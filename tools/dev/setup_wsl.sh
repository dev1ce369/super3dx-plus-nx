#!/bin/bash
# Set up a build machine: Ubuntu 24.04 (a WSL2 distro is fine). Everything it installs besides apt packages goes
# under $MT3DX_HOME (default ~/.super3dx): the Python environment and, with --android, the Android SDK and NDK.
#
#   tools/dev/setup_wsl.sh [--android]
#
# --android also downloads Google's command-line tools and runs `sdkmanager --licenses` (that ACCEPTS Google's
# Android SDK licences on this machine: read them first at https://developer.android.com/studio/terms),
# then installs build-tools 35, platform 34 and NDK 26.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
HOME_DIR="${MT3DX_HOME:-$HOME/.super3dx}"
ANDROID=0
[ "${1:-}" = "--android" ] && ANDROID=1
SUDO=; [ "$(id -u)" != 0 ] && SUDO=sudo

echo "== apt packages"
export DEBIAN_FRONTEND=noninteractive
$SUDO apt-get update -qq
$SUDO apt-get install -y -qq build-essential binutils python3 python3-pip python3-venv python3-dev ffmpeg git \
  cmake ninja-build pkg-config libsdl2-dev libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev unzip zip rsync file \
  xz-utils curl ca-certificates fonts-urw-base35 fonts-noto-cjk openjdk-17-jdk-headless
# the menu pictures' text needs these fonts; ffmpeg must decode Bink and encode H.264/AAC
ffmpeg -hide_banner -decoders 2>/dev/null | grep -qi binkvideo || { echo "this ffmpeg has no Bink decoder" >&2; exit 1; }
ffmpeg -hide_banner -encoders 2>/dev/null | grep -q libx264 || { echo "this ffmpeg has no libx264" >&2; exit 1; }

echo "== Python environment in $HOME_DIR/venv"
mkdir -p "$HOME_DIR"
[ -d "$HOME_DIR/venv" ] || python3 -m venv "$HOME_DIR/venv"
"$HOME_DIR/venv/bin/pip" install -q -r "$HERE/tools/requirements.txt"
"$HOME_DIR/venv/bin/python" -c "import capstone, unicorn, PIL, numpy; print('python packages ok')"

if [ "$ANDROID" = 1 ]; then
  echo "== Android SDK in $HOME_DIR/android-sdk"
  SDK="$HOME_DIR/android-sdk"
  mkdir -p "$SDK/cmdline-tools"
  if [ ! -x "$SDK/cmdline-tools/latest/bin/sdkmanager" ]; then
    curl -sSL -o /tmp/cmdtools.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
    rm -rf /tmp/cmdline-tools && unzip -q /tmp/cmdtools.zip -d /tmp && mv /tmp/cmdline-tools "$SDK/cmdline-tools/latest"
  fi
  export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
  yes | "$SDK/cmdline-tools/latest/bin/sdkmanager" --licenses > /dev/null || true
  "$SDK/cmdline-tools/latest/bin/sdkmanager" "build-tools;35.0.0" "platforms;android-34" "ndk;26.1.10909125"
fi

cat <<EOF

Done. In each new shell:
  export MT3DX_VENV="$HOME_DIR/venv"
  export PATH="\$MT3DX_VENV/bin:\$PATH"
EOF
[ "$ANDROID" = 1 ] && cat <<EOF
  export ANDROID_HOME="$HOME_DIR/android-sdk"
  export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
EOF
true
