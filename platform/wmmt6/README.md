# WMMT6R native PC source-port research

This directory is the start of a **native PC source-port** investigation for a
modern WMMT6-series arcade dump. It is **not** a playable port, and it does
**not** use an emulator at runtime.

It is a separate project from Super3DX+. The 3DX+ recompiler works because that
game is one old i386 `main` with a fixed ABI. A WMMT6 dump is a modern x86-64
Windows/D3D11 game with many DLLs, scripts, activation/auth components, and
network code. Nothing here can reuse the 3DX `tools/recomp` pipeline.

## Rules

- Never commit the dump, converted assets, saves, or generated game-derived source.
- Never publish a binary that contains game data.
- Keep all local analysis output in `.super3dx/wmmt6/`, which is git-ignored.
- Only generic tools/scripts and documentation live in this directory.
- Credit the original projects and rights holders; this is unofficial, non-commercial research.

## What is known so far

The dump inspected locally contains an x86-64 Windows GUI executable plus
support DLLs. It uses D3D11/DXGI, Media Foundation, WinSock, MSVC 2010 runtime,
Lua 5.1, Squirrel `.nut` scripts, and activation/auth DLLs. The data set is
about 13.7 GB across 25,000 files.

That means a native port would require, at minimum:

1. Mapping the executable and DLLs: roles, imports, exports, and entry points.
2. Decompiling selected modules to C-like source locally.
3. Reimplementing the Windows/D3D11/audio/input APIs used by the game.
4. Replacing or removing activation, card, and network dependencies.
5. Recreating the asset loader/format readers from the local dump.
6. Rewriting/replacing the game logic in portable C/C++.

Steps 1–2 are automatable only partially. Steps 3–6 are manual software
engineering and are the reason this is research, not a quick conversion.

## Local work area

All generated data stays outside git:

```text
.super3dx/wmmt6/
```

Suggested layout:

```text
.super3dx/wmmt6/
├── pe_inventory.json
├── ghidra/
│   └── wmn6r.gpr/
├── decomp/
│   └── <generated C, never commit>
└── notes/
```

## Toolchain

Available here:

- Python 3 with `capstone` and `unicorn`
- JDK 17 at `C:\Program Files\Microsoft\jdk-17.0.21.21-hotspot`

Recommended for real decompilation:

- Ghidra 11.1.2 (works with JDK 17) installed under `.super3dx/tools/ghidra/`

Ghidra 12.x requires a newer JDK, so 11.1.2 is the compatible choice with the
existing JDK 17.

## Step 1 — PE inventory (no game data published)

`tools/pe_inventory.py` reads only PE headers and import tables. It writes a
local JSON report:

```powershell
python platform\wmmt6\tools\pe_inventory.py `
  "C:\path\to\dump" `
  --out ".super3dx\wmmt6\pe_inventory.json"
```

This reports architecture, subsystem, sections, and imported DLL names. It
does not copy or modify any game file.

## Step 2 — local decompilation proof-of-concept

After Ghidra is available, import a small support DLL first, not the main
executable. The goal is to measure decompilation quality and identify which
APIs need manual reimplementation. All decompiled C stays in
`.super3dx/wmmt6/decomp/` and must never be committed.

## Status

- [x] Local dump inventory and PE architecture check
- [ ] Agree on/install Ghidra 11.1.2 locally
- [ ] PE inventory report written to local ignored folder
- [ ] Small-DLL decompilation proof-of-concept
- [ ] Main executable function/map report
- [ ] Decision on whether a native rewrite is tractable

No playable binary, APK, or NRO is planned from this work yet.
