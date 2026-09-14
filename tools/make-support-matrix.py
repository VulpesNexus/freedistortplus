"""Generates docs/FREE_DISTORT_SUPPORT_MATRIX.md from the probes' results.

Each feature row names the evidence cases that prove it. A measured row's
status is read from docs/evidence/*.tsv, never written by hand: "verified"
(or "safe refusal" for a refusal) when every named case passed, "FAILED" when
any failed, "untested" when a case has not been run. Rows that state a design
limit, something Adobe's effect cannot represent, or something not built yet
say so.
"""

import csv
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EVIDENCE = ROOT / "docs" / "evidence"
OUT = ROOT / "docs" / "FREE_DISTORT_SUPPORT_MATRIX.md"

# kind, feature, Adobe's dialog, this editor, stored as vanilla Free Distort?, [(probe, case substring)]
#
# kind decides what a row's status can be (see STATUS below): "capability" and
# "refusal" rows are measured; "limit", "unrepresentable", and "unbuilt" rows
# state a fact with nothing to measure, or nothing built yet to measure.
FEATURES = [
    ("capability", "Move one corner freely", "yes, in a wireframe preview about 300 × 210 px", "yes, on the artwork at any zoom", "yes",
     [("editor", "a free drag leaves the corner where it was released")]),
    ("capability", "See the result on the real artwork", "no: wireframe preview only", "yes: Adobe's effect redraws the document when a drag is released", "nothing stored",
     [("support", "dragging a corner redraws the art")]),
    ("capability", "Precision", "whole points; 1.8 pt per preview pixel for a 250 pt object", "the pointer's artwork coordinates, unrounded", "yes",
     [("poc", "the destination corner the plugin wrote is in the dictionary")]),
    ("unbuilt", "Numeric corner input", "no", "not yet", "would be", []),
    ("capability", "One undo step per drag", "one per dialog OK", "yes", "nothing stored",
     [("editor", "four drag steps make one undo step"), ("editor", "one Undo takes the whole drag back")]),
    ("capability", "Cancel restores exactly", "Cancel", "Esc during a drag", "nothing stored",
     [("editor", "Escape part-way through a drag restores the dictionary exactly"), ("editor", "a canceled drag leaves no undo step")]),
    ("capability", "No drift from wandering drags", "n/a", "yes", "nothing stored",
     [("editor", "sixty wild steps")]),
    ("capability", "Perspective (trapezoid) constraint", "no", "Shift", "yes: eight numbers",
     [("editor", "perspective: a mostly horizontal drag"), ("editor", "perspective: a mostly vertical drag")]),
    ("capability", "Symmetric constraint", "no", "Alt", "yes: eight numbers",
     [("editor", "symmetric: the opposite corner moves")]),
    ("capability", "Affine (parallelogram) constraint", "no", "Shift+Alt", "yes: eight numbers",
     [("editor", "affine: the result is a parallelogram")]),
    ("unrepresentable", "True perspective foreshortening", "no", "no: the renderer is bilinear", "cannot be", []),
    ("unbuilt", "Snap to anchors, guides, grid", "no", "not yet (Illustrator's own snapping planned)", "nothing stored", []),
    ("unbuilt", "Reference point, rotate, scale handles", "no", "not yet", "would be eight numbers", []),
    ("unbuilt", "Copy and paste a normalized distortion", "no", "not yet", "would be eight numbers", []),
    ("capability", "Edits a duplicate without moving the original", "yes", "yes", "yes",
     [("editor", "editing a duplicate leaves the original")]),
    ("capability", "Follows the art when it moves", "yes (Adobe renormalizes)", "yes", "yes",
     [("editor", "after the art moves, the handles move with it"), ("semantics", "the drawing moves with the art")]),
    ("capability", "Adobe's dialog reads the editor's edit", "yes", "yes", "yes",
     [("poc", "Adobe's dialog opens on the plugin's edit")]),
    ("capability", "The editor reads the dialog's edit", "yes", "yes", "yes",
     [("poc", "a corner dragged in Adobe's dialog is what the plugin reads"), ("poc", "the editor's quad is Adobe's new state")]),
    ("capability", "Only Adobe's keys in the document", "yes", "yes", "yes",
     [("poc", "the dictionary holds only the sixteen Adobe keys"), ("poc", "the appearance holds Adobe Free Distort and nothing of this plugin")]),
    ("capability", "Save and reopen", "yes", "yes", "yes",
     [("poc", "save, close, and reopen keep all sixteen numbers")]),
    ("capability", "Copy and paste", "yes", "yes", "yes",
     [("persistence", "copy and paste carry the edited Free Distort")]),
    ("capability", "PDF with Illustrator editing capabilities, and SVG", "yes", "yes, and neither file names the plugin", "yes",
     [("persistence", "a PDF saved with Illustrator editing capabilities reopens"), ("persistence", "the PDF names no part"),
      ("persistence", "SVG export writes"), ("persistence", "the SVG names no part")]),
    ("capability", "Several Free Distorts on one object", "each from its own Appearance entry", "one at a time; which one is reported", "yes",
     [("editor", "two Free Distorts on one object are both found"), ("editor", "a drag edits only the Free Distort the editor chose")]),
    ("capability", "Live text stays live and editable", "yes", "yes", "yes",
     [("support", "retyping the live text redraws it")]),
    ("capability", "Adobe's Free Distort item and Apply Last Effect beside this command", "yes", "unaffected", "n/a",
     [("persistence", "Apply Last Effect repeats it")]),
    ("refusal", "Declines what it cannot show truthfully: raster images, and a source frame with no width or height", "n/a", "yes, with a reason", "nothing stored",
     [("support", "the editor declines art"), ("editor", "source frame has no width is not edited")]),
    ("capability", "Reads a source quad that is not a rectangle exactly as Adobe's renderer does", "converts it to a rectangle on OK, drawing unchanged", "yes, and checks its reading against Adobe's own commit", "yes",
     [("source-reading", "fits every case"), ("source-reading", "without changing the drawing"), ("source-reading", "control: the obvious readings")]),
    ("capability", "Edits a Free Distort whose source is not a rectangle", "yes", "yes: handles where Adobe draws, and the first drag writes a rectangle the way Adobe's OK does, without moving the art", "yes",
     [("editor", "source is not a rectangle is edited"), ("editor", "first drag writes the source"), ("preview", "handles where Adobe's own commit"),
      ("preview", "before the pointer moves"), ("preview", "the artwork does not move"), ("preview", "from a converted source")]),
    ("capability", "The source follows the art through Illustrator's own transforms, reshaping, and blends", "yes", "yes", "yes: the dictionary is untouched",
     [("source-follow", "leaves the dictionary as it was"), ("source-follow", "writes a rectangular source"), ("source-follow", "does not change the drawing"),
      ("source-follow", "every blend step"), ("source-reading", "after each of Illustrator's own operations")]),
    ("capability", "A drag with the real mouse, through Illustrator's own tool dispatch", "n/a", "yes: one undo step, pointer coordinates unrounded", "yes",
     [("mouse", "reaches the editor"), ("mouse", "one undo step"), ("mouse", "commits its corners"), ("mouse", "back exactly")]),
    ("capability", "The live outline during a real drag, and Adobe's render on release, as seen by a person", "n/a", "yes", "nothing stored",
     [("mouse", "captured before the mouse-down"), ("mouse", "preview outline follows a real drag"), ("mouse", "fill updates when a real drag is released")]),
    ("limit", "Adobe's own rendering updates during a drag", "yes, in the preview", "no: Illustrator does not repaint the document inside a tool's drag loop; the outline below stands in, and Adobe's render lands on release", "nothing stored", []),
    ("capability", "A live outline during the drag of exactly where Adobe will draw every anchor and handle", "n/a", "yes, for any art, when Free Distort is the last effect", "nothing stored",
     [("preview", "a drag opens with a preview"), ("preview", "every previewed anchor and handle is where Adobe draws it"), ("preview", "matches the glyph outlines")]),
    ("capability", "Opens without the plugin, no warning", "yes", "yes", "yes",
     [("missing-plugin", "opening the document raises no alert at all"), ("missing-plugin", "the drawing is exactly what was saved")]),
    ("capability", "Dialog edits after the plugin is removed, and the plugin reads them back", "yes", "yes", "yes",
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


STATUS = {
    "limit": "design limit",
    "unrepresentable": "not representable by Adobe Free Distort",
    "unbuilt": "not built",
}


def status(rows, kind, cases):
    if kind in STATUS:
        return STATUS[kind]
    found = []
    for probe, needle in cases:
        hits = [r for r in rows if r["probe"] == probe and needle in r["case"]]
        if not hits:
            return "untested"
        found.extend(hits)
    if not found or any(r["status"] == "NOT RUN" for r in found):
        return "untested"
    if any(r["status"] == "FAIL" for r in found):
        return "**FAILED**"
    if all(r["status"] == "PASS" for r in found):
        return "safe refusal" if kind == "refusal" else "verified"
    return "measured"


def main():
    rows = load()
    lines = [
        "# Free Distort support matrix",
        "",
        "Generated by *tools/make-support-matrix.py* from the probe results in [evidence/](evidence/); do not edit by hand. Status values:",
        "",
        "- *verified*: every probe case the row names passed in the host.",
        "- *safe refusal*: the editor declines the case, says why, and changes nothing, and the probe cases showing that passed.",
        "- *design limit*: something the host does not allow; the row says what stands in for it.",
        "- *not representable by Adobe Free Distort*: no editor could store it, because the effect's own model cannot express it.",
        "- *not built*: not implemented yet.",
        "- *untested*: implemented, but a case the row names has not been run.",
        "",
        "Everything the editor adds is an editing capability. The document only ever stores Adobe's own sixteen numbers, so the last column is about what the result *is*, not about anything extra being kept.",
        "",
        "| Feature | Adobe's dialog | Enhanced editor | Stored as vanilla Free Distort? | Status |",
        "| --- | --- | --- | --- | --- |",
    ]
    for kind, feature, adobe, ours, stored, cases in FEATURES:
        lines.append(f"| {feature} | {adobe} | {ours} | {stored} | {status(rows, kind, cases)} |")

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
