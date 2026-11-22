# Switch backend (`super3dx+nx`)

This folder contains only the libnx entry point and the devkitPro makefile. The
game code is the shared `../../src/`, and the window/audio/input backend is
`../../platform/sdl2/` (SDL2-for-Switch).

- Build: `make -C platform/switch` (requires devkitPro, `switch-sdl2`, `switch-mesa`)
- Full instructions and SD-card layout: [`docs/SWITCH.md`](../../docs/SWITCH.md)
- Output: `super3dx+nx.nro`, installed as `sdmc:/switch/super3dx+nx/super3dx+nx.nro`

No game data is stored here; copy the converted `data/` folder to the SD card.
