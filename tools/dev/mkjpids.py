#!/usr/bin/env python3
"""Make tools/jpids.json: for each sprite id that hand-written code uses as the Japan build has it (JID(n) in src/*.c,
J(...) in the converters), the picture's name. Needs the Japan 3DX+ `main` (once, when a new id is written into the code); the
result ships, and is all the export build's tools need to turn those ids into its own (tools/sprids.py).

  python3 tools/dev/mkjpids.py <Japan main> [--python]

--python also imports the converters that call J(), with sprids recording the ids they ask for (they need the generated
sources: run tools/gen_from_original.sh first, and set WMMT_MAIN to the Japan main).
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..")
sys.path.insert(0, TOOLS)
main = sys.argv[1]
os.environ["WMMT_MAIN"] = main
import sprids  # noqa: E402

names, _ = sprids.table(main)
ids = set()
src = os.path.join(TOOLS, "..", "src")
for f in os.listdir(src):
    if f.endswith((".c", ".h")) and f != "spriteids.h":
        ids |= {int(m.group(1), 0) for m in sprids.JID_RE.finditer(open(os.path.join(src, f), encoding="utf8", errors="replace").read())}
for f in os.listdir(TOOLS):  # J(0x120f) written straight in a converter
    if f.endswith(".py") and f != "sprids.py":
        ids |= {int(m.group(1), 0) for m in re.finditer(r"\bJ\(\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*\)", open(os.path.join(TOOLS, f), encoding="utf8").read())}
if "--python" in sys.argv:
    sprids.RECORD = set()
    sprids._jp = {i: "?" for i in range(sprids.TABLE_N)}  # (no names needed to record)
    sprids.table = lambda m=None: ({}, {"?": 0})
    for mod in ("menupack", "hudpack"):
        __import__(mod)
    ids |= sprids.RECORD
out = {}
for i in sorted(ids):
    if i not in names:
        print("no picture at Japan id %d" % i, file=sys.stderr)
        continue
    out[str(i)] = names[i]
json.dump(out, open(os.path.join(TOOLS, "jpids.json"), "w"), indent=0, sort_keys=False)
print("tools/jpids.json: %d ids" % len(out))
