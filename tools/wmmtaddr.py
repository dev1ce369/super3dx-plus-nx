"""Addresses of the arcade `main` for the build being read.

The tools were written against the Japan 3DX+ `main` and hard-code its virtual addresses. A(va)
turns such an address into the one of the build named by $WMMT_MAIN: tools/addrs/*.json hold, per
known build, the translation of every address the tools use (made by tools/mkaddrmap.py from the
two files). An unknown build, or the Japan one, gets the address back as it is.
"""
import glob
import hashlib
import json
import os

_table = None
_lang = "jp"


def _load():
    global _table, _lang
    _table = {}
    main = os.environ.get("WMMT_MAIN")
    if not main or not os.path.isfile(main):
        return
    h = hashlib.md5(open(main, "rb").read()).hexdigest()
    for f in sorted(glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)), "addrs", "*.json"))):
        if f.endswith(".overrides.json"):  # hand-checked input to tools/mkaddrmap.py, not a table
            continue
        d = json.load(open(f))
        if d.get("md5") == h and "map" in d:
            _table = {int(k, 16): int(v, 16) for k, v in d["map"].items()}
            _lang = d.get("lang", "jp")
            return


def A(va):
    if _table is None:
        _load()
    return _table.get(va, va)


def lang():
    """The language of the build's own text: "en" for the export build, "jp". The arcade holds both languages in many tables (the
    titles: +0x14 Japanese, +0x18 English) and picks by its region; so do the tools (tools/addrs/<name>.json "lang")."""
    if _table is None:
        _load()
    return _lang
