"""Fits candidate models of Adobe Free Distort to docs/evidence/mapping.tsv.

For each case, every anchor and direction handle of the source path is paired
with the same point of the expanded result, and each model predicts where the
point should have gone. The verdict for a case is the model with the smallest
worst-case deviation; a model "fits" when that deviation is below 1e-6 pt,
which is far above the host's printing precision and far below anything a
real model disagreement produces.

Writes docs/evidence/mapping-verdicts.tsv and prints a summary.
"""

import csv
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EVIDENCE = ROOT / "docs" / "evidence"
FIT = 1e-6


def bilinear(q, u, t):
    (tlh, tlv), (trh, trv), (blh, blv), (brh, brv) = q
    bh = blh * (1 - u) + brh * u
    bv = blv * (1 - u) + brv * u
    th = tlh * (1 - u) + trh * u
    tv = tlv * (1 - u) + trv * u
    return bh * (1 - t) + th * t, bv * (1 - t) + tv * t


def renorm(p, frm, to):
    fl, ft, fr, fb = frm
    tl, tt, tr, tb = to
    return (tl + (p[0] - fl) / (fr - fl) * (tr - tl), tb + (p[1] - fb) / (ft - fb) * (tt - tb))


def solve(a, b):
    """Gaussian elimination with partial pivoting; a is n x n, b length n."""
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for col in range(n):
        pivot = max(range(col, n), key=lambda r: abs(m[r][col]))
        if abs(m[pivot][col]) < 1e-14:
            return None
        m[col], m[pivot] = m[pivot], m[col]
        for r in range(n):
            if r != col:
                f = m[r][col] / m[col][col]
                for k in range(col, n + 1):
                    m[r][k] -= f * m[col][k]
    return [m[i][n] / m[i][i] for i in range(n)]


def homography(src, dst):
    a, b = [], []
    for (x, y), (u, v) in zip(src, dst):
        a.append([x, y, 1, 0, 0, 0, -u * x, -u * y]); b.append(u)
        a.append([0, 0, 0, x, y, 1, -v * x, -v * y]); b.append(v)
    h = solve(a, b)
    if h is None:
        return None
    return lambda p: ((h[0] * p[0] + h[1] * p[1] + h[2]) / (h[6] * p[0] + h[7] * p[1] + 1),
                      (h[3] * p[0] + h[4] * p[1] + h[5]) / (h[6] * p[0] + h[7] * p[1] + 1))


def best_affine(pairs):
    # Least squares for x' and y' separately: normal equations of [x y 1].
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
    return lambda p: (cx[0] * p[0] + cx[1] * p[1] + cx[2], cy[0] * p[0] + cy[1] * p[1] + cy[2])


def main():
    path = EVIDENCE / "mapping.tsv"
    rows = list(csv.DictReader(open(path, newline=""), delimiter="\t"))
    cases = {}
    for r in rows:
        cases.setdefault(r["case"], []).append(r)

    out = [["case", "points", "model", "worst deviation (pt)", "fits"]]
    failed = False
    for name, rs in cases.items():
        src = [r for r in rs if r["kind"] == "S"]
        res = [r for r in rs if r["kind"] == "R"]
        if len(src) != len(res):
            out.append([name, f"{len(src)} vs {len(res)}", "point count differs: the effect added or removed points", "", "no"])
            continue
        pairs = []
        for s, t in zip(src, res):
            for k in ("a", "l", "r"):
                pairs.append(((float(s[k + "h"]), float(s[k + "v"])), (float(t[k + "h"]), float(t[k + "v"]))))
        sl, st, sr, sb = [float(x) for x in rs[0]["source"].split(",")]
        d = [float(x) for x in rs[0]["destination"].split(",")]
        dq = [(d[0], d[1]), (d[2], d[3]), (d[4], d[5]), (d[6], d[7])]
        il, it, ir, ib = [float(x) for x in rs[0]["inputBounds"].split(",")]
        source, inputb = (sl, st, sr, sb), (il, it, ir, ib)

        def dev(f):
            worst = 0.0
            for p, q in pairs:
                g = f(p)
                worst = max(worst, math.hypot(g[0] - q[0], g[1] - q[1]))
            return worst

        eff = [renorm(c, source, inputb) for c in dq]
        models = {
            "bilinear on input bounds, destination renormalized from source": lambda p: bilinear(
                eff, (p[0] - il) / (ir - il), (p[1] - ib) / (it - ib)),
            "bilinear on the stored source rectangle, no renormalization": lambda p: bilinear(
                dq, (p[0] - sl) / (sr - sl), (p[1] - sb) / (st - sb)),
            "best-fitting affine map": best_affine(pairs),
        }
        h = homography([(sl, st), (sr, st), (sl, sb), (sr, sb)], dq)
        if h is not None:
            models["homography from source corners to destination corners"] = h
        for model, f in models.items():
            try:
                w = dev(f)
            except ZeroDivisionError:
                w = float("inf")
            fits = w < FIT
            out.append([name, str(len(pairs)), model, f"{w:.3g}", "yes" if fits else "no"])
            if model.startswith("bilinear on input bounds") and not fits:
                failed = True

    with open(EVIDENCE / "mapping-verdicts.tsv", "w", newline="") as fh:
        csv.writer(fh, delimiter="\t", lineterminator="\n").writerows(out)
    for row in out:
        print("\t".join(row))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
