# Building and testing FreeDistort+

Everything here was done on Windows 11 with Visual Studio 2022, against Adobe Illustrator 2026 (30.7.0). The plugin is Windows-only today; the geometry in *plugin/Source/QuadMath.h* is platform-neutral, but nothing has been built or tested on macOS.

## What you need

| | |
| --- | --- |
| Toolchain | Visual Studio 2022, *Desktop development with C++* workload (platform toolset v143, C++17) |
| Windows SDK | 10.0 or later |
| Illustrator SDK | Adobe Illustrator 2026 SDK, build 114 |
| Python | 3.x on *PATH*: the SDK's *tools/pipl/create_pipl.py* runs as a prebuild step, and *tools/solve-mapping.py* reads the mapping evidence |
| Architecture | x64 only |

The Adobe SDK is not redistributable, so it is not vendored here and no path to it is baked in:

```powershell
$env:AI_SDK_ROOT = "<path to>\Adobe Illustrator 2026 SDK"
.\tools\build.ps1
```

The build writes *build\Release\FreeDistortPlus.aip*.

## Installing what you built

The plugin goes in Illustrator's Additional Plug-ins Folder, the one per-generation folder every plugin shares: *%LOCALAPPDATA%\Adobe Illustrator Plug-ins\30* for Illustrator 2026. Point *Edit > Preferences > Plug-ins & Scratch Disks > Additional Plug-ins Folder* at it once. Then, with Illustrator closed:

```powershell
.\tools\install.ps1              # copy FreeDistortPlus.aip into that folder
.\tools\install.ps1 -Uninstall   # take it out again
```

The script touches only *FreeDistortPlus.aip*; other plugins in the folder are left alone. It refuses to run while Illustrator is open, because Illustrator reads plugin folders only at startup and holds a loaded plugin open.

## Tests

One test needs no Illustrator. It compiles *QuadMath.h* unmodified and checks it against host measurements and against the invariants of every editing mode:

```powershell
.\tools\run-mathtest.ps1
```

The rest drive a real Illustrator over COM, with the plugin installed. They work only in their own document, *fdp-probe.ai* in the temp folder, found by name, and never in whatever document happens to be open:

```powershell
.\tools\probe-poc.ps1            # the plugin and Adobe's own dialog edit the same state
.\tools\probe-mapping.ps1        # what the renderer does to a point
python .\tools\solve-mapping.py  # which model fits
.\tools\probe-semantics.ps1      # what each dictionary key means; source and destination under art changes
.\tools\probe-editor.ps1         # drags, undo, Escape, drift, modes, duplicates, moved art, several instances
.\tools\probe-support.ps1        # every kind of artwork
.\tools\probe-persistence.ps1    # copy and paste, PDF, SVG, the menu command, cost per drag step
.\tools\probe-preview.ps1        # the drag outline against what Adobe draws on release
.\tools\probe-source-quads.ps1   # non-rectangular sources: the drawing, and Adobe's own commit, over 121 cases
.\tools\probe-source-follow.ps1  # Illustrator's own operations on distorted art
python .\tools\solve-source-quads.py    # which reading of a source fits
python .\tools\make-support-matrix.py   # regenerates docs/FREE_DISTORT_SUPPORT_MATRIX.md from the results
```

A drag with the real mouse cannot be scripted (section F of the investigation), so a person makes it and *tools/record-manual-drag.ps1* reads everything back over COM; its own help lists the steps before and after the drag.

