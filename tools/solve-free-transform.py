"""Classifies what Illustrator's Free Transform tool did in each recorded drag.

Reads docs/evidence/free-transform-points.tsv (tools/record-free-transform.ps1).
The fixture is a path through a 6 x 5 grid of anchors over 100..400 x
100..300; each recorded operation is the same path after one drag by a person.

For each operation this reports where the four bounding corners went, which of
them moved, and how well three models reproduce all thirty anchors and their
handles: the best affine map, the bilinear map from the four corners (what
Adobe Free Distort stores and draws), and the homography from the four corners
(true perspective). Writes docs/evidence/free-transform.tsv.
"""

import csv
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EVIDENCE = ROOT / "docs" / "evidence"
XS = [100, 150, 210, 260, 330, 400]
YS = [100, 140, 190, 250, 300]
FIT = 1e-3


def source_grid():
    pts = []
    for j, y in enumerate(YS):
        for k in range(len(XS)):
            i = k if j % 2 == 0 else len(XS) - 1 - k
            pts.append((XS[i], y))
    return pts


def solve(a, b):
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for col in range(n):
        pivot = max(range(col, n), key=lambda r: abs(m[r][col]))
        if abs(m[pivot][col]) < 1e-12:
            return None
        m[col], m[pivot] = m[pivot], m[col]
        for r in range(n):
            if r != col:
                f = m[r][col] / m[col][col]
                for k in range(col, n + 1):
                    m[r][k] -= f * m[col][k]
    return [m[i][n] / m[i][i] for i in range(n)]


def bilinear(q, u, t):
    (tlh, tlv), (trh, trv), (blh, blv), (brh, brv) = q
    top = (tlh + (trh - tlh) * u, tlv + (trv - tlv) * u)
    bottom = (blh + (brh - blh) * u, blv + (brv - blv) * u)
    return (bottom[0] + (top[0] - bottom[0]) * t, bottom[1] + (top[1] - bottom[1]) * t)


def main():
    path = EVIDENCE / "free-transform-points.tsv"
    if not path.exists():
        print("no recorded operations")
        return 1
    ops = {}
    order = []
    for r in csv.DictReader(open(path, newline="", encoding="utf-8"), delimiter="\t"):
        if r["operation"] not in ops:
            order.append(r["operation"])
        ops.setdefault(r["operation"], []).append(r)

    src = source_grid()
    corners_src = [(100, 300), (400, 300), (100, 100), (400, 100)]
    names = ["top-left", "top-right", "bottom-left", "bottom-right"]
    out = [["operation", "description", "corners after (TL TR BL BR)", "corners moved", "affine worst (pt)",
            "bilinear from corners worst (pt)", "homography from corners worst (pt)", "Free Distort can store it"]]
    for op in order:
        rows = ops[op]
        after = [(float(r["ah"]), float(r["av"])) for r in rows]
        handles = [((float(r["lh"]), float(r["lv"])), (float(r["rh"]), float(r["rv"]))) for r in rows]
        if len(after) != len(src):
            out.append([op, rows[0]["description"], "", f"{len(after)} anchors, expected {len(src)}", "", "", "", "no"])
            continue
        index = {p: i for i, p in enumerate(src)}
        corners = [after[index[c]] for c in corners_src]
        moved = [names[i] for i in range(4) if math.hypot(corners[i][0] - corners_src[i][0], corners[i][1] - corners_src[i][1]) > 0.01]

        pairs = list(zip(src, after))
        # Handles of a straight-segment path sit on their anchors; include them
        # so a model that moved handles off the anchors would show.
        pairs += [(s, h[0]) for s, h in zip(src, handles)] + [(s, h[1]) for s, h in zip(src, handles)]

        def worst(f):
            try:
                return max(math.hypot(f(p)[0] - q[0], f(p)[1] - q[1]) for p, q in pairs)
            except (ZeroDivisionError, TypeError):
                return float("inf")

        # Best affine by least squares.
        ata = [[0.0] * 3 for _ in range(3)]
        atx = [0.0] * 3
        aty = [0.0] * 3
        for (x, y), (u, v) in pairs:
            row = (x, y, 1.0)
            for i in range(3):
                for j in range(3):
                    ata[i][j] += row[i] * row[j]
                atx[i] += row[i] * u
                aty[i] += row[i] * v
        cx, cy = solve(ata, atx), solve(ata, aty)
        affine = lambda p: (cx[0] * p[0] + cx[1] * p[1] + cx[2], cy[0] * p[0] + cy[1] * p[1] + cy[2])
        bil = lambda p: bilinear(corners, (p[0] - 100) / 300, (p[1] - 100) / 200)
        a, b = [], []
        for (x, y), (u, v) in zip(corners_src, corners):
            a.append([x, y, 1, 0, 0, 0, -u * x, -u * y]); b.append(u)
            a.append([0, 0, 0, x, y, 1, -v * x, -v * y]); b.append(v)
        h = solve(a, b)
        homog = (lambda p: ((h[0] * p[0] + h[1] * p[1] + h[2]) / (h[6] * p[0] + h[7] * p[1] + 1),
                            (h[3] * p[0] + h[4] * p[1] + h[5]) / (h[6] * p[0] + h[7] * p[1] + 1))) if h else None
        wa, wb, wh = worst(affine), worst(bil), (worst(homog) if homog else float("inf"))
        stores = "yes" if wb < FIT else "no"
        out.append([op, rows[0]["description"], " ".join(f"{c[0]:.3f},{c[1]:.3f}" for c in corners),
                    ", ".join(moved) or "none", f"{wa:.3g}", f"{wb:.3g}", f"{wh:.3g}", stores])

    with open(EVIDENCE / "free-transform.tsv", "w", newline="", encoding="utf-8") as fh:
        csv.writer(fh, delimiter="\t", lineterminator="\n").writerows(out)
    for row in out[1:]:
        print(" | ".join(row))
    return 0


if __name__ == "__main__":
    sys.exit(main())
