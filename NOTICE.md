# Notices

## Based on Super3DX+ and Re3DX+

This repository is a continuation of **[Super3DXPlus](https://github.com/smolderyguest/Super3DXPlus)** by
**[smolderyguest](https://github.com/smolderyguest)**. The original PC, Android and Raspberry Pi port, its
build tooling, its documentation and the design of the player come from that project. Thank you to smolderyguest
for the original work. This repository keeps the original MIT notice where applicable and adds the Nintendo
Switch port, the READY-button fix, libnx socket initialization and diagnostics, and matching Android/NRO icons.

That project in turn builds on **Re3DX+**, an unofficial port of the arcade game to old iOS and Android devices, by
**高橋涼介のFC** (GitHub: [Allannn-sudo](https://github.com/Allannn-sudo)), released under the MIT licence
(see `LICENSE`, which keeps the original copyright line).

Re3DX+ showed that the arcade game's car physics can be statically recompiled from its i386 executable to C, and it
did the large work of rewriting the rendering, menus, HUD, sound and the asset converters from reverse engineering. The
code under `src/`, `tools/` and `test/` started as that work; this project adds to it (support for more than one build of
the game, a build-independent layout, Linux tooling and a Raspberry Pi port). The original project's own READMEs
are kept in `docs/upstream/`. Thank you to its author.

## No game content here

This repository contains code and tools only. It does **not** contain the arcade game's executable, data, or anything
generated from them (tables, converted art, sound, movies, the recompiled physics). You build those yourself from
your own legally obtained copy of the game, and `.gitignore` is set up to keep them out of the repository. Please do not
upload any of it in issues or pull requests.

## Unofficial

This is an unofficial, independent research and preservation project. It is not a version of the game released by
Bandai Namco Entertainment. *Wangan Midnight* and *Maximum Tune* and the names, trademarks, characters, art, music,
sound effects, vehicle brands and other protected content in them belong to their respective owners. This project
claims no ownership of any of it.
