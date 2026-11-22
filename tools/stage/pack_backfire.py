"""The cars' backfire (clBackFireManager), for src/backfiredb.h.

clBackFireManager (ctor A(0x85feaf8)) puts a data/effect/car_backfire.igb at each of the body
model's MF_NULL_R1, MF_NULL_R2, MF_NULL_L1, MF_NULL_L2 (A(0x93db7ef).., A(0x93d9c3c)) it has; the
"B" fires (+0x14) for the cars 0x2a, 0x2b, 0x34. setup(type) (A(0x85fe8b8)) starts the idle ones
with group SAMLL_A or MIDDLE_A by the type (B: SMALL_B, MIDDLE_B; the table A(0x93d9c00) of names
and last frames, 16 each), update (A(0x85fe694)) puts each at the null's position (no turn of its
own), half size, under the car's matrix, its animation at frame * 10 ms, until the frame is past
16. The IGB's groups: quads and cones whose named transforms scale (and turn) them by keys, under
an unnamed transform whose keyed translation steps the texture by cells (a flipbook: 0.25 across,
0.125 down); drawn SRC_ALPHA, ONE.

Writes src/backfiredb.h: per group (0..5 as the table) and frame (0..16) its triangles (x y z
u v, the texture), and per CARDB_PACKS pack and body (0 stock, 1..3 aero A..C) the nulls'
positions in the car's space and the B flag.

usage: pack_backfire.py <arcade root> <out.h>
"""
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..'))
from wmmtaddr import A  # the arcade addresses of the build being read
import json
import os
import re
import struct
import sys

import numpy as np

import pack_battle as pb
import scene
from igb import IGB

FILE = "data/effect/car_backfire.igb"
GROUPS = ["SAMLL_A", "MIDDLE_A", "LARGE_A", "SMALL_B", "MIDDLE_B", "LARGE_B"]
FRAMES = 17
NULLS = ["MF_NULL_R1", "MF_NULL_R2", "MF_NULL_L1", "MF_NULL_L2"]
B_CARS = (0x2a, 0x2b, 0x34)
BODIES = ["normal", "aeroA", "aeroB", "aeroC"]
HERE = os.path.dirname(os.path.abspath(__file__))


def name(f, o):
    return f.string(o[2]) if o[2] is not None and o[2].type == "String" else ""


class Seq:
    """an igTransform's own matrix by its keys (this file's: the sequence at slot 10)"""

    def __init__(self, f, o):
        self.static = np.frombuffer(o[8].raw, "<f4").reshape(4, 4).astype(np.float64)
        sq = f.ref(o[10]) if o[10] is not None else None
        self.seq = None
        if sq is not None and sq.type.startswith("igTransformSequence"):
            t = pb.lst(f, f.ref(sq[11]), "q", 8)
            self.seq = (t[:, 0] / 1e9 if t is not None else None, pb.lst(f, f.ref(sq[2]), "3f", 12),
                        pb.lst(f, f.ref(sq[3]), "4f", 16), pb.lst(f, f.ref(sq[4]), "3f", 12))

    def local(self, t):
        n = pb.Node.__new__(pb.Node)
        n.static, n.seq = self.static, self.seq
        return pb.Node.local(n, t)


def effect(root):
    path = os.path.join(root, FILE)
    f = IGB(path)
    images = [o.index for o in f.of_type("igImage")]
    groups = {g: [] for g in GROUPS}

    def walk(node, group, chain, image, inner):
        if node is None or node.mem is not None:
            return
        if node.type == "igGroup" and name(f, node) in groups:
            group = name(f, node)
        if node.type == "igTransform":
            if name(f, node):
                chain = chain + [Seq(f, node)]
            else:
                inner = Seq(f, node)
        attrs = scene.obj_list(f, f.ref(node[8])) if node.type in ("igAttrSet", "igGeometry") else []
        img = scene.texture_image(f, attrs)
        if img is not None:
            image = img
        if node.type == "igGeometry" and group:
            for a in attrs:
                if a.type.startswith("igGeometryAttr"):
                    va = f.ref(a[4])
                    v = scene.decode_vertices_1_1(f, va) if va.type == "igVertexArray1_1" else scene.decode_vertices(f, va)
                    tris = scene.decode_indices(f, a, len(v["pos"]))
                    groups[group].append((chain, inner, images.index(image.index), np.array(v["pos"], np.float64),
                                          np.array(v["uv"], np.float64), tris))
        for child in scene.obj_list(f, f.ref(node[7])):
            walk(child, group, chain, image, inner)

    walk(f.ref(f.of_type("igSceneInfo")[0][5]), None, [], None, None)
    used = sorted({m[2] for g in groups.values() for m in g})
    out = []
    for g in GROUPS:
        frames = []
        for fr in range(FRAMES):
            t = fr * 0.01  # clModelObject::setAnimFrame(frame * 1e7 ns)
            tris = []
            for chain, inner, img, pos, uv, idx in groups[g]:
                m = np.eye(4)
                for s in chain:
                    m = s.local(t) @ m  # (row vectors: the innermost first)
                off = inner.local(t)[3, :2] if inner is not None else np.zeros(2)
                p = np.c_[pos, np.ones(len(pos))] @ m
                for tri in np.array(idx).reshape(-1, 3):
                    tris.append((used.index(img), [(*p[k, :3], uv[k, 0] + off[0], uv[k, 1] + off[1]) for k in tri]))
            frames.append(tris)
        out.append(frames)
    return f, used, out


