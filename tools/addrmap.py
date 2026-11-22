#!/usr/bin/env python3
"""Translate virtual addresses of the Japan 3DX+ arcade `main` to another build's `main`.

The tools hard-code Japan addresses. Both builds export ~60k named dynamic symbols, so a symbol
present in both is an exact (japan, other) address pair. An address in between is translated by
the nearest symbol below it (code and data are only shifted by inserted code and data between
symbols), and the result is checked against the bytes at both places.

  python3 addrmap.py <japan main> <other main> [addr ...]   print translations and a summary
  from addrmap import AddrMap; m = AddrMap(jp, other); m(0x9417720)
"""
import bisect
import struct
import sys


class Elf:
    def __init__(self, path):
        self.b = open(path, "rb").read()
        b = self.b
        shoff, = struct.unpack_from("<I", b, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", b, 0x2E)
        self.secs = []
        for i in range(shnum):
            name, typ, flags, addr, off, size, link, info, al, es = struct.unpack_from("<10I", b, shoff + i * shentsize)
            self.secs.append([name, typ, flags, addr, off, size, link, info])
        strtab = self.secs[shstrndx]
        for s in self.secs:
            e = b.index(b"\0", strtab[4] + s[0])
            s[0] = b[strtab[4] + s[0]:e].decode()
        self.by_name = {s[0]: s for s in self.secs}

    def sec_of(self, va):
        for s in self.secs:
            if s[3] and s[3] <= va < s[3] + s[5] and s[2] & 2:
                return s
        return None

    def read(self, va, n):
        s = self.sec_of(va)
        if s is None or s[1] == 8:
            return None
        o = s[4] + va - s[3]
        return self.b[o:o + n]

    def plt_names(self):
        """The imported function each 16-byte stub of .plt jumps to, in stub order (from .rel.plt)."""
        rp, ds, st = self.by_name[".rel.plt"], self.by_name[".dynsym"], self.by_name[".dynstr"]
        out = []
        for i in range(rp[5] // 8):
            _, info = struct.unpack_from("<II", self.b, rp[4] + 8 * i)
            nm, = struct.unpack_from("<I", self.b, ds[4] + 16 * (info >> 8))
            e = self.b.index(b"\0", st[4] + nm)
            out.append(self.b[st[4] + nm:e].decode())
        return out

    def symbols(self):
        ds, st = self.by_name[".dynsym"], self.by_name[".dynstr"]
        out = {}
        for i in range(ds[5] // 16):
            nm, val, size, info, other, shndx = struct.unpack_from("<IIIBBH", self.b, ds[4] + 16 * i)
            if not val or not shndx or (info & 0xF) not in (1, 2):  # OBJECT, FUNC
                continue
            e = self.b.index(b"\0", st[4] + nm)
            out[self.b[st[4] + nm:e].decode()] = (val, size)
        return out


class AddrMap:
    def __init__(self, jp_path, other_path):
        self.jp, self.ot = Elf(jp_path), Elf(other_path)
        a, b = self.jp.symbols(), self.ot.symbols()
        pairs = sorted((a[n][0], b[n][0]) for n in a if n in b)
        self.js = [p[0] for p in pairs]
        self.deltas = [p[1] - p[0] for p in pairs]
        self.nsym = len(pairs)
        self.jplt, self.otplt = self.jp.by_name[".plt"], self.ot.by_name[".plt"]
        self.jpnames = self.jp.plt_names()
        self.otslot = {n: i for i, n in enumerate(self.ot.plt_names())}
        self.conflicts = {}  # va -> (the bytes' answer, the cross-references' answer), where they differ

    def __call__(self, va, check=64):
        """The other build's address for Japan's `va`, or None when it can't be told. The bytes decide; read-only data
        the bytes cannot place (text in another language has none to match) is found by its cross-references. Where both
        answer and disagree the bytes win and the pair is kept in self.conflicts, for a person to settle (the data
        itself can show which is right: tools/addrs/<name>.overrides.json). Cross-references alone were wrong for a
        table of plain integers, and the bytes alone are wrong for a string worded differently."""
        t = self._translate(va, check)
        jsec = self.jp.sec_of(va)
        if jsec is not None and jsec[0] == ".rodata":
            x = self._xref(va)
            if t is None:
                return x
            if x is not None and x != t:
                self.conflicts[va] = (t, x)
        return t

    def _xref(self, va):
        """Find the instructions of the Japan code that load `va` (a 4-byte operand), the same instructions in the
        other build's code (code translates well), and read the address they load there. The surrounding opcode
        bytes must agree, and so must most of the instructions."""
        jt, et = self.jp.by_name[".text"], self.ot.by_name[".text"]
        jdata = self.jp.b[jt[4]:jt[4] + jt[5]]
        pat = struct.pack("<I", va)
        votes, pos, n = {}, jdata.find(pat), 0
        while pos >= 6 and n < 60:
            n += 1
            ja = jt[3] + pos - 6  # six bytes of the instruction before the operand
            ea = self._translate(ja, 16)
            if ea is not None:
                cj, ce, val = self.jp.read(ja, 6), self.ot.read(ea, 6), self.ot.read(ea + 6, 4)
                if cj and ce and val and ce[-3:] == cj[-3:]:
                    t = struct.unpack("<I", val)[0]
                    if self.ot.sec_of(t) is not None:
                        votes[t] = votes.get(t, 0) + 1
            pos = jdata.find(pat, pos + 1)
        if not votes:
            return None
        best = max(votes, key=votes.get)
        return best if votes[best] * 2 > sum(votes.values()) else None

    def _translate(self, va, check=64):
        """The other build's address for Japan's `va`, or None when it can't be told."""
        if va < 0x08048134 or va >= 0x0A000000:  # the image base and beyond: the same in both
            return va
        jsec = self.jp.sec_of(va)
        if jsec is None:
            # one past the end of a section (a range's upper bound): the other build's section end
            for s in self.jp.secs:
                if s[3] and s[2] & 2 and s[3] + s[5] == va and s[0] in self.ot.by_name:
                    o = self.ot.by_name[s[0]]
                    return o[3] + o[5]
            return None
        osec = self.ot.by_name.get(jsec[0])
        if jsec is self.jplt:  # a stub of the import table: the same imported function's stub
            idx, off = divmod(va - jsec[3], 16)
            if idx == 0:
                return osec[3] + off
            slot = self.otslot.get(self.jpnames[idx - 1]) if idx - 1 < len(self.jpnames) else None
            return None if slot is None else osec[3] + 16 * (slot + 1) + off
        i = bisect.bisect_right(self.js, va) - 1
        lo = hi = None  # the shift at the nearest symbol below and above, in the same section
        if i >= 0 and self.jp.sec_of(self.js[i]) is jsec:
            lo = self.deltas[i]
        if i + 1 < len(self.js) and self.jp.sec_of(self.js[i + 1]) is jsec:
            hi = self.deltas[i + 1]
        if lo is not None and lo == hi:
            return va + lo  # nothing was inserted between the two symbols
        cands = [d for d in (lo, hi) if d is not None]
        if osec is not None:
            cands.append(osec[3] - jsec[3])
        if lo is not None and hi is not None and abs(hi - lo) <= 8192:
            # something was inserted (or cut) between the two symbols, maybe in several places:
            # any shift in between may be this address's
            cands += range(min(lo, hi), max(lo, hi) + 1)
        cands = list(dict.fromkeys(cands))
        code = jsec[2] & 4  # SHF_EXECINSTR: compare bytes, which don't fall on word boundaries
        if code:
            check = 16  # a function's start: past it, the next function may be padded differently
        jb = self.jp.read(va, check)
        if jb is None:  # .bss: nothing to compare
            return va + cands[0] if cands else None
        best, score = None, -1.0
        for d in cands:
            ob = self.ot.read(va + d, check)
            if ob is None:
                continue
            s = sum(x == y for x, y in zip(jb, ob)) / len(jb) if code else self._score(jb, ob)
            if s > score:
                best, score = d, s
        return va + best if best is not None and score >= 0.6 else None

    @staticmethod
    def _score(a, b):
        """Share of equal 4-byte words, not counting the words that are addresses of the image on
        both sides (those move between builds) and counting small drifts as equal. Tables of
        nothing but addresses score 1."""
        n = min(len(a), len(b)) // 4
        eq = tot = 0
        for k in range(n):
            x, y = struct.unpack_from("<II", a[4 * k:4 * k + 4] + b[4 * k:4 * k + 4])
            if x != y and 0x08048000 <= x < 0x0A000000 and 0x08048000 <= y < 0x0A000000:
                continue
            tot += 1
            # tables of offsets into a string pool drift a little between builds
            eq += x == y or (x and y and abs(x - y) <= 0x200)
        return eq / tot if tot else 1.0


def main():
    m = AddrMap(sys.argv[1], sys.argv[2])
    print("%d symbols in common" % m.nsym)
    ds = {}
    for d in m.deltas:
        ds[d] = ds.get(d, 0) + 1
    print("distinct deltas: %d; most common: %s" % (len(ds), sorted(ds.items(), key=lambda x: -x[1])[:6]))
    for a in sys.argv[3:]:
        va = int(a, 0)
        t = m(va)
        print("0x%X -> %s" % (va, "?" if t is None else "0x%X (delta %+d)" % (t, t - va)))


if __name__ == "__main__":
    main()
