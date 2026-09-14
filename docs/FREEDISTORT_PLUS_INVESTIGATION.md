# FreeDistort+: a better editor over Adobe's own effect

An investigation into whether Adobe Illustrator's built-in *Effect > Distort & Transform > Free Distort* can keep its renderer and its document format while its editor is replaced by something usable, and the proof-of-concept plugin built from the answer.

Everything below was measured against **Adobe Illustrator 2026, version 30.7.0, 64-bit, on Windows 11**, with the **Adobe Illustrator 2026 SDK, build 114**. Each host claim names the evidence file that produced it; raw output lives in [evidence/](evidence/). Where a claim rests on reading a header rather than on a host measurement, it says so.

The first measurements were taken through LiveShear's test bridge, before this plugin existed. Every number below comes from this plugin's own probes, re-run through its own bridge, and they agreed with the first run to the digit.

---

## A. Native Free Distort parameter model

Adobe Free Distort is the live effect registered as `Adobe Free Distort`, titled *Free Distort*, version 1.0, style flags `0x2` (post-effect), input preference `0x87` ([evidence/registry.txt](evidence/registry.txt)). By the SDK's `AIStyleFilterPreferredInputArtType`, `0x87` is groups, paths, compound paths, and the obsolete "mystery path" bit: everything else reaches the effect through Illustrator's conversion to paths.

The effect is implemented by *Plug-ins/Illustrator Filters/Distort.aip*; its dialog is a separate plugin, *Plug-ins/Illustrator UI/DistortUI.aip*, and the two talk through a private `AI DistortUI Suite` ([evidence/distort-strings.txt](evidence/distort-strings.txt)). The effect binary's whole parameter vocabulary is:

| Key | Type | Meaning | Renderer consumes it? | Dialog-only? |
| --- | --- | --- | --- | --- |
| `src0h`, `src0v` | real | source top-left corner | yes, as a frame of reference (section D) | no |
| `src1h`, `src1v` | real | source top-right corner | yes, as a frame of reference | no |
| `src2h`, `src2v` | real | source bottom-left corner | yes, as a frame of reference | no |
| `src3h`, `src3v` | real | source bottom-right corner | yes, as a frame of reference | no |
| `dst0h` … `dst3v` | real | destination corners, same order | yes | no |
| `-doingFakeGo` | — | internal; named in the binary, never seen in a dictionary | not observed | internal |
| `-liveDistortPreviewArt` | — | internal; named in the binary, never seen in a dictionary | not observed | internal |
| `-DefaultApplyEffectsKey` | boolean | written by Illustrator's effect-edit path, not by *Distort.aip*; **not saved** with the document | no | host, transient |

The "consumes it" column is measured by *tools/probe-semantics.ps1*, which moves each key alone by 37 pt (or flips it) and compares the expanded drawing ([evidence/semantics.txt](evidence/semantics.txt)). Every one of the sixteen reals changes the drawing, and putting it back restores the drawing exactly. Flipping or deleting `-DefaultApplyEffectsKey` changes nothing. A key Adobe does not know changes nothing, and survives Adobe's own edit path.

Coordinates are Illustrator's artwork coordinates, y up, the same system `AIArtSuite::GetArtTransformBounds` reports. Corner numbering goes row by row, which is not an outline order: the outline is 0, 1, 3, 2.

An effect added without its dialog has an **empty dictionary**, and an empty dictionary draws the art unchanged. Adobe's own edit path, on commit, writes the sixteen reals and `-DefaultApplyEffectsKey`.

## B. Mathematical mapping

**Free Distort is a bilinear map, applied to every anchor point and every direction handle, over the input art's geometric bounds.** It is not a homography and not affine.

The discriminating fixture is a closed path through a 6 × 5 grid of anchors. *tools/probe-mapping.ps1* writes a known source and destination, expands a copy of the result, and pairs every anchor and handle with its image; *tools/solve-mapping.py* fits the candidates ([evidence/mapping.tsv](evidence/mapping.tsv), [evidence/mapping-verdicts.tsv](evidence/mapping-verdicts.tsv)). Worst deviation, in points:

| Case | Bilinear, renormalized (section D) | Best-fitting affine | Homography from the corners |
| --- | --- | --- | --- |
| grid, general quad | 4.7 × 10⁻¹⁰ | 30.7 | 45.5 |
| grid, parallelogram | 2.8 × 10⁻¹⁴ | 1.1 × 10⁻¹³ | 1.4 × 10⁻¹⁴ |
| grid, trapezoid | 2.8 × 10⁻¹⁴ | 32.9 | **26.5** |
| grid, folded bow-tie | 5.7 × 10⁻¹⁴ | 165 | 1780 |
| curves and handles, source = bounds | 6.2 × 10⁻¹⁰ | 22.3 | 46.1 |
| curves and handles, source ≠ bounds | 6.5 × 10⁻¹⁰ | 27.0 | 50.0 |
| curves and handles, source 900 pt away | 6.5 × 10⁻¹⁰ | 27.0 | 2.2 × 10⁵ |

The residuals of 10⁻¹⁰ are the precision ExtendScript prints coordinates to. A five-point path with independent handles shows handles are mapped by the same function as anchors. The parallelogram row is the control: every model fits it, because a bilinear map of a parallelogram is affine. The trapezoid row settles perspective: a homography misses it by 26.5 pt.

Three consequences shape the editor:

- **No subdivision.** Thirty anchors in, thirty out. A straight segment stays straight however bent the quad is; only its end points move. Interior spacing along an edge is linear, not foreshortened.
- **It is not perspective.** A trapezoid destination makes a trapezoid outline, but the interior is not projected: equal steps stay equal. A "perspective" control over this effect can offer the trapezoid, and must not promise foreshortening.
- **A parallelogram destination is exactly affine**, because the bilinear term vanishes. That is how translation, rotation, scale, and shear are all representable.

## C. Coordinate system

The dictionary holds artwork coordinates. Illustrator's SDK calls them public or "soft" coordinates: origin at the ruler origin, y up (`AIHardSoft.h`). The tool API's cursor and `GetArtTransformBounds` use the same system, so the plugin never converts. Documents with a moved ruler origin, several artboards, and rotated views are in section N until measured.

## D. Source and destination quad semantics

**The stored source rectangle is a frame of reference, not a position.** The renderer re-expresses each destination corner relative to the source rectangle and lays the result onto the input art's current geometric bounds, then applies the bilinear map from those bounds:

```
E_i = B.origin + (dst_i − S.origin) / S.size × B.size        (per axis)
p′  = bilinear(E, (p − B.origin) / B.size)
```

with `S` the stored source rectangle, `B` the input bounds, and `E` the quad actually drawn into. The curved fixture with a source rectangle that is *not* its bounds fits this to 6.5 × 10⁻¹⁰ pt, and so does the same fixture with the source rectangle 900 pt away from the art; reading the source rectangle as a position misses those by 9.8 pt and 1860 pt (section B).

Measured consequences ([evidence/semantics.txt](evidence/semantics.txt)):

- **Moving the art** leaves the dictionary untouched and moves the drawing with it exactly.
- **Scaling the art** leaves the dictionary untouched and scales the distortion with it.
- **Rotating the art** leaves the dictionary untouched; the quad is re-laid onto the rotated art's new axis-aligned bounds, so the distortion does not rotate with the object. Adobe's effect behaves the same with or without this plugin.
- **Reshaping the art** so its bounds change re-lays the quad the same way.
- **Adobe's own editor, on commit, renormalizes**: it rewrites the source as the current input bounds and the destination as the same normalized quad, so the drawing does not change. Measured after a move, a 150 % scale, and a reshape.

So the effect has no brittle absolute-geometry assumption of its own. Two dictionaries that differ only by renormalization are the same effect, and the editor exploits that: it always writes `src` = the current input bounds, which is exactly what Adobe's dialog writes.

**The input bounds are the geometric bounds of the effect's input art**, not of the object ([evidence/semantics.txt](evidence/semantics.txt), [evidence/support.txt](evidence/support.txt)):

| Input | Input bounds Adobe uses | The object's geometric bounds |
| --- | --- | --- |
| path with a 20 pt stroke | 90..340 × 100..330 | the same: strokes do not count |
| path under a Transform effect (50 %, moved 30 pt) | 182.5..307.5 × 157.5..272.5 | 90..340 × 100..330 |
| clipping group, clip path larger than its art | the clip path | the same |
| point text, 72 pt | the glyph outlines, 125.5..323.5 × 338.9..388.6 | 120..325.3 × 321.7..400 |
| area text | the glyph outlines, 100.9..320.2 × 345.1..420 | the frame, 100..360 × 260..420 |

