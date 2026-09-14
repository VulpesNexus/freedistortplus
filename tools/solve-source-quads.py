"""Fits readings of a non-rectangular Free Distort source to the host's records.

For every case tools/probe-source-quads.ps1 recorded the dictionary as
written, the drawing it produced, the dictionary Adobe's own edit path
committed from it, and whether that commit changed the drawing (docs/evidence/source-quads-cases.tsv, with the
points in source-quads-points.tsv). Adobe's commit always writes the source as
the input bounds B, so its destination is the quad E the effect draws into.

Each candidate reading predicts E from the stored source, the stored
destination, and B. A reading fits a case when its E matches Adobe's committed
destination and the stored drawing is the bilinear map from B onto that E, at
every anchor and handle, both within 1e-6 pt.

The records of tools/probe-source-follow.ps1, which apply Illustrator's own
operations to distorted art, are checked the same way. Writes a -verdicts.tsv
beside each set of records and prints a summary. Exits 1 if Adobe's reading
misses any case, or if Adobe's commit changed any drawing.
"""

import csv
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EVIDENCE = ROOT / "docs" / "evidence"
FIT = 1e-6


def quad(numbers):
    return [(numbers[0], numbers[1]), (numbers[2], numbers[3]), (numbers[4], numbers[5]), (numbers[6], numbers[7])]


def bilinear(q, u, t):
    (tlh, tlv), (trh, trv), (blh, blv), (brh, brv) = q
    top = (tlh + (trh - tlh) * u, tlv + (trv - tlv) * u)
    bottom = (blh + (brh - blh) * u, blv + (brv - blv) * u)
    return (bottom[0] + (top[0] - bottom[0]) * t, bottom[1] + (top[1] - bottom[1]) * t)


def normalized(p, left, top, right, bottom):
    return ((p[0] - left) / (right - left), (p[1] - bottom) / (top - bottom))


def adobe(src, dst, b):
    """The reading this probe established. The source's frame is the rectangle
    through its top-left corner, its top-right corner's h, and its bottom-left
    corner's v. Each source corner's offset from that rectangle's corner is
    subtracted, unscaled, from the input bounds' corner; the destination is
    mapped bilinearly from the frame onto that quad."""
    left, top, right, bottom = src[0][0], src[0][1], src[1][0], src[2][1]
    frame = [(left, top), (right, top), (left, bottom), (right, bottom)]
    corners = [(b[0], b[1]), (b[2], b[1]), (b[0], b[3]), (b[2], b[3])]
    q = [(corners[i][0] - (src[i][0] - frame[i][0]), corners[i][1] - (src[i][1] - frame[i][1])) for i in range(4)]
    return [bilinear(q, *normalized(d, left, top, right, bottom)) for d in dst]


def bounding_box(src, dst, b):
    left = min(p[0] for p in src); right = max(p[0] for p in src)
    bottom = min(p[1] for p in src); top = max(p[1] for p in src)
    return [(b[0] + (d[0] - left) / (right - left) * (b[2] - b[0]), b[3] + (d[1] - bottom) / (top - bottom) * (b[1] - b[3])) for d in dst]


def inverse_bilinear(q, p):
    u, t = 0.5, 0.5
    for _ in range(100):
        x, y = bilinear(q, u, t)
        (tlh, tlv), (trh, trv), (blh, blv), (brh, brv) = q
        xu = (brh - blh) * (1 - t) + (trh - tlh) * t
        yu = (brv - blv) * (1 - t) + (trv - tlv) * t
        xt = (tlh + (trh - tlh) * u) - (blh + (brh - blh) * u)
        yt = (tlv + (trv - tlv) * u) - (blv + (brv - blv) * u)
        det = xu * yt - xt * yu
        if abs(det) < 1e-12:
            raise ZeroDivisionError
        du = ((p[0] - x) * yt - xt * (p[1] - y)) / det
        dt = (xu * (p[1] - y) - (p[0] - x) * yu) / det
        u += du; t += dt
        if abs(du) + abs(dt) < 1e-15:
            break
    return u, t


def inverse_bilinear_reading(src, dst, b):
    out = []
    for d in dst:
        u, t = inverse_bilinear(src, d)
        out.append((b[0] + u * (b[2] - b[0]), b[3] + t * (b[1] - b[3])))
    return out


def solve(a, rhs):
    n = len(rhs)
    m = [row[:] + [rhs[i]] for i, row in enumerate(a)]
    for col in range(n):
        pivot = max(range(col, n), key=lambda r: abs(m[r][col]))
        if abs(m[pivot][col]) < 1e-12:
            raise ZeroDivisionError
        m[col], m[pivot] = m[pivot], m[col]
        for r in range(n):
            if r != col:
                f = m[r][col] / m[col][col]
                for k in range(col, n + 1):
                    m[r][k] -= f * m[col][k]
    return [m[i][n] / m[i][i] for i in range(n)]


def homography_reading(src, dst, b):
    corners = [(b[0], b[1]), (b[2], b[1]), (b[0], b[3]), (b[2], b[3])]
    a, rhs = [], []
    for (x, y), (u, v) in zip(src, corners):
        a.append([x, y, 1, 0, 0, 0, -u * x, -u * y]); rhs.append(u)
        a.append([0, 0, 0, x, y, 1, -v * x, -v * y]); rhs.append(v)
    h = solve(a, rhs)
    return [((h[0] * x + h[1] * y + h[2]) / (h[6] * x + h[7] * y + 1), (h[3] * x + h[4] * y + h[5]) / (h[6] * x + h[7] * y + 1)) for x, y in dst]