Each writes its raw output under *docs/evidence/*, through `Hide-Personal` in *tools/ai.ps1*, so paths that name the machine are redacted as they are written.

*probe-poc.ps1* opens Adobe's Free Distort dialog twice. The call that opens it blocks, so a background job (*tools/dialog-driver.ps1*) finds the window and drives it with posted window messages, which do not move the real pointer or take the keyboard. The dialog does take the foreground while it is open, which matters if someone else is using the desktop.

Two probes restart Illustrator, and are run as detached processes so that nothing killing the calling shell can leave Illustrator without its plugins:

```powershell
.\tools\run-detached.ps1 -Probe probe-missing-plugin.ps1 -Arguments '-Phase all'   # the document without the plugin, and back
.\tools\run-detached.ps1 -Probe probe-churn.ps1                                    # document churn with and without the editor active
```

*probe-missing-plugin.ps1* takes *FreeDistortPlus.aip*, and only that file, out of the Additional Plug-ins Folder and puts it back. While it opens the document without the plugin, *tools/watch-alerts.ps1* watches from a second process for any alert Illustrator raises, captures it, and dismisses it; the expected number is zero. Opening Adobe's dialog with this plugin absent needs another plugin's bridge, and uses LiveShear's when it is installed.

`.\tools\capture-view.ps1 -Path <png>` captures Illustrator's document view with `PrintWindow`, without bringing Illustrator forward or touching the mouse or keyboard. `.\tools\canvas-driver.ps1` posts a mouse gesture to the document view; it is kept to show that Illustrator's tools do not receive posted input, and the editor's status line `tool messages` counts what does arrive.

After changing the plugin, `.\tools\redeploy.ps1` builds, quits Illustrator (closing every document without saving), installs, and starts it again.

Probe scripts share *tools/fixtures.jsx*, and work only in their own document, *fdp-probe.ai* in the temp folder. Two Illustrator scripting behaviors shape them, and both cost a run before they were understood:

- **A document made active in one call is not yet the current document to a plugin in that call.** Session setup activates the probe document in a call of its own.
- **After page items are removed in a call, items added later in that call are missing from every collection** (`pageItems`, `pathItems`, `getByName`) until the next call. Fixtures register what they create, and `FDP.named` falls back to that registry.

## The test bridge

The plugin answers `app.sendScriptMessage("FreeDistortPlus", selector, arguments)`. The probes use these selectors:

| Selector | Arguments | Does |
| --- | --- | --- |
| `version` | | product, version, and a decimal-point check |
| `appearance`, `geometry`, `registry`, `menu groups`, `menu` | | read-only dumps |
| `apply effect` | `name\|key=r:1;key=b:true` | append any live effect by unique name |
| `set param`, `delete param` | `index\|key\|type\|value`, `index\|key` | edit one key of a post-effect, in a fresh copy of its dictionary |
| `edit effect` | `index` | Adobe's own editor for that post-effect, as a double-click would open it |
| `remove effect`, `move effect` | `index`, `from,to` | restack post-effects |
| `undo count`, `undo clear` | | undo and redo transaction counts; forget the current document's history |
| `fd list`, `fd read`, `fd bounds` | `index` | find, read, and measure input bounds of Free Distort |
| `fd write` | `index\|l,t,r,b\|8 numbers` | write a source rectangle and destination quad |
| `fd corner` | `index\|corner\|h,v` | move one corner in on-canvas coordinates |
| `fd append` | | add an identity Free Distort |
| `editor open`, `editor status`, `editor refresh`, `editor handles` | `measure` for refresh | the editor's own state |
| `editor drag` | `corner\|mode\|cancelAt\|h,v;h,v;...` | run a drag through the editor's drag code |
| `editor preview open`, `editor preview points`, `editor preview close` | `corner\|mode\|h,v`, none, none | leave a drag open, read the outline it draws, then release it |
| `editor reset` | | zero the editor's tool-message counters |
| `tools`, `tool select`, `view` | tool name for select | tool and view inspection |

**Status: a test interface, not an API.** It is unsupported and may change or disappear between any two versions. It ships in the binary so the binary that passes the tests is the binary that ships. Everything it reaches is reachable through Illustrator's own scripting, and it opens no files, sockets, or processes.

**Numbers cross the bridge with a decimal point**, whatever the machine's locale; `Format-AiNumber` in *tools/ai.ps1* formats them that way, and `version` reports how the plugin itself prints 0.25.

## Before committing

```powershell
python .workspace\tools\housestyle.py --check FreeDistort+
python .workspace\tools\privacy.py --check --tracked FreeDistort+
```