The text rows are why the plugin asks Adobe rather than computing bounds: handles laid on the text frame would sit up to 85 pt from where Adobe draws.

### Sources that are not rectangles

**Illustrator never makes one.** Adobe's dialog writes the input bounds. *tools/probe-source-follow.ps1* distorts a pentagon and then applies Illustrator's own operations to it: move, uniform and non-uniform scale with and without *Scale Strokes & Effects*, rotation by 30° and 90°, reflection, shear, moving an anchor out and in, adding and removing an anchor, pulling a handle, and a blend between two distorted objects, expanded ([evidence/source-follow.txt](evidence/source-follow.txt)). None touches the dictionary, every blend step carries a rectangular source, and after each one Adobe's edit path commits a rectangle again without changing the drawing. "The source follows the art" is therefore not an update to the source: the stored rectangle stays put, and the renderer re-lays it onto whatever the input bounds have become. After all thirteen operations, the drawing is the bilinear map onto the renormalized quad at every anchor and handle ([evidence/source-follow-verdicts.tsv](evidence/source-follow-verdicts.tsv)).

A source of any other shape can only come from another writer: a script, a plugin, or an edited action file. That still has to be handled, and the renderer does have a precise reading of one.

**The measurement.** *tools/probe-source-quads.ps1* writes non-rectangular sources into the dictionary key by key: each of the eight source keys alone in both directions, every pair of keys, each destination key alone against a convex and a trapezoidal source, parallelogram, trapezoid, convex, concave, bow-tie, 1000 pt away, 20 seeded random cases including wild ones, the same questions over different input bounds, and curves with independent handles. That makes 121 cases. For each it records the drawing, lets Adobe's own edit path commit, and records the dictionary Adobe wrote and the drawing again ([evidence/source-quads-cases.tsv](evidence/source-quads-cases.tsv)).

Two findings came first, and they carry the rest:

- **The drawing is still bilinear over the input bounds**, into some quad E, to 10⁻⁹ pt in every case.
- **Adobe's own edit path converts such a source losslessly.** It writes the source as the input bounds and the destination as that E, and the drawing does not change in any of the 121 cases. So Adobe's commit is an oracle for E, for any dictionary.

**The reading.** The obvious ones fail: the source's bounding box misses 119 of 121 cases, inverse bilinear in the source quad misses 99, and a homography misses 98 ([evidence/source-quads-verdicts.tsv](evidence/source-quads-verdicts.tsv)). Designed variations showed E's structure. Each Eᵢ depends only on dᵢ, and one bilinear function carries all four corners, with an xy term. Its values at the corners of one particular rectangle are round numbers. That gives this reading, which fits all 121 cases at the committed quad and at every drawn anchor and handle, to 10⁻⁹ pt:

```
R  = the rectangle left src0h, top src0v, right src1h, bottom src2v     (the source's frame)
Qᵢ = Bᵢ − (srcᵢ − Rᵢ)                                                   (per corner, unscaled points)
Eᵢ = bilinear(Q, (dstᵢ − R.origin) / R.size)
p′ = bilinear(E, (p − B.origin) / B.size)
```

Here `B` is the input bounds and `Bᵢ`, `Rᵢ` are corners in Adobe's order. The frame uses only three corners' worth of numbers. The other source numbers act as offsets, in points, not scaled by any size, so moving `src3h` 37 pt right moves the drawn bottom-right corner 37 pt left however large the art is. With a rectangular source, every offset is zero and this is the renormalization above. A bow-tie source gives the frame a negative width, and the reading follows it. A source whose frame has no width or no height leaves the renderer dividing by zero.

**What the editor does with it.** It reads any source through this formula (*QuadMath.h*, `EffectiveQuad` for a quad source, pinned to six host commits in *tools/mathtest*). It does not rely on the formula alone. Each time it measures the input bounds through Adobe's edit path, that commit's destination is where Adobe draws. The editor puts the handles there, and refuses the effect if its formula disagrees by more than 0.001 pt. The first drag writes the source as the input bounds, exactly as Adobe's OK converts it, and the artwork does not move. A frame with no width or height is refused before Adobe is asked anything. The status line reports the source's shape, where the quad came from, and the formula's distance from Adobe's answer.