READINGS = {
    "Adobe: frame from top-left, top-right h, bottom-left v; corner offsets subtracted from the bounds": adobe,
    "the source's bounding box, renormalized onto the bounds": bounding_box,
    "inverse bilinear in the source quad": inverse_bilinear_reading,
    "homography from the source quad onto the bounds": homography_reading,
}


def solve_records(stem):
    cases = list(csv.DictReader(open(EVIDENCE / f"{stem}-cases.tsv", newline="", encoding="utf-8"), delimiter="\t"))
    points = {}
    for r in csv.DictReader(open(EVIDENCE / f"{stem}-points.tsv", newline="", encoding="utf-8"), delimiter="\t"):
        points.setdefault((r["case"], r["kind"]), []).append(r)

    out = [["case", "reading", "points", "E vs Adobe's commit (pt)", "drawing vs bilinear onto E (pt)", "fits"]]
    summary = {name: [0, 0] for name in READINGS}
    commits_changing_drawing = 0
    failed = False
    for case in cases:
        name = case["case"]
        src = quad([float(x) for x in case["stored source"].split(",")])
        dst = quad([float(x) for x in case["stored destination"].split(",")])
        csrc = [float(x) for x in case["committed source"].split(",")]
        b = (csrc[0], csrc[1], csrc[2], csrc[5])  # left, top, right, bottom
        adobe_e = quad([float(x) for x in case["committed destination"].split(",")])
        if case["commit changed the drawing"] != "no":
            commits_changing_drawing += 1

        before = points.get((case["fixture"], "S"), [])
        drawn = points.get((name, "R"), [])
        pairs = []
        for s, t in zip(before, drawn):
            for k in ("a", "l", "r"):
                pairs.append(((float(s[k + "h"]), float(s[k + "v"])), (float(t[k + "h"]), float(t[k + "v"]))))

        for reading, f in READINGS.items():
            try:
                e = f(src, dst, b)
                e_dev = max(math.hypot(p[0] - q[0], p[1] - q[1]) for p, q in zip(e, adobe_e))
                d_dev = 0.0
                for p, q in pairs:
                    g = bilinear(e, *normalized(p, *b))
                    d_dev = max(d_dev, math.hypot(g[0] - q[0], g[1] - q[1]))
            except ZeroDivisionError:
                e_dev = d_dev = float("inf")
            if not drawn or len(before) != len(drawn):
                d_dev = float("inf")
            fits = e_dev < FIT and d_dev < FIT
            summary[reading][0 if fits else 1] += 1
            out.append([name, reading, str(len(pairs)), f"{e_dev:.3g}", f"{d_dev:.3g}", "yes" if fits else "no"])
            if f is adobe and not fits:
                failed = True

    with open(EVIDENCE / f"{stem}-verdicts.tsv", "w", newline="", encoding="utf-8") as fh:
        csv.writer(fh, delimiter="\t", lineterminator="\n").writerows(out)
    print(f"{stem}: {len(cases)} cases; Adobe's commit changed the drawing in {commits_changing_drawing}")
    if commits_changing_drawing:
        failed = True
    for reading, (yes, no) in summary.items():
        print(f"  {yes:3d} fit, {no:3d} miss: {reading}")
    return failed, len(cases), summary, commits_changing_drawing


def main():
    failed = False
    results = [["probe", "group", "case", "expected", "observed", "status"]]

    def result(group, case, expected, observed, ok):
        results.append(["source-reading", group, case, expected, observed, "PASS" if ok else "FAIL"])

    adobe_name = next(iter(READINGS))
    for stem in ("source-quads", "source-follow"):
        if not (EVIDENCE / f"{stem}-cases.tsv").exists():
            print(f"{stem}: no records yet")
            failed = True
            continue
        stem_failed, count, summary, changed = solve_records(stem)
        failed = failed or stem_failed
        fit = summary[adobe_name][0]
        if stem == "source-quads":
            result("reading", "Adobe's reading of a non-rectangular source fits every case, at the committed quad and at every drawn anchor and handle",
                   f"{count} of {count}", f"{fit} of {count}", fit == count)
            result("reading", "Adobe's own edit path converts a non-rectangular source to a rectangle without changing the drawing",
                   f"0 of {count} drawings changed", f"{changed} of {count} drawings changed", changed == 0)
            box = summary["the source's bounding box, renormalized onto the bounds"][1]
            inverse = summary["inverse bilinear in the source quad"][1]
            result("reading", "control: the obvious readings do not fit", "the bounding box and inverse bilinear each miss cases",
                   f"bounding box misses {box}, inverse bilinear misses {inverse}", box > 0 and inverse > 0)
        else:
            result("follow", "after each of Illustrator's own operations on distorted art, the drawing is the bilinear map onto the renormalized quad",
                   f"{count} of {count}", f"{fit} of {count}", fit == count)

    with open(EVIDENCE / "source-reading.tsv", "w", newline="", encoding="utf-8") as fh:
        csv.writer(fh, delimiter="\t", lineterminator="\n").writerows(results)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
