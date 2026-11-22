#!/usr/bin/env python3
"""Builds a local disassembly report for exported PE functions using capstone.

Source-only research tool: it reads a local PE file and writes generated output
only to an ignored local work folder. It never copies or publishes game data.

    python platform/wmmt6/tools/pe_exports_disasm.py <file.dll> --out <folder>
"""
import argparse
import json
import os
import re
import struct

from capstone import CS_ARCH_X86, CS_MODE_64, Cs
from capstone.x86 import X86_INS_RET

MACHINES = {0x14c: "i386", 0x8664: "x86-64"}


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def cstr(b, o):
    end = b.find(b"\0", o)
    return b[o:end].decode("ascii", "replace")


class PE:
    def __init__(self, path):
        self.path = path
        self.b = open(path, "rb").read()
        if self.b[:2] != b"MZ":
            raise ValueError("not a PE file")
        self.pe = u32(self.b, 0x3c)
        if self.b[self.pe:self.pe + 4] != b"PE\0\0":
            raise ValueError("bad PE signature")
        coff = self.pe + 4
        self.machine = u16(self.b, coff)
        nsec = u16(self.b, coff + 2)
        opt_size = u16(self.b, coff + 16)
        opt = coff + 20
        magic = u16(self.b, opt)
        if magic not in (0x10b, 0x20b):
            raise ValueError("unknown optional header")
        plus = magic == 0x20b
        dd = opt + (112 if plus else 96)
        self.export_rva = u32(self.b, dd)
        self.export_size = u32(self.b, dd + 4)
        self.entry_rva = u32(self.b, opt + 16)
        self.sections = []
        sec_off = opt + opt_size
        for i in range(nsec):
            s = sec_off + i * 40
            self.sections.append({
                "name": cstr(self.b, s).strip() or "?",
                "vsize": u32(self.b, s + 8),
                "va": u32(self.b, s + 12),
                "raw_ptr": u32(self.b, s + 20),
                "raw_size": u32(self.b, s + 16),
            })

    def rva2off(self, rva):
        for s in self.sections:
            if s["va"] <= rva < s["va"] + max(s["vsize"], s["raw_size"]):
                return s["raw_ptr"] + (rva - s["va"])
        return None

    def data_at_rva(self, rva, size):
        off = self.rva2off(rva)
        if off is None:
            return b""
        return self.b[off:off + size]

    def exports(self):
        if not self.export_rva:
            return []
        off = self.rva2off(self.export_rva)
        if off is None:
            return []
        b = self.b
        base = u32(b, off + 16)
        nfunc = u32(b, off + 20)
        nname = u32(b, off + 24)
        afunc = u32(b, off + 28)
        aname = u32(b, off + 32)
        aord = u32(b, off + 36)
        funcs = []
        fo = self.rva2off(afunc)
        if fo is None:
            return []
        for i in range(nfunc):
            funcs.append(u32(b, fo + i * 4))
        names = []
        no = self.rva2off(aname)
        oo = self.rva2off(aord)
        if no is None or oo is None:
            return []
        for i in range(nname):
            name_rva = u32(b, no + i * 4)
            ordinal = u16(b, oo + i * 2)
            if ordinal < len(funcs) and funcs[ordinal]:
                name_off = self.rva2off(name_rva)
                name = cstr(b, name_off) if name_off is not None else "ord%d" % (base + ordinal)
                names.append((name, funcs[ordinal], base + ordinal))
        return names


def disasm_range(pe, rva, max_bytes=1024):
    off = pe.rva2off(rva)
    if off is None:
        return [], 0
    code = pe.b[off:off + max_bytes]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    out = []
    count = 0
    for ins in md.disasm(code, rva):
        out.append("0x%08x  %-10s %s" % (ins.address, ins.mnemonic, ins.op_str))
        count += ins.size
        if ins.id == X86_INS_RET:
            break
    return out, count


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", help="local PE file to inspect")
    ap.add_argument("--out", required=True, help="local ignored output folder")
    ap.add_argument("--max-bytes", type=int, default=1024)
    a = ap.parse_args()

    pe = PE(a.file)
    base = os.path.basename(a.file)
    stem = os.path.splitext(base)[0]
    outdir = os.path.join(a.out, stem)
    os.makedirs(outdir, exist_ok=True)

    exports = pe.exports()
    entries = []
    for name, rva, ordinal in exports:
        lines, used = disasm_range(pe, rva, a.max_bytes)
        safe = re.sub(r"[^A-Za-z0-9_.-]", "_", name)[:120]
        path = os.path.join(outdir, "%s.asm.txt" % safe)
        with open(path, "w", encoding="utf-8") as f:
            f.write("; %s  ordinal=%d rva=0x%x bytes=%d\n" % (name, ordinal, rva, used))
            f.write("\n".join(lines))
            f.write("\n")
        entries.append({"name": name, "ordinal": ordinal, "rva": hex(rva), "bytes": used, "file": os.path.basename(path)})

    entry_lines, entry_used = disasm_range(pe, pe.entry_rva, a.max_bytes)
    with open(os.path.join(outdir, "entry.asm.txt"), "w", encoding="utf-8") as f:
        f.write("; entry rva=0x%x bytes=%d\n" % (pe.entry_rva, entry_used))
        f.write("\n".join(entry_lines))
        f.write("\n")

    report = {
        "source": os.path.abspath(a.file),
        "machine": MACHINES.get(pe.machine, hex(pe.machine)),
        "export_count": len(exports),
        "exports": entries,
        "entry_rva": hex(pe.entry_rva),
    }
    with open(os.path.join(outdir, "report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("%s: machine=%s exports=%d -> %s" % (base, report["machine"], len(exports), outdir))


if __name__ == "__main__":
    main()
