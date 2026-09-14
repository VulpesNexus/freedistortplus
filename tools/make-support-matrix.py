"""Generates docs/FREE_DISTORT_SUPPORT_MATRIX.md from the probes' results.

Each feature row names the evidence cases that prove it. A row's status is
read from docs/evidence/*.tsv, never written by hand: "verified" when every
named case passed, "FAILED" when any failed, "not measured" when a case has
not been run. Rows with no cases are design statements and say so.
"""

import csv
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EVIDENCE = ROOT / "docs" / "evidence"
OUT = ROOT / "docs" / "FREE_DISTORT_SUPPORT_MATRIX.md"

# feature, Adobe's dialog, this editor, stored as vanilla Free Distort?, [(probe, case substring)]
FEATURES = [
    ("Move one corner freely", "yes, in a wireframe preview about 300 × 210 px", "yes, on the artwork at any zoom", "yes",
     [("editor", "a free drag leaves the corner where it was released")]),
    ("See the result on the real artwork", "no: wireframe preview only", "yes: Adobe's effect redraws the document when a drag is released", "nothing stored",
     [("support", "dragging a corner redraws the art")]),
    ("Precision", "whole points; 1.8 pt per preview pixel for a 250 pt object", "the pointer's artwork coordinates, unrounded", "yes",
     [("poc", "the destination corner the plugin wrote is in the dictionary")]),
    ("Numeric corner input", "no", "not yet", "would be", []),
    ("One undo step per drag", "one per dialog OK", "yes", "nothing stored",
     [("editor", "four drag steps make one undo step"), ("editor", "one Undo takes the whole drag back")]),
    ("Cancel restores exactly", "Cancel", "Esc during a drag", "nothing stored",
     [("editor", "Escape part-way through a drag restores the dictionary exactly"), ("editor", "a canceled drag leaves no undo step")]),
    ("No drift from wandering drags", "n/a", "yes", "nothing stored",
     [("editor", "sixty wild steps")]),
    ("Perspective (trapezoid) constraint", "no", "Shift", "yes: eight numbers",
     [("editor", "perspective: a mostly horizontal drag"), ("editor", "perspective: a mostly vertical drag")]),
    ("Symmetric constraint", "no", "Alt", "yes: eight numbers",
     [("editor", "symmetric: the opposite corner moves")]),
    ("Affine (parallelogram) constraint", "no", "Shift+Alt", "yes: eight numbers",
     [("editor", "affine: the result is a parallelogram")]),
    ("True perspective foreshortening", "no", "no: the renderer is bilinear", "cannot be", []),
    ("Snap to anchors, guides, grid", "no", "not yet (Illustrator's own snapping planned)", "nothing stored", []),
    ("Reference point, rotate, scale handles", "no", "not yet", "would be eight numbers", []),
    ("Copy and paste a normalized distortion", "no", "not yet", "would be eight numbers", []),
    ("Edits a duplicate without moving the original", "yes", "yes", "yes",
     [("editor", "editing a duplicate leaves the original")]),
    ("Follows the art when it moves", "yes (Adobe renormalizes)", "yes", "yes",
     [("editor", "after the art moves, the handles move with it"), ("semantics", "the drawing moves with the art")]),
    ("Adobe's dialog reads the editor's edit", "yes", "yes", "yes",
     [("poc", "Adobe's dialog opens on the plugin's edit")]),
    ("The editor reads the dialog's edit", "yes", "yes", "yes",
     [("poc", "a corner dragged in Adobe's dialog is what the plugin reads"), ("poc", "the editor's quad is Adobe's new state")]),
    ("Only Adobe's keys in the document", "yes", "yes", "yes",
     [("poc", "the dictionary holds only the sixteen Adobe keys"), ("poc", "the appearance holds Adobe Free Distort and nothing of this plugin")]),
    ("Save and reopen", "yes", "yes", "yes",
     [("poc", "save, close, and reopen keep all sixteen numbers")]),
    ("Copy and paste", "yes", "yes", "yes",
     [("persistence", "copy and paste carry the edited Free Distort")]),
    ("PDF with Illustrator editing capabilities, and SVG", "yes", "yes, and neither file names the plugin", "yes",
     [("persistence", "a PDF saved with Illustrator editing capabilities reopens"), ("persistence", "the PDF names no part"),
      ("persistence", "SVG export writes"), ("persistence", "the SVG names no part")]),
    ("Several Free Distorts on one object", "each from its own Appearance entry", "one at a time; which one is reported", "yes",
     [("editor", "two Free Distorts on one object are both found"), ("editor", "a drag edits only the Free Distort the editor chose")]),
    ("Live text stays live and editable", "yes", "yes", "yes",
     [("support", "retyping the live text redraws it")]),
    ("Adobe's Free Distort item and Apply Last Effect beside this command", "yes", "unaffected", "n/a",
     [("persistence", "Apply Last Effect repeats it")]),
    ("Declines what it cannot show truthfully: raster images, non-rectangular sources", "n/a", "yes, with a reason", "nothing stored",
     [("support", "the editor declines art"), ("editor", "an effect whose source is not a rectangle is not edited")]),
    ("A drag with the real mouse, through Illustrator's own tool dispatch", "n/a", "yes: one undo step, pointer coordinates unrounded", "yes",
     [("mouse", "reaches the editor"), ("mouse", "one undo step"), ("mouse", "commits its corners"), ("mouse", "back exactly")]),
    ("The live outline during a real drag, and Adobe's render on release, as seen by a person", "n/a", "yes", "nothing stored",
     [("mouse", "captured before the mouse-down"), ("mouse", "preview outline follows a real drag"), ("mouse", "fill updates when a real drag is released")]),
    ("Adobe's own rendering updates during a drag", "yes, in the preview", "no: Illustrator does not repaint the document inside a tool's drag loop; the outline below stands in, and Adobe's render lands on release", "nothing stored", []),
    ("A live outline during the drag of exactly where Adobe will draw every anchor and handle", "n/a", "yes, for any art, when Free Distort is the last effect", "nothing stored",
     [("preview", "a drag opens with a preview"), ("preview", "every previewed anchor and handle is where Adobe draws it"), ("preview", "matches the glyph outlines")]),
    ("Opens without the plugin, no warning", "yes", "yes", "yes",
     [("missing-plugin", "opening the document raises no alert at all"), ("missing-plugin", "the drawing is exactly what was saved")]),
    ("Dialog edits after the plugin is removed, and the plugin reads them back", "yes", "yes", "yes",
     [("missing-plugin", "Adobe's own dialog edits the effect with the plugin absent"), ("missing-plugin", "the plugin reads exactly what Adobe's dialog wrote")]),
]

