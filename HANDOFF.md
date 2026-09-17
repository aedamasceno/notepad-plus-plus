# Linux panel batch handoff

## Implemented

- `replaceAll` now loops over `SCI_SEARCHINTARGET`/`SCI_REPLACETARGET`, uses UTF-8 byte lengths, honors Scintilla search flags, and advances past inserted bytes so replacements containing the search text terminate.
- Tab closing has one prompt path. Saving always receives the tab being closed, background tabs write their own editor bytes, untitled tabs use Save As, and Save As cancellation keeps the tab open.
- Function List, Document List, Document Map, and File Browser are enabled dock panels with explicit signals/data APIs; no panel reaches into `MainWindow` internals.
  - Document List shows real tab names, paths/tooltips, dirty state, active selection, and activates tabs.
  - Function List parses the live buffer for common C/C++/Java/JavaScript/Python forms, including unsaved buffers, and navigates by line.
  - Document Map is a custom-painted live text overview with viewport highlighting and click-to-line navigation. The former simulated `QLabel` implementation and slot are removed.
  - File Browser supports root selection, parent navigation, directory browsing, and file activation.
- Panel updates are connected to tab creation/switching/closing, title/dirty changes, editor notifications, and vertical scrolling. Panels refresh immediately when opened.
- Docks have stable object names. Checkable actions follow dock visibility. `QSettings` persists window geometry, dock layout, visibility, and File Browser root.
- The repository-root CMake entry delegates to `linux/`, which owns WidgetGen generation, Linux sources (including Document Map), tests, and install rules.

## Verification

Run from the repository root:

```sh
cmake -S . -B build-linux
cmake --build build-linux --clean-first -j1
ctest --test-dir build-linux --output-on-failure
QT_QPA_PLATFORM=offscreen ./build-linux/linux/linux_panel_tests
```

Results:

- clean build: `npp_linux` and `linux_panel_tests` built successfully
- CTest: `1/1` passed
- focused executable: `All Linux panel tests passed`
- offscreen smoke: application remained running for 5 seconds
- non-fatal configure note: CUPS development files were not installed; Qt PrintSupport still configured
- upstream warning: `scintilla/src/Editor.cxx` uses a C++20-deprecated implicit `this` capture

Focused tests cover multiple matches, no match, Unicode, self-containing replacement, whole-word search options, exact writes from two distinct editors, and live function parsing for C++, Java, JavaScript, Python, and unsaved Python.

## Limitations / queued work

- Function extraction is intentionally heuristic (regular-expression based), not a full language parser; complex multiline declarations may be omitted.
- Document Map is a text-density overview, not syntax-colored rendering.
- Printing remains disabled/queued; the existing demonstration code is not a completed print implementation.
- Macro recording/playback remains disabled/queued.
- Dual editor views and synchronized scrolling remain disabled/queued.
