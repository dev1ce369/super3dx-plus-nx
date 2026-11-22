# Raspberry Pi 5: 480i composite on a CRT

The game runs on a Pi 5 as a normal arm64 Linux program (the SDL2 player, `platform/sdl2/`, built for arm64) with no desktop: SDL's
KMS/DRM backend draws with Mesa's V3D GPU driver straight onto the Pi's composite connector. It runs at a steady 60 fps (the
arcade's rate) with the textures at 4x anisotropic filtering, at 53 °C at most (no throttling, no fan; 5.5 minutes of racing
sampled every 5 s, the CPU at ~18% of one core). First boot to the game on a CRT was tested on 2026-11-05 with the Export
(English) data, a Pi 5 8 GB, a Sony Trinitron, a USB audio device and an Xbox 360 pad. Status: it works; see "Not done yet".

## The easy way

Run `setup-pi.bat` (Windows) or `./setup-pi.sh` after the main setup (`docs/BUILDING.md`). With a Pi that is on your network with SSH enabled (Raspberry Pi Imager's
settings: user name, password, "Enable SSH"), it builds the player on the Pi, installs the game and the boot service, and sets up composite or HDMI output. The sections
below describe what it does and the by-hand way (a prepared card, `tools/pi/install.sh`, cross compiling).

## What you need

- A Pi 5 (4 GB or 8 GB), a 32 GB SD card (the Export data is 2.5 GB), a CRT (or anything with a composite input), Ethernet for
  the setup, a USB audio device (the Pi 5 has no headphone jack; HDMI audio is untested, see Audio) and a pad.
- Composite out of the Pi 5 (its TV-out pads or header, wired to an RCA cable; the software side is the same however you do it).
- The converted data folder (`docs/BUILDING.md`: `python build.py data <dump>`); no game content is in this repository.
- A PC to build the data on (any system, `docs/BUILDING.md`), and the Raspberry Pi Imager for the card. The player itself can be built on the Pi.

## 1. Build the player for the Pi

```sh
python3 build.py exe --target pi              # on the Pi itself (after the data step has been run on some machine and the generated sources copied)
# or from a PC with an arm64 sysroot:
sudo tools/pi/sysroot.sh                      # once: the aarch64 compiler and a Debian trixie arm64 sysroot (/opt/pi-sysroot)
SYSROOT=/opt/pi-sysroot python3 build.py exe --target pi    # -> build/pi/super3dx
```

The sysroot is Debian trixie because current Raspberry Pi OS is: the binary links the glibc and SDL2 the Pi has.

## 2. Prepare the card

