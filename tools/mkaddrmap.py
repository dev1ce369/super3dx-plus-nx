#!/usr/bin/env python3
"""Make tools/addrs/<name>.json: the addresses the tools use, translated from the Japan 3DX+ `main`
to another build's. Needs both files (once); the result is what ships (see wmmtaddr.py).

  python3 tools/mkaddrmap.py <japan main> <other main> <name>
"""
import glob
import hashlib
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from addrmap import AddrMap

HERE = os.path.dirname(os.path.abspath(__file__))
LIT = re.compile(r"\b0x0?([89][0-9A-Fa-f]{6})\b")


def literals():
    out = {}
    files = glob.glob(os.path.join(HERE, "**", "*.py"), recursive=True) + glob.glob(os.path.join(HERE, "**", "*.txt"), recursive=True)
    for f in files:
        if os.path.basename(f) in ("addrmap.py", "mkaddrmap.py", "wmmtaddr.py"):
            continue
        text = open(f, encoding="utf8", errors="replace").read()
        pats = [LIT]
        if os.path.basename(f) == "roots.txt":  # code that is not a symbol: bare hex, or start:end:name
            pats.append(re.compile(r"(?<![0-9A-Za-z_])([89][0-9a-f]{6})(?![0-9A-Za-z_])"))
        for pat in pats:
            for m in pat.finditer(text):
                v = int(m.group(1), 16)
                if 0x08048000 <= v < 0x0A000000:
                    out.setdefault(v, set()).add(os.path.relpath(f, HERE))
    # the port's C code: game state as RCA(98028e4), recompiled functions as f_898317c
    src = os.path.join(HERE, "..", "src")
    c_pats = (re.compile(r"\bRCA\(([0-9a-f]{7})\)"), re.compile(r"\bf_([0-9a-f]{7})\b"))
    for root, _, fs in os.walk(src):
        for f in fs:
            if f.endswith((".c", ".h")) and f not in ("phys_rc.c", "rc_addr.h"):
                text = open(os.path.join(root, f), encoding="utf8", errors="replace").read()
                for pat in c_pats:
                    for m in pat.finditer(text):
                        v = int(m.group(1), 16)
                        if 0x08048000 <= v < 0x0A000000:
                            out.setdefault(v, set()).add("src/" + f)
    # the image's ends (rc_runtime: main's rodata..bss)
    for v in (0x092f1b20, 0x0993a468):
        out.setdefault(v, set()).add("image")
    return out


def main():
    jp, other, name = sys.argv[1:4]
    m = AddrMap(jp, other)
    lits = literals()
    table, missed = {}, []
    for v in sorted(lits):
        t = m(v)
        if t is None:
            missed.append(v)
        elif t != v:
            table["0x%X" % v] = "0x%X" % t
    # hand-checked entries win over the automatic mapping (tools/addrs/<name>.overrides.json): addresses it cannot find,
    # or finds wrong, such as strings the two builds word differently. A null target: the build has nothing equivalent.
    ov_path = os.path.join(HERE, "addrs", name + ".overrides.json")
    if os.path.exists(ov_path):
        for k, v in json.load(open(ov_path)).items():
            if k.startswith("_"):
                continue
            va = int(k, 16)
            missed = [x for x in missed if x != va]
            if v[0] is None:
                table.pop("0x%X" % va, None)
            else:
                table["0x%X" % va] = "0x%X" % int(v[0], 16)
            print("  override 0x%X -> %s" % (va, "none" if v[0] is None else v[0]))
    os.makedirs(os.path.join(HERE, "addrs"), exist_ok=True)
    path = os.path.join(HERE, "addrs", name + ".json")
    lang = "jp"
    if os.path.exists(ov_path):
        lang = json.load(open(ov_path)).get("_lang", "jp")  # the build's text language (wmmtaddr.lang)
    json.dump({"md5": hashlib.md5(open(other, "rb").read()).hexdigest(), "lang": lang, "map": table}, open(path, "w"), indent=0)
    print("%s: %d of %d addresses translated -> %s" % (name, len(table), len(lits), path))
    for v in missed:
        print("  not translated: 0x%X (%s)" % (v, ", ".join(sorted(lits[v])[:3])))
    # where the bytes and the code's cross-references gave different answers, and no override settles it
    ov = set()
    if os.path.exists(ov_path):
        ov = {int(k, 16) for k in json.load(open(ov_path)) if not k.startswith("_")}
    for v, (t, x) in sorted(m.conflicts.items()):
        if v in lits and v not in ov:
            print("  CHECK 0x%X: bytes say 0x%X, code references say 0x%X (%s): decide from the data, then add an override" % (
                v, t, x, ", ".join(sorted(lits[v])[:2])))


if __name__ == "__main__":
    main()
