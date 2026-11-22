# Building

## The easy way: the setup

Windows: double-click `setup.bat`. Linux, macOS, Raspberry Pi: run `./setup.sh`. Both start `python build.py wizard` (plain `python build.py` does the
same). It:

1. installs what it can by itself: Python (Windows, with your OK, through winget), the Python packages (in a private environment under `.super3dx/venv`
   if the system Python refuses them), the Zig compiler and the SDL2 library (downloaded into `.super3dx/`), and ffmpeg (Windows, with your OK, through
   winget). On Linux it cannot use `sudo` for you, so it prints the one `apt install` line to run and stops;
2. asks for your copy of the game (a folder with `main` and `data/`; it opens a folder picker, or you can paste or drag the folder into the window),
   and tells you whether it is the Japan or the Export version;
3. converts the data and builds the player (an hour or more, nothing to click);
4. moves the result to `Super3DXPlus-Export/` or `Super3DXPlus-JP/` next to the sources (never over an earlier one, since that may hold saves).

To build the other version too, run it again and pick the other game folder.

**Updating.** After a new version of Super3DX+ (`git pull` in a checkout, or a new ZIP copied over the old folder, keeping your `Super3DXPlus-*` folders and `.super3dx`),
run the setup again. Each finished build carries a `build-info.json` (a fingerprint of the data tools, the game's `main`, the date and the source commit). If the data
tools have not changed it only regenerates the sources and rebuilds the player (a few minutes) and leaves the data and `save/` alone; if they changed, it converts the data
again (about an hour) and copies your saves into the new build. `python build.py wizard --full` forces a complete rebuild. The setup checks GitHub for a newer version first (`python build.py update` does only that; `SUPER3DX_NO_UPDATE=1` skips it): in a git checkout it offers `git pull`, otherwise it downloads the latest files and replaces the ones that differ, keeping the old copies in `build/update-backup/` (`tools/updater.py`). If the setup's own files changed it restarts itself, and it remembers the game folder of your last build.

**Raspberry Pi and Android.** Run `setup-pi.bat` / `./setup-pi.sh` and `setup-android.bat` / `./setup-android.sh` after the main setup (below). Everything it prints is also saved in `build/logs/setup-<date>.txt`; if it
stops, it says in plain words what probably happened and what to do. If you ask for help, attach that file.

## By hand, start to finish (Windows)

The setup does exactly this; do it yourself if you want control over where things go.

1. Install [Python 3.9 or newer](https://www.python.org/downloads/) (tick "Add python.exe to PATH") and, optionally, [Git](https://git-scm.com/).
2. Get the sources: `git clone https://github.com/smolderyguest/Super3DXPlus.git`, or download the ZIP from GitHub and unzip it to a short path such as
   `C:\Super3DXPlus` (very long paths and non-English characters in the path can make Windows tools fail).
3. In a terminal in that folder:
   ```
   pip install -r tools/requirements.txt
   python build.py setup-zig
   python build.py setup-sdl2
   winget install Gyan.FFmpeg
   python build.py doctor
   python build.py all "C:\path\to\your\game\folder"
   ```
   (`doctor` lists anything still missing. Setup checks that the ffmpeg it finds runs and has the Bink decoder, and if there is none that works it offers
   winget, or a portable copy it downloads into `.super3dx/ffmpeg` (a pinned "full" build, its SHA-256 checked). By hand: install the "full" build from
   https://www.gyan.dev/ffmpeg/builds/ and put it on PATH, or set the `FFMPEG` environment variable to the ffmpeg.exe.)
4. Run `build\windows\package\super3dx.exe`. Copy or zip the `package` folder to keep or move the game.

On Linux: `sudo apt install build-essential libsdl2-dev libegl1-mesa-dev libgles2-mesa-dev ffmpeg python3-pip python3-venv`, then
`pip install -r tools/requirements.txt` (use a virtual environment if pip refuses: `python3 -m venv .venv && . .venv/bin/activate`), then
`python3 build.py all /path/to/your/game/folder`; run `build/linux/package/play.sh`.

## If something goes wrong

- The setup's log is `build/logs/setup-<date>.txt`; each conversion step also has its own `build/logs/<step>.log`. Open an issue and attach the newest.
- "no ffmpeg", "no compiler", "no SDL2": run `python build.py doctor`; it names each missing piece and the command that fixes it.
- "Access is denied", "being used by another process": close File Explorer windows inside the folder and any running copy of the game, pause the antivirus
  or exclude the folder, and make sure the folder is not under Program Files.
- "The drive ran out of space": the build needs about 15 GB free.
- A path-too-long error: move the project to a short path such as `C:\Super3DXPlus`. (The Android setup keeps its SDK in `%LOCALAPPDATA%\Super3DX\android-sdk` by itself when the project's folder is deep, so only the rest of the setup needs the short path.)
- A download failed: check the internet connection, a firewall, a VPN or a proxy; the message gives the file's address so you can fetch it by hand.
- "GENERATION STOPPED": the tools could not read the game's `main`. Check that the version is one of those in `docs/BUILDS.md` and that the copy is complete.
- A conversion step failed: `python tools/pipeline.py convert <game folder> --only <step>` re-runs just that step once the cause is fixed.

## The pieces

Everything is driven by `build.py` (the player) and `tools/pipeline.py` (the data); both are plain Python 3.9+, the same on Windows,
Linux and macOS. `python build.py doctor` shows what is installed and what is missing, with the command to get it.

Nothing from the arcade game is in this repository. Two things are made from **your own dump** of the game (a folder with `main` and
`data/`, e.g. from your cabinet's drive): the *data* the player reads (`data/`, a few GB) and the *generated sources* (tables, and the
physics recompiled from `main`; `src/phys/phys_rc.c` is about 18 MB). Both stay out of git (`.gitignore`). Never upload them.

## 1. Get the tools

| You need | Windows | Linux / macOS |
|---|---|---|
| Python 3.9+ and the packages | python.org, then `pip install -r tools/requirements.txt` | the same (`python3`) |
| a C compiler | **Zig** (`python build.py setup-zig`, no installer) or MinGW-w64 (MSYS2: `pacman -S mingw-w64-x86_64-gcc`) | gcc or clang (`apt install build-essential`) |
| SDL2 development files | `python build.py setup-sdl2` (the mingw package, from libsdl.org's release) | `apt install libsdl2-dev libegl1-mesa-dev libgles2-mesa-dev` |
| ffmpeg with the Bink decoder (sound, movies) | a full build: `winget install Gyan.FFmpeg`, or set `FFMPEG` | `apt install ffmpeg` |
| a C++ demangler | built automatically with Zig (`tools/demangle.cpp`); nothing to install | `c++filt` (binutils; or any C++ compiler builds the helper) |
| fonts for a few menu pictures | Arial (already there); the converter looks in `C:\Windows\Fonts` | Nimbus Sans / DejaVu (`apt install fonts-urw-base35 fonts-dejavu fonts-noto-cjk`) |

The converters draw a few pictures' text (the versus selector's discs, the "?" of the special vehicles button, the save screen's
Chinese info page) with whatever of those fonts they find, in the folders named by `MT3DX_FONT_DIRS` and the system's. A different font
gives a slightly different look in those pictures only.

## 2. Make the data (once per game build)

```sh
python build.py data /path/to/dump          # = tools/pipeline.py gen --phys, then convert
```

- `gen` reads `main` and writes the tables (`src/*db.h`, `src/spriteids.h`, `tools/*.json`) and, with `--phys`, the recompiled physics
  (`src/phys/phys_rc.c`, `src/phys/rc_addr.h`) and `data/phys.img`. A few minutes.
- `convert` writes `data/`: the maps, cars, HUD, menus, sounds, music and movies. It takes an hour or more (the menu pictures are
  the long step), and every step logs to `build/logs/<step>.log`. `python tools/pipeline.py steps` lists the steps; `--only <step>` runs
  one, `--no-media` skips the sound, music and movies (the ones that need ffmpeg), `--keep-going` collects all the failures.
- The dump may be the Japan or the Export build (`docs/BUILDS.md`); the region's art folders (`sprite` / `sprite-us`, ...) are handled.
  The dump is only read.

## 3. Build the player

```sh
python build.py exe                         # Windows: build/windows/super3dx.exe;  Linux: build/linux/super3dx
python build.py exe --cc "zig cc -target x86_64-windows-gnu"     # pick the compiler yourself
python build.py exe -O -O1 -j 8             # quicker to compile; eight compiler processes
python build.py package                     # build/<target>/package/: the player, its DLL, data/, play.bat or play.sh
```

The first build compiles the physics (`phys_rc.c`, a minute or two); later ones only what changed. `play.bat` starts the game with its
data and saves next to it (`play.bat --size 1280x960` for a bigger window; the layout is 4:3).

Run it: double-click `super3dx.exe` (it takes `data/` and `save/` from beside itself, opens at 1280x960 and has the high-quality effects on; `play.bat` is optional), or from a shell: `build\windows\super3dx.exe [data] [--save save] [--size WxH] [--fullscreen]`. `MT3DX_HQ=0` turns the effects off (a slow GPU). Controls and remapping: `docs/INPUT.md`.

**AMD graphics, and screens of 90 Hz or more, on Windows start on ANGLE.** The player checks the graphics card's name once the window exists, and on an AMD / Radeon card starts itself again on the bundled ANGLE (`libEGL.dll` and `libGLESv2.dll` beside the exe: OpenGL ES over Direct3D 11) in place of AMD's own OpenGL ES, which showed lifted blacks (issue 15) and, on a Radeon RX 9070 XT, every other frame of a race from a stale buffer (issue 33). The log says so (`ANGLE chosen automatically`). A screen of 90 Hz or more (checked before the window is made) starts on ANGLE too, whatever the card: at 120 Hz the NVIDIA driver's own OpenGL ES let its frame queue run empty and fill again every 3 s, a hitch the game's smooth motion shows, and ANGLE's Direct3D 11 swap chain did not (the log says `the screen runs at N Hz: starting on ANGLE`). To keep the driver's own OpenGL ES instead:

- for one run: `super3dx.exe --native-gl` (or the variable `MT3DX_NATIVE_GL=1`);
- for good, without a command line: an empty file named `native-gl.txt` next to `super3dx.exe`;
- for a build that never switches: `set WMMT_NO_AUTO_ANGLE=1` before `python build.py exe`.

`super3dx.exe --angle` still forces ANGLE on any card. Without the two ANGLE files beside the exe nothing switches.

## Raspberry Pi 5

**The easy way:** `setup-pi.bat` (Windows) or `./setup-pi.sh`, after the main setup. Flash 64-bit Raspberry Pi OS Lite onto the card with Raspberry Pi Imager (in its
settings: set a user name and password, tick "Enable SSH"), start the Pi on your network, and run it. It asks for the Pi's address and user, installs an SSH key (the Pi's
password is asked once), installs the packages, builds the player on the Pi, sends the data (only when it changed), installs the service that starts the game at boot and, for
composite video, sets the Pi 5's video output (it keeps a `.super3dx.bak` of `config.txt` and `cmdline.txt`) and offers to restart. Run it again after an update: it only sends
what changed. Its log is `build/logs/setup-pi-<date>.txt`. It needs `ssh` on your PC (Windows 10/11: Settings > Optional features > OpenSSH Client). By hand, with the older
scripts and cross compiling:

On the Pi itself: install the dependencies as for Linux, then `python3 build.py data ...` is not needed there: build the data on a PC and copy
`data/` over (`tools/pi/install.sh` does it), and build the player on the Pi with `python3 build.py exe --target pi` (a few minutes).
From another machine: `tools/pi/sysroot.sh` makes an arm64 sysroot, then `SYSROOT=/opt/pi-sysroot python3 build.py exe --target pi`.
Preparing the card, the service and the composite (or HDMI) output: `docs/PI.md`.

## Android

**The easy way:** `setup-android.bat` (Windows) or `./setup-android.sh`, after the main setup. It finds a JDK 17+ and the Android SDK (build-tools 35, a platform and an NDK), or
downloads them into `.super3dx/` (about 3 GB; you accept Google's Android SDK licence terms first), builds one self-contained APK with the game's data inside
(`Super3DXPlus-<version>-Android.apk`, about 1 GB, Android 9+ on a 64-bit ARM device with Vulkan) and, if a phone with USB debugging is connected, installs it. Japan and Export
get different app ids (`com.wmmt.port`, `com.wmmt.port.export`), so both can be installed. The APK is signed with a key made on first use and kept in
`.super3dx/android/debug.keystore`: **keep it**, because Android only installs an update over an app signed with the same key (a different key means uninstalling first, which
deletes the app's saves). Its log is `build/logs/setup-android-<date>.txt`.

Everything on the Android device itself, with no PC (Termux): `docs/ANDROID_ON_DEVICE.md`.

By hand: `python platform/android/build_apk.py --sdk <Android SDK> --jdk <JDK> --out app.apk [--pkg ... --label ... --embed <data folder>]` (the same on every OS), or the older
`platform/android/build.sh` (bash; `tools/dev/setup_wsl.sh --android` installs its tools on Ubuntu), after `python build.py gensrc <your game folder>`;
`platform/android/deploy.ps1` installs over USB.

## Problems

- `missing src/phys/phys_rc.c`: run the data step first; the player cannot be built without the generated sources.
- `no SDL2 development files for mingw`: `python build.py setup-sdl2`, or point `SDL2_DIR` at `<...>/x86_64-w64-mingw32`.
- A converter step fails: read `build/logs/<step>.log`; `--only <step>` re-runs it after you fix the cause. A step that fails for a missing
  font or ffmpeg says so.
- Windows: if linking a mingw build complains about `winpthread`, use Zig or the `posix` thread model of MinGW (`x86_64-w64-mingw32-gcc-posix`).
