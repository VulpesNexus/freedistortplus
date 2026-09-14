# Release notes

## 0.1.0

The first release: an on-canvas editor for Adobe Illustrator's own *Free Distort* effect, for Illustrator 2026 on Windows.

- **Drag the four corners on the artwork**, at any zoom, at the pointer's unrounded position, where Adobe's dialog offers a thumbnail wireframe and whole points. A magenta outline shows during the drag exactly where Illustrator will draw every anchor and handle, and Adobe's own render lands on release.
- **Free Transform's keys:** *Shift* locks an axis, *Alt* moves the opposite corner the other way, *Shift+Alt* makes the edge converge, and *Ctrl* is ignored so the habit carries over. *Esc* cancels a drag.
- **Exact numbers** in the *Free Distort Corners* dialog, opened by double-clicking the tool or *Alt*-clicking a handle: positions on Illustrator's rulers or offsets from the undistorted corner, in any unit, with arithmetic, and a live preview. *Undistort* puts every corner back, and *Alt* turns *Cancel* into *Reset*.
- **Arrow keys** move a clicked corner by Illustrator's *Keyboard Increment*, ten times as far with *Shift*.
- **Snapping** through Illustrator's own Smart Guides, plus the undistorted corners, the undistorted center, and the other corners.
- **One undo step** per drag, per dialog, and per arrow key press.
- **Documents keep only Adobe's state.** A document edited with the plugin opens with no warning and draws the same on a machine without it, and Adobe's dialog can still edit it there. Copy and paste, save and reopen, PDF with editing capabilities, and SVG all keep the sixteen numbers exactly.
- **Sources that are not rectangles**, which only other scripts and plugins write, are read exactly as Adobe's renderer reads them, and checked against Adobe's own commit.

**Tested** against this exact binary, on Illustrator 30.7.0 and Windows 11: 284 checks passed and none failed, with 57 more values measured and recorded. 19 of those checks were made by hand with the real mouse and keyboard: a drag, a click, the arrow keys, the icon, both windows, and a Smart Guides snap. Every row is in [docs/FREE_DISTORT_SUPPORT_MATRIX.md](docs/FREE_DISTORT_SUPPORT_MATRIX.md), and the raw results in [docs/evidence/](docs/evidence/).

**Known limitations:** no macOS build; no editing of a *Free Distort* on a fill or stroke; *Shift+Alt* cannot foreshorten the interior, because *Free Distort* is bilinear; the drag outline needs *Free Distort* to be the last effect; no reference point, rotate, or scale handles yet. Illustrator 30.7.0 itself can crash or hang after a script closes and reopens documents. That reproduces with no third-party plugin loaded at all, and it is described in section M of [the investigation](docs/FREEDISTORT_PLUS_INVESTIGATION.md).