Use the official [Raspberry Pi Imager](https://www.raspberrypi.com/software/) (Windows, macOS and Linux):

1. Choose the Pi 5, then the operating system **Raspberry Pi OS Lite (64-bit)** (Debian trixie), then your SD card.
2. When it offers to customise the OS settings, set a **host name** (`wmmt`, or anything), a **user name and password** (the setup asks for
   the user name), tick **Enable SSH** (password, or your public key), and fill in Wi-Fi if you are not using Ethernet. This also
   skips the first-boot wizard that otherwise waits for a screen and keyboard.
3. Write the card, put it in the Pi, connect Ethernet and power, and find it with `ping wmmt.local` (your host name), then
   `ssh <user>@<address>`.

The composite video settings are not needed on the card first: `setup-pi` makes them over SSH (and `install.sh` leaves them to you, see
"The video settings" below). If you set up by hand and want them on the card before the first boot, edit `config.txt` and `cmdline.txt` on the
card's boot partition, the FAT one every system can read, with the lines below.

## 3. Install the game

```sh
tools/pi/install.sh wmmt@<address> /path/to/converted/data
```

It installs the few runtime packages (SDL2, Mesa's EGL/GLES and V3D driver, ALSA), copies the player and the data to
`~/game`, the CRT settings, the audio default and a systemd service (`wmmt`) that starts the game at every boot and restarts
it if it exits. Logs: `~/game/game.log`. Saves: `~/game/save/` (back it up; copy the folder).

## The video settings, and why

The Pi 5 has a separate display device for composite (the RP1 chip's `rp1-vec`; `/dev/dri/card0` here) and Mesa renders on
the V3D GPU and scans out through it. What it needs, found in the Lakka Pi 5 composite image and confirmed by trial:

`config.txt`:
```
dtoverlay=vc4-kms-v3d,composite       # the composite option; without it the kernel has no composite connector
enable_tvout=1
disable_splash=1
```
`cmdline.txt` (one line, appended):
```
video=Composite-1:720x480,tv_mode=NTSC-J drm_rp1_vec.cmode=27000:721/16/64/58,480/6/6/34
```
`NTSC-J` is NTSC without the 7.5 IRE black pedestal (the arcade is Japanese). The `cmode` is the 480i timing Lakka uses (27 MHz, 721 x 480
active). The connector appears as `card0-Composite-1` (`720x576i` is listed too: PAL is
`tv_mode=PAL`). SDL then reports a 720x480, 60 Hz window.

### The CRT pass (`--crt`, `crt.txt`)

A television hides the edge of the frame (overscan) and a one-pixel horizontal line in an interlaced frame shows in one field
only, so the menus flicker. With `--crt` the player draws the game 640x480 into a texture and puts it on the 720x480 frame
(`platform/sdl2/main_sdl.c`, `crt_present`) inside a safe area, through a bilinear filter, with a vertical flicker filter (each
line takes a quarter of the lines above and below it). `~/game/crt.txt`:

| key | meaning | default |
|---|---|---|
| `safe_x`, `safe_y` | the part of the frame the picture covers (1.0 = all, no border) | 0.90 |
| `flicker` | 0 = off .. 1 = full | 0.5 |
| `width`, `height` | the game's drawing size | 640 x 480 |

Edit and `sudo systemctl restart wmmt`. A smaller safe area gives a smaller, softer picture (the vertical scale is not an integer:
0.90 x 480 = 432 lines); raise it as far as your set's overscan allows.

### Texture filtering (`~/game/save/graphics`)

`aniso 4` and `lodbias -2`: sharper distance textures than the PC default (-1); a CRT at 480 lines shows detail a PC does not.
On the Pi (and with `--crt`) the player keeps the high-quality effects off unless `MT3DX_HQ=1` (a PC has them on; `MT3DX_HQ=0` turns them off); the service sets `WMMT_ANISO=4` (read by `app.c`) to turn it on.
Mip bias runs -3 .. 3 (negative sharper); 8x anisotropic was judged too much.

## Audio

The game opens ALSA's `default`. The Pi has two HDMI audio cards that are listed first, with nothing on them, so a USB device is
card 2 and `default` points at an HDMI port: "no audio: ALSA: Couldn't open audio device: Unknown error 524" in the log.
`platform/pi/files/asound.conf` (installed to `/etc/asound.conf`) pins `default` to the card named `Audio` (`aplay -l`;
change `CARD=` for yours, or delete the file for HDMI sound).

## Troubleshooting (all of these happened)

- **Nothing on the CRT, the Pi answers on the network**: the composite option is missing (`dtoverlay=vc4-kms-v3d,composite`).
  `ls /sys/class/drm` must show `card0-Composite-1 connected`.
- **SSH says "valid user has been set up", then permission denied**: the first-boot script did not run; the image's
  `userconfig.txt` and cloud-init `user-data` do nothing on trixie.
- **Four green flashes at power-on**: the Pi could not read the card: reseat it, power-cycle. (After writing to the card on
  Windows, remove it only after the script has printed its last line.)
- **`wsl --mount` of the card fails** ("cannot find the drive"): WSL cannot attach SD readers (removable media); use the boot
  partition scripts here instead of editing the root partition.
- **"SDL_Init: kmsdrm not available" when restarting the game**: the old player still holds the display; wait a few seconds
  (the service does: `RestartSec`).
- **kmscube exits at once**: it dies on SIGPIPE if its output goes through `head`; use a file.

## Not done yet

- A clean shutdown: the game cannot be quit with the pad; powering off by cutting power risks the save (use `ssh wmmt@... sudo poweroff`
  until there is a power/quit path), and a read-only root with a tmpfs would make a power cut safe.
- Pad mapping on the Pi (an Xbox 360 pad works; other pads untested), the launcher/boot menu, a Buildroot image instead of
  Raspberry Pi OS.
- Audio over HDMI, 4:3 versus the arcade's aspect on a 16:9 screen: untested.
- `tools/pi/install.sh` is the tested steps written down as a script; it was run in its pieces during the first bring-up, not yet as
  a script from a clean start. (The first bring-up wrote the card with its own scripts; the Raspberry Pi Imager route above replaces them and
  was not run on this hardware.)

## HDMI output (optional)

The default is composite on a CRT (above). For an HDMI screen instead, the same player runs without the CRT pass:

1. Leave the composite settings out of `config.txt` and `cmdline.txt` (`setup-pi` asks which output you want), or delete the
   `video=Composite-1...` and `drm_rp1_vec.cmode=...` parts of `cmdline.txt` and `enable_tvout=1` from `config.txt` on a card already made.
2. Install with `tools/pi/install.sh wmmt@<address> /path/to/data --hdmi`. That uses `platform/pi/files/wmmt-hdmi.service` (no `--crt`,
   fullscreen at 1920x1080: edit `--size` for your screen's mode) and removes the USB audio default (`/etc/asound.conf`) so ALSA's `default`
   is the HDMI sink.

The game is 4:3, so a 16:9 screen gets a centred picture with bars at the sides. This path is **untested on hardware**: SDL's KMS/DRM
backend takes the first connected connector and the picture size follows `--size`; if the Pi picks the wrong output or a mode the
screen does not take, check `~/game/game.log` and set the mode with a `video=HDMI-A-1:1920x1080@60` entry on `cmdline.txt`.
