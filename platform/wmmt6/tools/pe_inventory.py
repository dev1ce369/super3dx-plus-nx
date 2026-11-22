#!/usr/bin/env python3
"""Reads PE headers/imports from a local arcade dump and writes a local JSON report.

This is a source-only research tool. It reads binary headers only: it does not
copy, modify, or publish any game file.

    python platform/wmmt6/tools/pe_inventory.py <dump folder> --out <report.json>
"""
import argparse
import json
import os
import struct
import sys

MACHINES = {0x14c: "i386", 0x8664: "x86-64", 0xaa64: "arm64"}
SUBSYSTEMS = {2: "gui", 3: "cui"}


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def cstr(b, o):
    end = b.find(b"\0", o)
    return b[o:end].decode("ascii", "replace")


def inspect(path, rel):
    with open(path, "rb") as f:
        b = f.read()
    if b[:2] != b"MZ" or len(b) < 0x40:
        return None
    pe = u32(b, 0x3c)
    if b[pe:pe + 4] != b"PE\0\0":
        return None
    coff = pe + 4
    machine = u16(b, coff)
    nsec = u16(b, coff + 2)
    opt_size = u16(b, coff + 16)
    opt = coff + 20
    magic = u16(b, opt)
    if magic not in (0x10b, 0x20b):
        return None
    plus = magic == 0x20b
    subsystem = u16(b, opt + 68)
    dd = opt + (112 if plus else 96)
    import_rva = u32(b, dd + 8)

    sections = []
    sec_off = opt + opt_size
    for i in range(nsec):
        s = sec_off + i * 40
        name = cstr(b, s).strip() or "?"
        sections.append({
            "name": name,
            "va": u32(b, s + 12),
            "vsize": u32(b, s + 8),
            "raw_ptr": u32(b, s + 20),
            "raw_size": u32(b, s + 16),
        })

    def rva2off(rva):
        for s in sections:
            if s["va"] <= rva < s["va"] + max(s["vsize"], s["raw_size"]):
                return s["raw_ptr"] + (rva - s["va"])
        return None

    imports = []
    if import_rva:
        off = rva2off(import_rva)
        i = 0
        while off is not None and i < 512:
            ent = off + i * 20
            name_rva = u32(b, ent + 12)
            if not name_rva:
                break
            no = rva2off(name_rva)
            if no is not None:
                imports.append(cstr(b, no))
            i += 1

    return {
        "path": rel.replace(os.sep, "/"),
        "size": len(b),
        "machine": MACHINES.get(machine, hex(machine)),
        "subsystem": SUBSYSTEMS.get(subsystem, str(subsystem)),
        "sections": [s["name"] for s in sections],
        "imports": imports,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump", help="local dump folder")
    ap.add_argument("--out", required=True, help="local JSON report path")
    a = ap.parse_args()

    rows = []
    for dp, _, fn in os.walk(a.dump):
        for name in fn:
            if not name.lower().endswith((".exe", ".dll")):
                continue
            full = os.path.join(dp, name)
            rel = os.path.relpath(full, a.dump)
            try:
                row = inspect(full, rel)
            except OSError:
                row = None
            if row:
                rows.append(row)

    rows.sort(key=lambda r: r["path"])
    report = {
        "source": os.path.abspath(a.dump),
        "pe_count": len(rows),
        "files": rows,
    }
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("wrote %s (%d PE files)" % (a.out, len(rows)))


if __name__ == "__main__":
    main()
