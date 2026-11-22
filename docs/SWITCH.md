# Super3DX+NX — Nintendo Switch port

`platform/switch/` builds `super3dx+nx.nro`, a Switch homebrew build of the same
game code as the Windows, Linux and Android players. It uses **devkitA64 +
libnx + switch-sdl2 (SDL2-for-Switch)** and reuses the normal SDL2 backend in
`platform/sdl2/`; there is no separate game engine in this folder.

> Status: **not yet tested on Switch hardware.** The Makefile tracks the current
> devkitPro packages (devkitA64, libnx, switch-sdl2, switch-mesa, switch-zlib);
> expect to tweak a flag or two the first time you run it on a console.

## 1. What you need

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

## 2. Build

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

## 3. Install on the SD card

Make a folder like any other Switch homebrew and put the NRO and the converted
data in it:

```text
sdmc:/switch/super3dx+nx/
├── super3dx+nx.nro
├── data/                 ← copy from Super3DXPlus-Export/data (≈1 GB)
└── save/                 ← created automatically on first launch
```

- Copy the whole converted `data/` folder from the PC build. The NRO looks for
  `sdmc:/switch/super3dx+nx/data/menu.bin`; if it is missing, the game writes
  the reason to `save/log.txt` and exits.
- Launch `super3dx+nx` from the Homebrew Menu. Press **HOME** to return to it.
- The `save/` folder holds cards, settings and the log. Copy it whenever you
  replace the NRO if you want to keep your progress.

Do **not** put the game data in the repository or in a published archive. The
NRO contains only our code.

## 4. Controls

- **Menus:** the arcade game's menus are touch screens. Touch the Switch screen
  directly — switch-sdl2 delivers touch as SDL finger events, and the SDL
  backend maps them into game taps (including letterboxing on the 16:9 screen).
- **Racing:** a Pro Controller or Joy-Con through SDL game-controller input.
  Steering/accelerate/brake, shift, camera, reset and transmission are the
  normal SDL bindings; the exact keys are in `docs/INPUT.md`.
- **Versus:** LAN play uses the same `net.c` code as the desktop player. Both
  machines must be on the same network and run the same build.

## 5. Known limits / things to check on real hardware

- The homebrew uses GLES2 through switch-mesa. If the screen stays black, check
  `save/log.txt`; the first thing to try is removing the `-lEGL -lGLESv2` from
  `LIBS` in `platform/switch/Makefile` if your switch-sdl2 already links them.
- Music and movies come from `platform/sdl2/bgm.c` and `movie.c`; they need the
  Ogg/Vorbis files that the converter puts in `data/`.
- No icon is supplied. Add a 256×256 `platform/switch/icon.jpg` if you want a
  custom Homebrew Menu tile; otherwise devkitPro's default icon is used.
- This build has not been signed for retail firmware; run it through the
  Homebrew Menu as usual.

## 6. Publishing a fork

Source and instructions only: keep the MIT license, `NOTICE` and credits, and
never commit the converted `data/`, the arcade dump, an APK or an NRO that
contains game data.