## E. Compatibility with the vanilla editor

The round trip the project exists for, measured by *tools/probe-poc.ps1* ([evidence/poc.txt](evidence/poc.txt)):

1. the plugin moves a destination corner by rewriting Adobe's dictionary
2. Adobe's effect re-renders it, with the source path untouched and the appearance still exactly one `Adobe Free Distort` holding only Adobe's keys
3. Adobe's own dialog opens showing the moved corner ([evidence/poc-vanilla-open.png](evidence/poc-vanilla-open.png)), and Cancel leaves the dictionary as it was
4. a corner dragged inside Adobe's dialog, then OK, is what the plugin reads next, with the plugin's own corner preserved beside it
5. save, close, and reopen keep all sixteen numbers to the digit; the host's `-DefaultApplyEffectsKey`, present before saving, is not in the reopened dictionary, which is consistent with Illustrator treating keys that start with a minus sign as transient

Adobe's dialog was driven from a second process with posted window messages: it is a `#32770` window titled *Free Distort* containing a single Drover view, 334 × 310 px. Its preview is wireframe only, about 0.56 px per point for a 250 pt object, with no numeric fields, and it **stores whole points**: a 20 px drag that is 35.7 pt at that scale was committed as 35. That is the precision problem in one number.

**The dialog's edit path can run without its dialog.** With user interaction off, `AIArtStyleParserSuite::EditEffectParameters` on a Free Distort commits at once, exactly as OK on an untouched dialog does, renormalization included. That is what makes section F's input-bounds measurement possible.

## F. Public-SDK UI integration limits

From the SDK headers, confirmed where the host could confirm it:

| Wanted | Available? | Evidence |
| --- | --- | --- |
| Replace Free Distort's editor on Appearance double-click | **no** | The edit message goes to the registering plugin (`AILiveEffectData::self`); `AILiveEffectSuite::EditParameters` is "Internal. Do not use."; no notifier fires on an Appearance double-click of an effect. |
| Intercept *Effect > Distort & Transform > Free Distort…* | **observe only** | `kAILiveFreeDistortCommandPreNotifierStr` exists; `AINotifierMessage` has no cancel field. |
| Read and write Adobe's dictionary | yes | `AIArtStyleParserSuite` plus a fresh `CreateLiveEffectParameters` copy; section E |
| Know which Appearance entry has focus | partly | `AIArtStyleParserSuite::GetFocusEffect`; the header never says it is the panel's selection |
| Draw handles over the artwork | yes | `AIAnnotatorSuite`, `AIAnnotatorDrawerSuite` |
| A custom tool with drag handling | yes | `AIToolSuite`; drags suspend live effects unless `kToolDoesntWantArtStyleExecutionSuspender` |
| Arrow keys while a tool is active | **no** tool message | only `[` and `]` reach a tool |
| Drive a tool with posted mouse messages, for testing | **no** | Mouse messages posted to the document view reach no tool handler at all (measured with message counters in the tool), unlike Adobe's Drover dialogs, which accept them. The document view is the `OS_ViewContainer` window whose parent is `OWL.Document`: panels are Drover views with the same title, and the *Tools* panel is found first, which voided the first run of this test. |
| Repaint the document, and so a live effect's result, during a tool's drag | **no** | `kToolDoesntWantArtStyleExecutionSuspender` and `AIDocumentSuite::RedrawDocument` on every drag step both left the artwork unchanged until release, as reported by a person dragging; no SDK call updates a view at once. Annotator drawing does update during a drag. |
| See the canvas without bringing Illustrator forward | yes | `PrintWindow` with `PW_RENDERFULLCONTENT` on the document view captures artwork, live effects, and annotator drawing while Illustrator is behind other windows ([evidence/editor-handles-on-canvas.png](evidence/editor-handles-on-canvas.png)). |
| Illustrator's own snapping | yes | `AICursorSnapSuite::Track`, custom constraints with `SetCustom` |
| One undo step per drag | yes | a tool's mouse messages share one undo context; `AIUndoSuite::UndoChanges` discards the previous step |
| Native panel hosting Win32 controls | yes | `AIPanelSuite::Create`, `GetPlatformWindow` |
| Suppress an effect's dialog | yes | `ASUserInteractionSuite::SetInteractionAllowed(kASInteractWithNone)` |