def nulls(root, pack):
    """per muffler (0 stock, 1 tuned) and body: the MF_NULLs' positions (the body model's space), from the model's own scene.

    The tail pipes' nulls hang under the body's MUFFLER1 group (the stock exhaust) or MUFFLER2 (the tuned one, which can have a different
    number of pipes: the Savanna RX-7's tuned exhaust is a single pipe), and the car shows one of the two, so the fires are the nulls
    outside both groups and under the one that is shown. A car with no nulls of its own under MUFFLER2 keeps the stock ones."""
    per = {0: [], 1: []}
    for body in BODIES:
        p = os.path.join(root, "data", "car", pack, body, "body.igb")
        found = []  # (null index, position, under MUFFLER1, under MUFFLER2)
        if os.path.exists(p):
            f = IGB(p)

            def walk(node, mat, m1, m2):
                if node is None or node.mem is not None:
                    return
                n = name(f, node)
                m1, m2 = m1 or n == "MUFFLER1", m2 or n == "MUFFLER2"
                if node.type == "igTransform" and node[8] is not None:
                    mat = np.frombuffer(node[8].raw, "<f4").reshape(4, 4).astype(np.float64) @ mat
                    if n in NULLS:
                        found.append((NULLS.index(n), mat[3, :3], m1, m2))
                for child in scene.obj_list(f, f.ref(node[7])):
                    walk(child, mat, m1, m2)

            for info in f.of_type("igSceneInfo"):
                walk(f.ref(info[5]), np.eye(4), False, False)
        found.sort(key=lambda x: x[0])
        stock = [q for _, q, m1, m2 in found if not m2][:4]
        tuned = [q for _, q, m1, m2 in found if not m1][:4]
        if not any(m2 for _, _, _, m2 in found):
            tuned = stock
        per[0].append(stock)
        per[1].append(tuned)
    out = []
    for v in (0, 1):
        first = next((b for b in per[v] if b), [])
        out += [b if b else per[v][0] or first for b in per[v]]
    return out  # (muffler * 4 + body)


def cf(v):
    t = "%g" % round(float(v), 4)
    return (t if "." in t or "e" in t else t + ".0") + "f"


def main():
    root, dst = sys.argv[1], sys.argv[2]
    _, used, groups = effect(root)
    cardb = open(os.path.join(HERE, "..", "..", "src", "cardb.h")).read()
    packs = re.findall(r'"([^"]+)"', cardb[cardb.index("CARDB_PACKS["):cardb.index("};", cardb.index("CARDB_PACKS["))])
    cars = {k.lower(): v for k, v in json.load(open(os.path.join(HERE, "..", "brakelamps.json")))["cars"].items()}
    L = ["/* Generated by tools/stage/pack_backfire.py from the arcade's %s and the cars' body.igb: do not edit. */" % FILE,
         "#ifndef BACKFIREDB_H", "#define BACKFIREDB_H", "",
         "/* clBackFireManager's fire: groups SAMLL_A MIDDLE_A LARGE_A SMALL_B MIDDLE_B LARGE_B, each frame",
         " * 0..16 its triangles (x y z u v, in the fire's own space; texture BACKFIRE_TEX[k] in lights.wpk) */",
         "#define BACKFIRE_GROUPS 6", "#define BACKFIRE_FRAMES %d" % FRAMES, "#define BACKFIRE_TEXTURES %d" % len(used),
         "typedef struct { float v[3][5]; unsigned char tex; } backfire_tri;",
         "typedef struct { unsigned short first, n; } backfire_frame;"]
    tris, table = [], []
    for g in groups:
        row = []
        for fr in g:
            row.append((len(tris), len(fr)))
            tris += fr
        table.append(row)
    L.append("static const backfire_tri BACKFIRE_TRI[%d] = {" % max(1, len(tris)))
    for tex, vs in tris:
        L.append("  { { %s }, %d }," % (", ".join("{ %s }" % ", ".join(cf(x) for x in v) for v in vs), tex))
    L.append("};")
    L.append("static const backfire_frame BACKFIRE_FRAME[BACKFIRE_GROUPS][BACKFIRE_FRAMES] = {")
    for row in table:
        L.append("  { %s }," % ", ".join("{ %d, %d }" % r for r in row))
    L.append("};")
    L.append("")
    L.append("/* by CARDB_PACKS and muffler * 4 + body (muffler 0 stock, 1 tuned; body 0 stock, 1..3 aero A..C): its MF_NULL_R1 R2 L1 L2 there")
    L.append(" * are (n), their positions in the car's space; b: the B fires (emCarID 0x2a, 0x2b, 0x34) */")
    L.append("typedef struct { unsigned char n, b; float at[4][3]; } backfire_car;")
    L.append("static const backfire_car BACKFIRE_CAR[%d][8] = {" % len(packs))
    for p in packs:
        car = cars.get(p.lower())
        b = int(car in B_CARS) if car is not None else 0
        row = []
        for q in nulls(root, p):
            at = ", ".join("{ %s }" % ", ".join(cf(x) for x in v) for v in q) if q else "{ 0 }"
            row.append("{ %d, %d, { %s } }" % (len(q), b, at))
        L.append("  { %s }, /* %s */" % (", ".join(row), p))
    L.append("};")
    L.append("")
    L.append("#endif")
    open(dst, "w").write("\n".join(L) + "\n")
    print("%d triangles, textures %s, %d packs" % (len(tris), used, len(packs)))


if __name__ == "__main__":
    main()
