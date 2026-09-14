# FreeDistort+

An on-canvas editor for Adobe Illustrator's built-in *Free Distort* effect. Adobe's own dialog edits the effect in a thumbnail-sized wireframe and rounds every corner to a whole point. This plugin lets you drag the four corners directly over your artwork, at any zoom, at the pointer's exact position, while an outline shows where Illustrator will draw the result.

**It edits Adobe's effect; it does not replace it.** The *Appearance* panel still says *Free Distort*, because it is Adobe's *Free Distort*. A document edited with this plugin opens normally on a machine without it, draws the same, and can still be edited there in Adobe's own *Free Distort* dialog. When the plugin is back, it picks up whatever that dialog changed.

**Status: proof of concept, not released.** Built for Illustrator 2026 (version 30) on Windows. Do not assume a build for one Illustrator year loads in another. Its editing has been verified through its test interface and by hand with the real mouse.

## Install

1. Quit Illustrator.
2. Copy *FreeDistortPlus.aip* into the shared plugin folder for Illustrator 2026, *%LOCALAPPDATA%\Adobe Illustrator Plug-ins\30*. Other plugins live there too; do not make a folder of its own.
3. If you have not already, point *Edit > Preferences > Plug-ins & Scratch Disks > Additional Plug-ins Folder* at that folder once, then restart Illustrator.

Install only one copy. The same file name in both Illustrator's own *Plug-ins* folder and the additional folder makes Illustrator ignore the additional folder entirely.

## Use

1. Select one object.
2. Choose *Effect > Distort & Transform > FreeDistort+…* (if Illustrator put it elsewhere, *Object > Transform > FreeDistort+…*). The object gets a *Free Distort* if it has none, and the *FreeDistort+* tool is selected.
3. Drag a corner handle. While you drag, a magenta outline shows exactly where Illustrator will draw every edge; the artwork itself updates when you release. *Shift* keeps a trapezoid, *Alt* moves the opposite corner the other way, and *Shift+Alt* keeps a parallelogram. *Esc* during a drag cancels it.
4. Each drag is one step in *Edit > Undo*.

Double-clicking *Free Distort* in the *Appearance* panel still opens Adobe's dialog.

## What it does not do yet

No numeric fields, snapping, or arrow-key nudging; no editing of a *Free Distort* applied to a fill or stroke rather than the whole object; no macOS build. The modifier keys may change once they are checked against Illustrator's *Free Transform* tool.

## More

- [docs/FREEDISTORT_PLUS_INVESTIGATION.md](docs/FREEDISTORT_PLUS_INVESTIGATION.md): what Adobe's effect actually does, and why the plugin is built this way
- [docs/BUILDING.md](docs/BUILDING.md): building, installing from source, and the tests

## License

GPL-3.0-or-later, with an Adobe Illustrator SDK linking exception: see [LICENSE](LICENSE) and [LICENSE-EXCEPTION](LICENSE-EXCEPTION). Copyright © 2026 Vixen420.