So **Outcome 1's interception is not available through the public SDK**, and the plugin does not try: no binary patching, no private suites. The architecture is Outcome 2 with Outcome 3's canvas editing: Adobe's double-click keeps opening Adobe's dialog, and this plugin's command and tool open the better editor over the same state.

## G. Editor architecture

**The document is the result.** The editor is a tool (*FreeDistort+*). Its annotator draws four things over the real artwork: the effect's destination corners, the outline, a bilinear grid at thirds, and the dashed input bounds. A capture of the canvas after an edit shows exactly that over Adobe's re-rendered artwork ([evidence/editor-handles-on-canvas.png](evidence/editor-handles-on-canvas.png)).

**During a drag, the preview is the editor's; on release, it is Adobe's.** The first design assumed Adobe's effect would redraw the document on every drag step. A person dragging with the real mouse reported otherwise: the handles followed the pointer, but the artwork changed only on release. That held with `kToolDoesntWantArtStyleExecutionSuspender` set, and again with `AIDocumentSuite::RedrawDocument` called on every step ([evidence/mouse.txt](evidence/mouse.txt)). The SDK has no call that updates a view immediately. Illustrator simply does not repaint the document inside a tool's drag loop.

So the editor draws the result itself, from Adobe's own. When a drag begins, it reads the art's styled result, which is what Adobe's effect drew for the starting quad. It stores every anchor and handle as its (u, t) in that quad, by inverse bilinear. Because Free Distort maps each point independently by the bilinear map (section B), the same (u, t) through the quad under the pointer is exactly where Adobe will put that point. The annotator draws those curves as an outline that follows the pointer ([evidence/editor-preview-open.png](evidence/editor-preview-open.png)). *tools/probe-preview.ps1* opens a drag, reads back every previewed point, releases, and compares with what Adobe drew ([evidence/preview.txt](evidence/preview.txt)):

- a 30-anchor path starting from an already distorted quad: 90 anchors and handles, worst deviation 0 at the precision coordinates are printed to
- live point text: 267 points of the glyph outlines, worst deviation 10⁻⁹ pt, and the text still live afterward

The styled result cannot be read at the moment it is needed. The first real drag with this preview showed no outline, and the editor's status said why: `AIArtStyleSuite::GetStyledArt` fails inside a tool's mouse-down, though it succeeds from a script message. So the editor captures the preview beforehand, whenever it looks at its target outside a drag: at tool selection, and on selection and art notifications. Each capture is kept with the art, style, and quad it came from, and a drag uses it only while all three are unchanged. After each release, the next notification captures the new result. The outline is drawn in magenta, because Illustrator outlines the selected source art in its layer color, blue by default, and that outline stays put during a drag.

The preview needs Free Distort to be the last effect in the appearance, so that the styled result is its output, and a starting quad that is not folded. Otherwise the drag shows only the handles and the quad, and the status says why. Dragging a corner rewrites Adobe's dictionary on every mouse event and Adobe's effect redraws in place, at the user's zoom, with the rest of the appearance and the surrounding artwork. There is no miniature preview.

**Transactions are Illustrator's own.** Each drag is one undo step, the way any Illustrator tool's drag is: every drag event first discards the previous event's write with `UndoChanges`, then writes from the gesture's start. Escape during a drag discards it. There is no separate OK and Cancel session: a released drag is committed exactly as a released Selection-tool drag is, and *Edit > Undo* takes it back whole. This is a deliberate departure from the brief's modal-session sketch, because a session layered over Illustrator's history would give the user two undo models.

**With the real mouse.** Illustrator's tools receive only real input (section F), and automating real input takes the pointer and the foreground from whoever is at the machine; this machine's antivirus also denied access to a script that injects input. So a person made the drags, and everything was read back over COM.

The first session had them drag one corner and release, drag another and press *Esc* before releasing, then press *Ctrl+Z* once. The editor received 2 mouse-downs, 264 drag events, and 2 mouse-ups through Illustrator's own dispatch, and its hit test took the corner under the pointer. The history then held nothing to undo and one step to redo, so the released drag was exactly one undo step and the canceled one was none. The dictionary was back to its starting state exactly. Redoing the step showed one corner committed at (415.111, 372.556): the pointer's own artwork coordinates, where Adobe's dialog would have stored whole points. That session also reported the artwork changing only on release, which led to the preview below. Its evidence file was overwritten by later sessions.

