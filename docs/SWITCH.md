# Super3DX+NX — Nintendo Switch port

`platform/switch/` builds `super3dx+nx.nro`, a Switch homebrew build of the same
game code as the Windows, Linux and Android players. It uses **devkitA64 +
libnx + switch-sdl2 (SDL2-for-Switch)** and reuses the normal SDL2 backend in
`platform/sdl2/`; there is no separate game engine in this folder.

> Status: **tested on Switch hardware.** The Makefile tracks the current
> devkitPro packages (devkitA64, libnx, switch-sdl2, switch-mesa, switch-zlib).

## 1. Download the NRO (no build needed)

If you just want to play, download `super3dx+nx.nro` from
[GitHub Releases](https://github.com/dev1ce369/super3dx-plus-nx/releases/latest).
It is only the homebrew executable — it contains **no game data**. You must make
the converted `data/` folder yourself from your own legally obtained dump; see
[Section 4](#4-install-on-the-sd-card) for the exact layout.

## 2. What you need to build it yourself

Build on Linux, macOS or Windows with devkitPro's MSYS2 shell (not plain
PowerShell):

1. Install devkitPro with **devkitA64** and **libnx**:
   <https://devkitpro.org/wiki/Getting_Started/devkitPro>
2. In the MSYS2 shell, install the Switch toolchain and the SDL2/GLES2/zlib ports:

   ```bash
   pacman -S switch-dev switch-sdl2 switch-mesa switch-zlib
   ```

   If you prefer not to run the elevated Windows installer, you can extract the
   `msys-<version>.7z` named in `https://downloads.devkitpro.org/devkitProUpdate.ini`
   into `C:\devkitPro`, replace `#{DEVKITPRO}` and `#{PROFILES_ROOT}` in
   `msys2/etc/fstab` with `C:/devkitPro` and your Windows profile directory, then
   run `pacman -Syu` from that MSYS2 shell before the command above.

3. Export `DEVKITPRO` (the devkitPro installer normally does this in its shell):

   ```bash
   export DEVKITPRO=/opt/devkitpro      # Linux/macOS; MSYS2 uses /c/devkitPro
   ```

4. Generate the game-derived tables and physics from **your own** dump first,
   on any machine that has already run the normal setup:

   ```bash
   python build.py refresh "<your 3DX+ dump folder>"
   ```

   The `make` build needs the generated `src/phys/phys_rc.c`, `src/cardb.h`,
   etc. It deliberately does not embed any game data in the NRO.

## 3. Build

```bash
make -C platform/switch
# or, from the project root:
python build.py switch
```

On Windows, `python build.py switch` launches devkitPro's MSYS2 shell itself
(`C:\devkitPro\msys2\usr\bin\bash.exe` by default), so `make` does not have to be
on your normal PATH. Set `DEVKITPRO` to your install folder if you used another
location.

The result is:

```text
platform/switch/super3dx+nx.nro
```

The build target name (`TARGET`) and the installed folder are both
`super3dx+nx`, so the file keeps its name when copied into the homebrew folder.

## 4. Install on the SD card

### Step A — make the converted data on a PC

The NRO does **not** contain the game. You first need the converted `data/`
folder, made from your own legally obtained dump (a folder containing `main`
and `data/`):

```bash
python build.py data "<your 3DX+ dump folder>"
# or, if you already converted it before:
python build.py refresh "<your 3DX+ dump folder>"
```

That creates a `data/` folder in the project root (`menu.bin` must be inside it,
about 2.95 GB / 1,890 files for the Export build). This is the folder that goes
on the Switch — **not** the raw dump's `data/` folder and not the `main` file.

### Step B — copy this exact layout to the SD card

```text
sdmc:/switch/
└── super3dx+nx/
    ├── super3dx+nx.nro       ← the downloaded NRO
    ├── data/                 ← the whole converted data folder from the PC
    │   ├── menu.bin
    │   ├── cars/
    │   ├── sound/
    │   ├── ...
    └── save/                 ← created automatically on first launch
```

The NRO and `data/` must be **siblings directly inside** `super3dx+nx/`. Do not
nest another folder inside, and do not rename the NRO or the `data` folder. The
game looks for `sdmc:/switch/super3dx+nx/data/menu.bin`; if it is missing, it
writes the reason to `save/log.txt` and exits.

### Step C — launch

- Put the SD card back in the Switch and launch **super3dx+nx** from the
  Homebrew Menu.
- Menus use touch; racing uses a Pro Controller or Joy-Con through SDL.
- Press **HOME** to return to the Homebrew Menu.
- `save/` holds cards, settings and the log. Back it up before replacing the
  NRO if you want to keep progress.

Never upload your dump, converted `data/`, or `save/` folder. The released NRO
contains only this project's code, so it can be shared on its own.

## 5. Controls

- **Menus:** the arcade game's menus are touch screens. Touch the Switch screen
  directly — switch-sdl2 delivers touch as SDL finger events, and the SDL
  backend maps them into game taps (including letterboxing on the 16:9 screen).
- **Racing:** a Pro Controller or Joy-Con through SDL game-controller input.
  Steering/accelerate/brake, shift, camera, reset and transmission are the
  normal SDL bindings; the exact keys are in `docs/INPUT.md`.
- **Versus:** LAN play uses the same `net.c` code as the desktop player. Both
  machines must be on the same network and run the same build.

## 6. Known limits / things to check on real hardware

- The homebrew uses GLES2 through switch-mesa. If the screen stays black, check
  `save/log.txt`; the first thing to try is removing the `-lEGL -lGLESv2` from
  `LIBS` in `platform/switch/Makefile` if your switch-sdl2 already links them.
- Music and movies come from `platform/sdl2/bgm.c` and `movie.c`; they need the
  Ogg/Vorbis files that the converter puts in `data/`.
- The Release NRO has a custom 256×256 icon. Source checkouts do not include
  that artwork: add your own `platform/switch/icon.jpg` if you want a custom
  Homebrew Menu tile; otherwise devkitPro's default icon is used.
- This build has not been signed for retail firmware; run it through the
  Homebrew Menu as usual.

## 7. Publishing a fork

Keep the source tree source-only: keep the MIT license, `NOTICE` and credits,
and never commit the converted `data/`, the arcade dump, generated game-derived
sources, an APK, or any binary containing game data. A separately built NRO that
contains only this project's code can be attached to a GitHub Release as a
convenience, with the install steps above and a clear note that users must
supply their own converted data.