ART = ["path", "compound path", "group", "clipping group", "point text", "area text", "symbol instance", "embedded raster"]


def load():
    rows = []
    for path in sorted(EVIDENCE.glob("*.tsv")):
        with open(path, newline="", encoding="utf-8") as fh:
            reader = csv.DictReader(fh, delimiter="\t")
            if not reader.fieldnames or "probe" not in reader.fieldnames:
                continue
            rows.extend(reader)
    return rows


def status(rows, cases):
    if not cases:
        return "design"
    found = []
    for probe, needle in cases:
        hits = [r for r in rows if r["probe"] == probe and needle in r["case"]]
        if not hits:
            return "not measured"
        found.extend(hits)
    if any(r["status"] == "FAIL" for r in found):
        return "**FAILED**"
    if any(r["status"] == "NOT RUN" for r in found):
        return "not measured"
    if all(r["status"] == "PASS" for r in found):
        return "verified"
    return "measured"


def main():
    rows = load()
    lines = [
        "# Free Distort support matrix",
        "",
        "Generated by *tools/make-support-matrix.py* from the probe results in [evidence/](evidence/); do not edit by hand. *verified* means every probe case the row names passed in the host; *not measured* means a case has not been run; *design* rows state an intent with nothing to measure yet.",
        "",
        "Everything the editor adds is an editing capability. The document only ever stores Adobe's own sixteen numbers, so the last column is about what the result *is*, not about anything extra being kept.",
        "",
        "| Feature | Adobe's dialog | Enhanced editor | Stored as vanilla Free Distort? | Status |",
        "| --- | --- | --- | --- | --- |",
    ]
    for feature, adobe, ours, stored, cases in FEATURES:
        lines.append(f"| {feature} | {adobe} | {ours} | {stored} | {status(rows, cases)} |")

    lines += ["", "## Art types", "", "Measured by *tools/probe-support.ps1*. The editor goes wherever Adobe's effect goes; Adobe's effect takes groups, paths, and compound paths directly, and everything else through Illustrator's conversion to paths.", "",
              "| Art | Effect added | Editor drag redraws, geometry untouched | Art keeps its type and contents | Adobe's edit path keeps the corners |",
              "| --- | --- | --- | --- | --- |"]
    for art in ART:
        cells = []
        declined = [r for r in rows if r["probe"] == "support" and r["group"] == art and "declines art" in r["case"]]
        for needle in ("the effect can be added", "dragging a corner redraws", "keeps its type and contents", "Adobe's own edit path keeps"):
            hits = [r for r in rows if r["probe"] == "support" and r["group"] == art and needle in r["case"]]
            if hits:
                cells.append(hits[0]["status"].lower())
            elif declined and declined[0]["status"] == "PASS":
                cells.append("declined: Adobe's effect leaves it unchanged")
            else:
                cells.append("not measured")
        lines.append(f"| {art} | " + " | ".join(cells) + " |")

    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
