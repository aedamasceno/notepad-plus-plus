# Linux session recovery and document persistence handoff

## Repository state

- Branch: `linux-port-recovery`
- Batch starting HEAD: `25648d05b2907a673f0e58f901f83c0f683ef5f2`
- Recovery implementation and integration tests: `23f992221b0c5a90a2bd66f093462c56d201b037`
- No remote history was rewritten and nothing was pushed.
- The preserved untracked `demo.cpp` and `demo.py` were not modified or committed.

## Implemented behavior

- Every tab now has a UUID-backed stable identity. Session restoration preserves exact UTF-8 bytes, dirty state, tab order, active selection, untitled numbering, and distinct identities even when two recovered named tabs reference the same path.
- Recovery covers untitled buffers and dirty named files. Dirty named content is written only to a separate snapshot; the original is not touched until an explicit Save/Save As.
- Named documents record whether the original existed plus its byte size, millisecond mtime, and SHA-256 hash. Missing and externally changed originals retain the recovered snapshot and show a status-bar recovery warning instead of silently replacing the original.
- Normal application shutdown checkpoints all open documents and does not run close prompts or interpret shutdown as Discard. Explicit tab Close and Close All retain Save/Discard/Cancel semantics:
  - Cancel leaves the tab and recovery data intact.
  - Save/Save As atomically writes the chosen original, clears dirty recovery, and keeps the clean named tab in the session.
  - Discard removes that document from session metadata; its snapshot is removed only after replacement metadata commits.
- Ordinary saves, recovery snapshots, and session metadata use `QSaveFile`. Failed ordinary saves preserve existing bytes, leave the editor dirty, and display the write error.
- Recovery writes are debounced for 1000 ms. Text/lifecycle/layout changes schedule checkpoints; cursor movement and scrolling do not. Shutdown stops the timer and synchronously flushes snapshots before metadata.
- Malformed/unsupported metadata, duplicate/invalid identities, and unmanaged snapshot paths produce diagnostics and put recovery in read-only mode for that run. This preserves the original metadata and orphan snapshots rather than normalizing or deleting uncertain data. Missing managed snapshots remain represented and are not replaced with empty files during startup/shutdown.
- Snapshot paths loaded from metadata must exactly match the SHA-256-derived path under the managed `snapshots/` directory, preventing absolute-path or `..` deletion attacks.

## Storage schema and location

Canonical storage remains:

```text
${QStandardPaths::AppDataLocation}/notepad++/sessions/
  session.json
  snapshots/<sha256(document-id)>.snapshot
```

`AppDataLocation` includes the platform-specific organization/application component. The shipped Linux entry point sets organization `Notepad++` and application `Notepad++ Linux`; changing either changes the resolved prefix. Tests bypass that variability with `NPP_SESSION_DIR` and isolated `QSettings` directories.

`session.json` schema version 2 stores:

- `schemaVersion`
- `checkpointIntervalMs`
- `activeDocumentId`
- ordered `documents[]`, each with `id`, `filePath`, `untitledNumber`, `dirty`, relative `snapshot`, `originalExisted`, `originalSize`, `originalMtimeMs`, and `originalSha256`

Dirty documents use snapshots. Clean named documents reload from their originals. Clean untitled tabs are retained as empty lifecycle entries.

### Legacy import

When canonical `session.json` is absent, recovery checks these prior/default candidates under `QStandardPaths::GenericDataLocation`:

```text
notepad++/sessions
Notepad++/notepad++/sessions
Notepad++/Notepad++ Linux/notepad++/sessions
```

The legacy `untitledTabs` / `new_<number>.backup` format is imported into deterministic `legacy-untitled-<number>` identities. Duplicate tab numbers are deduplicated. Imported bytes are copied atomically into canonical snapshots; legacy metadata and backups are never modified or deleted. Malformed or incomplete legacy data is reported and retained.

## Verification

Run from the repository root:

```sh
cmake -S . -B build-linux
cmake --build build-linux --clean-first -j1
ctest --test-dir build-linux --output-on-failure
QT_QPA_PLATFORM=offscreen ./build-linux/linux/linux_panel_tests
QT_QPA_PLATFORM=offscreen ./build-linux/linux/linux_recovery_tests
```

Actual results for `23f992221`:

- Configure: succeeded; the existing non-fatal missing CUPS development-files note remains.
- Clean serial build: `npp_linux`, `linux_panel_tests`, and `linux_recovery_tests` built successfully.
- CTest: `2/2` passed, `0` failed, in 3.28 seconds.
- Focused panel executable: `All Linux panel tests passed`.
- Focused recovery executable: `All Linux recovery tests passed`.
- Isolated offscreen smoke: application remained running for 5 seconds.
- Independent final review: PASS after fixes for managed-path validation, malformed-metadata preservation, missing-snapshot lifecycle safety, and duplicate named-path restoration.
- Existing upstream warning remains: `scintilla/src/Editor.cxx` uses a C++20-deprecated implicit `this` capture.

Recovery coverage uses disposable settings/session/file roots and exercises exact Unicode restart for untitled and dirty named buffers; order, active selection, stable identities, and dirty state; Save, Save As, Discard, and Cancel; failed atomic save preservation; missing originals and snapshots; externally changed originals; malformed metadata and orphan retention; hostile snapshot paths; helper-process crash recovery after a real debounced MainWindow checkpoint; and non-destructive legacy migration. Existing panel/replacement tests remain in the same CTest run.

## Known limitations / queued work

- Conflict handling is intentionally conservative: the recovered buffer opens with a status warning, but there is not yet a side-by-side diff/merge UI.
- Malformed or unsupported canonical metadata disables recovery writes for that process. The status bar reports why; repair or move the retained metadata before expecting new checkpoints.
- Legacy migration imports the repository's previous untitled-only schema. No older named-document schema existed to import.
- Function extraction remains heuristic, and Document Map remains a text-density overview.
- Printing, macro recording/playback, dual editor views, and synchronized scrolling remain queued.