The recorded session ([evidence/mouse.txt](evidence/mouse.txt)) was made after the preview was in place. One drag of 1 mouse-down, 345 drag events, and 1 mouse-up began with a preview captured before the mouse-down. It committed one corner at the pointer's unrounded coordinates as exactly one undo step, and *Ctrl+Z* restored the dictionary exactly. The person dragging reported the magenta outline following the drag and Adobe's fill updating on release.

**No drift, by construction.** Every step is computed from the quad at mouse-down plus the pointer now, never from the previous step. Measured: sixty wild steps ending at the start point leave the dictionary and the drawing identical ([evidence/editor.txt](evidence/editor.txt)).

**No state is cached across edits.** The target is re-read from the host whenever the tool is selected, whenever the selection or the art changes, and at every mouse-down. The input bounds are measured by asking Adobe (below) at tool selection and at every mouse-down, and estimated in between by carrying the last measurement along with the art's geometric bounds.

**Measuring input bounds by asking Adobe.** The rectangle the renderer lays the quad onto is not exposed. It is not the object's bounds when anything above the effect changes geometry, and it is computed by Adobe for text and other converted art. The plugin asks for it directly. With user interaction switched off, it asks the effect to edit its parameters; Adobe's edit path commits the renormalized dictionary, whose source rectangle is the input bounds by construction. The plugin reads that rectangle and undoes the commit with `UndoChanges`, leaving the appearance and the undo history as they were (measured in section E). If that path is unavailable, it falls back to geometric bounds and says so.

**Which effect.** With several Free Distorts on one object, the editor keeps the one it was editing; else takes the Appearance panel's focus effect if that is a Free Distort; else the last. The status reports which rule chose. Every write verifies that the post-effect at its index is still `Adobe Free Distort`.

**Duplicates stay independent.** An object and its duplicate share an art style until something forks it, and writing into the style's own dictionary would move both. Every write goes into a fresh dictionary, so editing one leaves the other alone (measured).

## H. Snapping architecture

Not built yet, on purpose: snapping bugs can hide coordinate bugs, and free dragging is proved exact first. The plan is `AICursorSnapSuite::Track`, Illustrator's own engine, with its standard control string for anchors, guides, grid, and page and artboard bounds, plus custom point and line constraints (`SetCustom`) for the source corners and edges and the other destination corners. It honors the user's *Snap to Point* and Smart Guides preferences.

## I. Supported Free Transform-like modes

Every mode compiles down to the same eight destination numbers, so none needs state Adobe cannot store:

| Modifier | Mode | What it does |
| --- | --- | --- |
| none | Free | the corner goes where it is put |
| *Shift* | Perspective | a mostly horizontal drag widens or narrows that corner's horizontal edge about its midpoint; a mostly vertical drag does the same to its vertical edge |
| *Alt* | Symmetric | the diagonally opposite corner moves the other way; the centroid stays |
| *Shift+Alt* | Affine | the quad stays a parallelogram, with the opposite corner fixed and the dragged corner under the pointer |

