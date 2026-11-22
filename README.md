# Super3DX+

This is an accessibility-focused native port of *Wangan Midnight Maximum Tune 3DX+* with extensive controller support, native wheel support, and a new console-style frontend. It is a continuation of [Super3DXPlus by smolderyguest](https://github.com/smolderyguest/Super3DXPlus), which is based on the static recompilation work of [Re3DX+](https://github.com/Allannn-sudo). Full credit for the original PC, Android and Raspberry Pi port, its tools and its documentation goes to **smolderyguest**. This repository adds the Nintendo Switch port, the READY-button fix, Switch socket initialization and diagnostics, and matching Android/NRO icons. The player was rebuilt on SDL2 alongside the original Android player. It supports both the Japan and Export (English) versions of the game, and runs on:

- PC (Windows / Linux)
- Android
- Raspberry Pi
- Nintendo Switch

> **Switch test status:** the Switch build has been tested on hardware with the **Export (English)** version. The **Japan (JPN)** version has not been tried on Switch yet.

The goal of this project is to allow you to play the game natively, and access its features like car saving/loading without needing to run several additional programs and compatibility layers to do so.

Showcase videos: 

https://www.youtube.com/watch?v=WwAjkctDoF4 (Solo play)

https://www.youtube.com/watch?v=_sUFo9nlgWI (Online VS)

## Copyright disclaimer

This repository does **NOT** contain any copyrighted content from the original game or from Bandai Namco Entertainment, Inc. or its affiliates, and nothing generated from it. It contains only free, open-source (MIT) tools for building the conversion from **your own legally obtained game data**. See [Quick start](#quick-start).

*Wangan Midnight Maximum Tune* is a trademark of Bandai Namco Entertainment, Inc. This is an UNOFFICIAL, strictly NON-COMMERCIAL fan project and is not affiliated with them in any way, shape or form, written or implied.

**Where can I get the APK / EXE?**

You can't. Any pre-built APK or PC EXE you see online is an unauthorized distribution of copyrighted content, and is not affiliated with this project or its developers. Running unsigned executables from strangers is a bad idea anyway.

**Where can I get the Switch NRO?**

Download `super3dx+NX.zip` from [GitHub Releases](https://github.com/dev1ce369/super3dx-plus-nx/releases/latest). The ZIP contains only this layout:

```text
super3dx+NX/
└── switch/
    └── super3dx+nx.nro
```

The NRO contains only this project's code — **no game data, no dump, no converted assets**. Unzip it, take `super3dx+nx.nro` from `super3dx+NX/switch/`, and put it beside your own converted `data/` folder in `sdmc:/switch/super3dx+nx/`. A standalone NRO is also attached to the release. See [docs/SWITCH.md](docs/SWITCH.md) for the exact folder layout and steps.

## Quick start

**You need your own (LEGALLY OBTAINED) copy of the game:** 

*Wangan Midnight Maximum Tune 3DX+ (Export)*, or

*Wangan Midnight Maximum Tune 3DX+ (JPN)*

These should have a folder with a file called `main` and a folder called `data` inside (from the Japan or Export version of 3DX+; `docs/BUILDS.md` lists the versions known to work). Nothing from the game is included or downloaded. You also need about 15 GB of free disk space, an internet connection for the first run, and an hour or more of waiting.

**Windows**

1. On this page, click the green **Code** button, then **Download ZIP**, and unzip it somewhere (not inside a protected folder like Program Files).
2. Double-click **`setup.bat`**.
3. Follow the prompts. It installs what it needs (it will offer to install Python and ffmpeg if you don't have them), asks you to pick your game folder, and builds everything.
4. When it says it's done, it tells you which folder your game is in. Open it and double-click **`super3dx.exe`**.

**Linux / macOS**

Download the code, open a terminal in its folder and run `./setup.sh`. It installs the Python packages by itself, and tells you the one `apt install` command to run if a compiler or SDL2 is missing.

**Raspberry Pi and Android** (after the setup above)

Run **`setup-pi.bat`** / `./setup-pi.sh` to build and install the game on a Raspberry Pi over your network, or **`setup-android.bat`** / `./setup-android.sh` to build one self-contained Android app (and install it on a phone connected by USB). They download what they need and ask before anything big. No PC at all? `docs/ANDROID_ON_DEVICE.md` builds everything on the Android device itself, in Termux.

**Updating to a new version** (no full rebuild needed)

1. Run **`setup.bat`** (or `./setup.sh`). It first checks GitHub and, if there is a newer version, offers to install it: with `git pull` in a git checkout, or by downloading the latest files and replacing the ones that differ (the old copies are kept in `build/update-backup/`; unzipping a new ZIP over the old folder works too). `setup.bat update` does only that check. Your finished game folders (`Super3DXPlus-Export`, `Super3DXPlus-JP`), your saves and the tools the setup downloaded (`.super3dx`) are never touched.
2. The same run then carries on and rebuilds. It remembers the game folder of your last build (it asks once whether to use it again), recognises your earlier build and tells you what it will do:
   - **Most updates (game code, menus, fixes):** it only rebuilds the game program, in a few minutes. Your converted data and saves are not touched.
   - **When the data converters changed** (the changelog says so): it converts the data again (about an hour) and copies your saves into the new build.
3. For the Raspberry Pi or Android, run `setup-pi` / `setup-android` again afterwards. The Pi script only re-sends data that changed; the Android script rebuilds the app in a few minutes, and installing it over the old one keeps your saves (as long as you keep the signing key in `.super3dx/android/debug.keystore`).

Add `--full` to the setup (`python build.py wizard --full`) to force a complete rebuild if you ever want one.

**If something goes wrong:** the setup stops with a plain-language explanation of what probably happened and what to do, and saves a full log in `build/logs/` (named `setup-<date>.txt`). If you are stuck, open an issue and attach that file. `docs/BUILDING.md` has a list of common problems and their fixes.

**Prefer to do it by hand, or build for something else?** `docs/BUILDING.md` has the complete manual steps for Windows and Linux (no installer), the individual build commands, and the Raspberry Pi and Android builds (also `docs/PI.md`). Controls and remapping are in `docs/INPUT.md`. To build both the Japan and Export versions, run the setup again and pick the other game folder.

**Nintendo Switch:** after the main setup, install devkitPro with `switch-dev`, `switch-sdl2`, `switch-mesa` and `switch-zlib`, then run `python build.py switch` (Windows finds devkitPro's MSYS2 shell automatically). Copy the resulting `platform/switch/super3dx+nx.nro` and your converted `data/` folder to `sdmc:/switch/super3dx+nx/`, and launch it from the Homebrew Menu. See `docs/SWITCH.md` for details. The NRO and the converted data are never committed here.

**Are you wild man who wants to build the entire thing on your android phone without a PC?**

See `docs/ANDROID_ON_DEVICE.md`
It is substantially harder than building it on a PC, but it is possible.

## Layout

```
build.py        build driver (Windows, Linux, macOS)
src/            the game (platform-neutral C)
platform/       per-target code: sdl2/ (PC, Pi), android/, pi/, switch/
tools/          data pipeline (Python): converters, generators, packers, per-build address tables
test/           desktop tests in C
third_party/    stb, zlib, Khronos GLES2 headers (each with its own notice)
docs/           building, testing, Pi, versus, input, builds, architecture
```

## NOTE - Wheel / FFB support

Basic FFB/wheel support is currently implemented, and has been tested to be working fairly well.
If you get the game running and use a wheel, please let me know how your experience goes in this issue tracker:
https://github.com/smolderyguest/Super3DXPlus/issues/2

Known working wheels:
- Logitech ( RS50, G29, G920, DFGT )
- Thrustmaster ( T300RS )
- Fanatec ( ClubSport DD+ )
- Moza ( R5, R12 )

Currently unknown if these wheels work with it:
- Simagic
- Cammus
- Simucube
- VRS / OSW type direct-drive

## What makes this "native"?

This is not an emulator wrapper. The arcade's car physics are statically recompiled from the game's own executable, and everything else (rendering, menus, sound, netcode) is a ground-up reconstruction. The game code runs on bare metal and "just works" without emulator setup, and most of the work goes into quality-of-life improvements for all sorts of control schemes.

## What's different from Re3DX+

Re3DX+ was touch-only, Japan-only, and basic in functionality. Super3DX+ adds:

- Controller support, with vibration and input filtering
- Support for both Export and Japan versions
- Menus and flow reworked for a non-arcade setting, as authentic as possible
- A rebuilt car save/load system, like a modern Banapass-style garage
- The original race UI restored, with touch controls optional
- Restored features: 10 Outrun music, story-mode visual upgrades (roll cages, exhaust, Gatchan's Celsior parts), body color customization, and more
- Many bug fixes, such as broken texture conversions and missing tuning parts

This only scratches the surface; see `CHANGELOG.md`.

## Why the Pi?

The Raspberry Pi build turns a Pi into an appliance that boots straight into the game over composite video: "what if WMMT3 had a home-console port?" Flash the card, put it in the Pi, play (`docs/PI.md`).

See: https://www.youtube.com/watch?v=vMuYHEGcGEk

- **Pi 5:** solder a composite video jack onto the J7 pads for composite video, and use a USB sound card for analog audio.
- **Pi 4 and below:** use a TRRS-to-composite (camcorder style) cable in the headphone jack.
- **HDMI:** install with `tools/pi/install.sh --hdmi`. Untested on real hardware.

Only tested on a Raspberry Pi 5 (8 GB). Other models should work with simple config tweaks, but I don't have the hardware to test.

## Status

**Not implemented:**
- Ghost VS. Battle / Crowns (lack of interest - Re3DX+ is strictly native, doesn't use a server to sync crowns, so it would be quite boring.)
- MacOS support (OpenGL ES has iffy support. Could be possible.)

**Known bugs:**
- Menu slider animations are buggy in some cases.
- Sometimes, VS battles will hang at the start of a race indefinitely. This issue is hard to reproduce, but if you encounter it, please close the game and upload the log.txt from both machines to the issues page.
- Headlight flare positions for story rival cars are incorrect.

**Oversights:**
- The Japan build has been tested much less than the Export build, because I can't read Japanese. Please submit an issue if you encounter any issues with the text, with as much information as possible.
- Story mode has not been played all the way through yet, so it is unknown if any bugs come up in certain story mode missions.

**Maybe later (NOT planned atm. Just possibly could be added down the line):**
- Rollback netcode for Online VS
- Easier EXE installer / updater
- Implementation of online leaderboards/ghost battles, with community-hosted servers.


## Licence and credits

MIT (`LICENSE`), keeping Re3DX+'s copyright. Game content is not covered and not included. See `NOTICE.md`.

- **Super3DX+**: developed by [smolderyguest](https://github.com/smolderyguest).
- **Re3DX+** by [Allannn-sudo](https://github.com/Allannn-sudo): the original conversion effort, including the static recompilation of the physics and the first rewrite of rendering, menus, HUD, sound and data converters (MIT).
- **libretro / Lakka-LibreELEC**: reference for the Pi's composite output and filtering method.
