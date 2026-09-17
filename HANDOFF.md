# Linux panel batch handoff

## Repository state

- Branch: `linux-port-recovery`
- Starting HEAD: `8a6884b5675234fcd76236edbee950eb0bcf88ef`
- Panel implementation: `b4902190142f9673f067784ddb027d1d8c5f7759`
- Original verification handoff: `cfde49cc624f31d0e0cdcd7f745a6086d3f6c3fd`
- Explicit-save safety: `721802225d6003b0d811cd987966e5ce91bb8d51`
- Panel review corrections and behavioral tests: `f75bdd31ef86dd1d6c387f88de25b47f1ef735b4`
- No remote history was rewritten and nothing was pushed.
- After verification, the only working-tree entries before this handoff update were the preserved untracked `demo.cpp` and `demo.py`.

## Implemented

- `replaceAll` loops over `SCI_SEARCHINTARGET`/`SCI_REPLACETARGET`, uses UTF-8 byte lengths, honors Scintilla search flags, and advances past inserted bytes so replacements containing the search text terminate.
- Tab closing has one prompt path. Saving receives the tab being closed, background tabs write their own editor bytes, untitled tabs use Save As, and Save As cancellation keeps the tab open.
- Function List, Document List, Document Map, and File Browser are enabled dock panels with explicit signals/data APIs.
  - Document List shows real tab names, paths/tooltips, dirty state, active selection, activates tabs, and preserves its items when document metadata is unchanged.
  - Function List parses the live buffer for common C/C++/Java/JavaScript/Python forms, including unsaved buffers, and navigates by line.
  - Document Map uses proportional line/pixel helpers for painting, viewport highlighting, and clicks. Long documents aggregate line density per pixel row instead of collapsing lines at the bottom.
  - File Browser supports root selection, parent navigation, directory browsing, and file activation.
- Wrapped and folded editor viewports are converted from Scintilla display lines to document-line ranges with `SCI_DOCLINEFROMVISIBLE` before updating Document Map.
- Panel content and viewport refreshes are separate. Only insert/delete `SCN_MODIFIED` notifications schedule the 100 ms content debounce; scrolling updates only the viewport. Hidden content panels refresh from the current editor when reopened.
- Main-window orchestration was extracted behind the small `createMainWindow` seam so focused tests exercise real tab/panel/save wiring without duplicating it.
- Docks have stable object names. Checkable actions follow dock visibility. `QSettings` persists window geometry, dock layout, visibility, and File Browser root.
- The repository-root CMake entry delegates to `linux/`, which owns WidgetGen generation, Linux sources, tests, and install rules.

## Verification

Run from the repository root:

```sh
cmake -S . -B build-linux
cmake --build build-linux --clean-first -j1
ctest --test-dir build-linux --output-on-failure
QT_QPA_PLATFORM=offscreen ./build-linux/linux/linux_panel_tests
```

Actual results for review commit `f75bdd31e`:

- configure: succeeded; non-fatal CUPS development-files note remains
- clean serial build: `npp_linux`, `npp_linux_app`, and `linux_panel_tests` built successfully
- CTest: `1/1` passed in 1.34 seconds
- focused executable: `All Linux panel tests passed`
- isolated offscreen smoke: application remained running for 5 seconds
- upstream warning remains: `scintilla/src/Editor.cxx` uses a C++20-deprecated implicit `this` capture

Focused coverage includes replacement edge cases, exact editor writes, live function parsing, proportional map coordinates and 10,000-lines-to-100-pixels aggregation, wrapped/folded viewport conversion, Document List activation and unchanged-item preservation, File Browser activation, debounced content updates, scroll-only viewport updates, hidden-panel refresh, real background-tab close/save, and Save As cancellation preserving a dirty untitled tab. Tests use disposable files and isolated `QSettings` paths.

## Known limitations / queued work

- Recovery remains intentionally unchanged in this batch. Two pre-existing defects are still present:
  - `DocumentTab::m_shouldRegisterWithSessionManager` is initialized to `false` and is never enabled, so the intended first-modification registration path cannot run.
  - `MainWindow::loadSession()` recreates untitled tabs but does not restore their saved backup contents.
- Function extraction is heuristic (regular-expression based), not a full language parser; complex multiline declarations may be omitted.
- Document Map is a text-density overview, not syntax-colored rendering.
- Printing remains disabled/queued; the existing demonstration code is not a completed print implementation.
- Macro recording/playback remains disabled/queued.
- Dual editor views and synchronized scrolling remain disabled/queued.