These modifiers are provisional. The Free Transform parity study (the brief's items 18 and 39), which measures what Illustrator's own Free Transform tool does in its Free Distort and Perspective Distort modes and at which modifiers, has not been run yet, and it decides the final mapping.

## J. Unsupported modes and why

- **True perspective foreshortening** cannot be stored. The renderer is bilinear (section B).
- **Curved edges and envelope meshes** cannot be stored: four corners are all there is.
- **A reference point and rotation handle** need no stored state, but are not built yet.

## K. Persistence

Nothing to persist beyond Adobe's dictionary ([evidence/persistence.txt](evidence/persistence.txt)):

- **Save, close, reopen** keep all sixteen numbers to the digit (section E).
- **Copy and paste** carry all sixteen numbers. The pasted copy lands elsewhere on the page, and its drawing follows it by renormalization.
- **A PDF saved with Illustrator editing capabilities** reopens with the same Free Distort, and names no part of this plugin anywhere in its bytes.
- **SVG export** writes the distorted drawing, and names no part of this plugin.
- **Duplicate** carries the effect, and editing the duplicate leaves the original alone (section G).

## L. Missing-plugin behavior

**Removing the plugin is a non-event.** *tools/probe-missing-plugin.ps1* measures it across three Illustrator sessions, restarting between them ([evidence/missing-plugin.txt](evidence/missing-plugin.txt)):

1. **With the plugin**, a path, a point text, and a group each get a Free Distort edited through the editor, and the document is saved. The saved file contains neither `FreeDistortPlus` nor `VulpesNexus` anywhere in its bytes.
2. **Without it**, Illustrator opens the document and raises **no alert of any kind**; a watcher in a second process looked for one for the whole open. Every object draws exactly as saved, to the micropoint. Another plugin reads the appearance as `Adobe Free Distort`. Adobe's own dialog opens showing the editor's corner ([evidence/missing-plugin-vanilla-open.png](evidence/missing-plugin-vanilla-open.png)), a corner is dragged in it, and OK commits. The document is saved.
3. **With the plugin back**, it reads exactly what Adobe's dialog wrote, worst difference 0, and the editor shows that state with no conversion.

For contrast, LiveShear's own effect, which *is* a third-party effect, raises Illustrator's missing-plugin warning in the same situation and stops re-rendering. Nothing of the kind happens here, because nothing here is third-party state.

## M. Performance

A drag step costs the plugin one style parse, one copy of a dictionary of at most seventeen entries, one style rebuild, and one `UndoChanges`. Adobe's effect then renders when Illustrator next draws. Measured over 200 steps each ([evidence/persistence.txt](evidence/persistence.txt)):

| Artwork | Per step, the plugin's write | Illustrator redrawing the result |
| --- | --- | --- |
| five-point path | 0.42 ms | 47 ms |
| 30-anchor path | 0.98 ms | 66 ms |
| three lines of area text | 0.34 ms | 248 ms |

The write figures include the round trip through scripting, so they are upper bounds. Whatever lag a drag has is Illustrator re-running its own effect, not the editor. Text is the slow case: Illustrator converts it to outlines for the effect on every render.

**Stability.** Illustrator crashed once during these measurements: an access violation inside *Illustrator.exe* at offset `0x849e1c`, while a probe saved the document as PDF, closed it, and reopened it, with the editor active. That build's annotator converted coordinates through the *current* view rather than the view it was drawing, and kept its target across documents closing. Both were changed: drawing uses the view it is handed, and a document about to close, or another window coming forward, clears the target. Afterward, 16 cycles of the same sequence, 8 with the editor active and 8 with the Selection tool, all survived ([evidence/churn.txt](evidence/churn.txt)). That does not show the plugin caused the first crash: Illustrator 30.7.0 also crashes under scripted document churn with no third-party plugin at all, at a different offset (LiveShear's crash-control evidence). It is recorded as unresolved, with the fix in place and the question open.

## N. Remaining unknowns

Worst first.

1. **Adobe's own rendering cannot update during a drag** (section G). The editor's exact outline stands in for it, verified against Adobe's render point by point and seen following a real drag. It needs Free Distort to be the last effect; with another effect after it, a drag shows only the handles.
2. **The Free Transform parity study** (section I), which decides the final modifier mapping. It needs real input for the same reason.
3. The first crash (section M): fixed on suspicion, not proven either way.
4. A source frame with no width or height (section D). The editor declines it before asking Adobe anything; what Adobe's renderer does with one has not been measured, because it divides by zero.
5. Ruler origins, several artboards, rotated views, and art inside rotated groups (section C).
6. Free Distort inside a fill or stroke rather than on the whole object; the editor edits post-effects only.
7. Arrow-key nudging: no tool message carries arrow keys, and Illustrator's own keyboard increment preference is not named in the SDK or in any obvious key of the preferences file.
8. Menu placement, mostly settled. The command item is in Adobe's own `Live Vector &Distort && Transform` group, added from `PostStartupPlugin`. Unlike a live-effect item placed there (*.workspace/ADOBE_PLUGIN_MENUS.md*), this plain command does not break *Apply Last Effect*: Adobe's *Free Distort* item applies, and *Apply Last Effect* repeats it, with the command beside them ([evidence/persistence.txt](evidence/persistence.txt)). Not yet checked: *Apply Last Effect* after a third-party effect.
