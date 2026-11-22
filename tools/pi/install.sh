#!/bin/bash
# Installs the game on a Pi that has booted Raspberry Pi OS Lite (64-bit, trixie) with SSH and your user (set in the Raspberry Pi Imager, docs/PI.md):
#
#   tools/pi/install.sh <user@host> <converted data dir> [--no-data] [--hdmi]
#
# It copies the arm64 player (build/pi/super3dx) and the data to ~/game, installs the runtime libraries (minimal: SDL2, Mesa, ALSA),
# the CRT settings, the USB audio default, and a systemd service that starts the game at boot. Safe to run again (the data copy
# is skipped with --no-data, and an existing save/ folder and crt.txt are never overwritten). Needs a network on the Pi for apt.
# --hdmi: the HDMI service (platform/pi/files/wmmt-hdmi.service: no CRT pass, the screen's size) in place of the composite one, and HDMI
# audio (the USB default in /etc/asound.conf is removed). Not tested on hardware: docs/PI.md "HDMI output".
set -euo pipefail
cd "$(dirname "$0")/../.."
TARGET="${1:?usage: install.sh user@host data-dir [--no-data] [--hdmi]}"
DATA="${2:?usage: install.sh user@host data-dir [--no-data] [--hdmi]}"
NODATA=0; HDMI=0
for a in "${3}"; do
  case "$a" in --no-data) NODATA=1 ;; --hdmi) HDMI=1 ;; *) echo "unknown option $a" >&2; exit 1 ;; esac
done
[ -f build/pi/super3dx ] || { echo "no build/pi/super3dx: run tools/pi/sysroot.sh once, then SYSROOT=/opt/pi-sysroot platform/sdl2/build.sh" >&2; exit 1; }
SSH="ssh -o StrictHostKeyChecking=accept-new $TARGET"
F=platform/pi/files
GAME='$HOME/game'
$SSH 'mkdir -p ~/game/data ~/game/save'
# the player: next to the old one, then moved over it (the game may be running)
scp build/pi/super3dx "$TARGET:game/super3dx.new"
$SSH 'sudo systemctl stop wmmt 2>/dev/null; mv ~/game/super3dx.new ~/game/super3dx && chmod +x ~/game/super3dx'
if [ "$NODATA" = 0 ]; then
  echo "copying the data (this takes a few minutes)..."
  N=$(find "$DATA" -type f | wc -l)
  (cd "$DATA" && tar -cf - .) | $SSH 'tar -xf - -C ~/game/data'
  echo "sent $N files; on the Pi: $($SSH 'find ~/game/data -type f | wc -l') files"
fi
# settings the user may have tuned stay: crt.txt, save/graphics
$SSH '[ -f ~/game/crt.txt ] || cat > ~/game/crt.txt' < $F/crt.txt
$SSH '[ -f ~/game/save/graphics ] || cat > ~/game/save/graphics' < $F/graphics
# the runtime: only what the player links (no desktop, no PulseAudio server)
$SSH 'sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq && sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends libsdl2-2.0-0 libegl1 libgles2 libgbm1 libgl1-mesa-dri libasound2t64 alsa-utils'
if [ "$HDMI" = 1 ]; then
  $SSH 'sudo rm -f /etc/asound.conf'
  $SSH 'sudo tee /etc/systemd/system/wmmt.service >/dev/null' < $F/wmmt-hdmi.service
else
  $SSH 'sudo tee /etc/asound.conf >/dev/null' < $F/asound.conf
  $SSH 'sudo tee /etc/systemd/system/wmmt.service >/dev/null' < $F/wmmt.service
fi
$SSH 'sudo systemctl daemon-reload && sudo systemctl enable wmmt && sudo systemctl restart wmmt'
sleep 8
$SSH 'systemctl is-active wmmt; tail -3 ~/game/game.log | cut -c1-150'
echo "installed. The game starts at every boot; to stop it: sudo systemctl stop wmmt"
