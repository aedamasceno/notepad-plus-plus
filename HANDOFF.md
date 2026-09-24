# Linux session recovery and document persistence handoff

## Repository state

- Canonical development branch: `linux-port`
- Batch starting HEAD: `646b4031d4ccdf4b608a8a2b1383bc11baaa6a49`
- Snapshot reactivation cleanup: `832d8b1f0a444fa722710187d0670a2c6d374dfe`
- Checkpoint status and failure-safe shutdown: `aaea384671f107dcc2b21e1daf0949476b436db5`
- Explicit recovery read results: `9d8fde1cb8d81a247cb797d52c3b2f5f0ae530d7`
- Recovered dirty save-point semantics: `ecb9e28d8a1580e0841fb724caedf247977e229c`
- Canonical/historical legacy migration: `07a0d95ecbec950dbd5d1ce756f97601939c6829`
- Independent-review follow-up: `5d2f19d92feb5b4d7552672cb9acaf2b888c064c`
- Final independent-review correction: `b9c46c63cc54a1d05bb04f372cec761ed1d134a2`
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
- Snapshot cleanup is based on the metadata that was successfully committed. New snapshots use immutable, content-addressed generations, so a snapshot write followed by a failed metadata commit cannot change the meaning of old committed metadata. A dirty→flush→clean→dirty reactivation cancels stale deletion state, and committed snapshot references are never deleted by cleanup.
- Checkpoints return typed status for durable/no-op, blocked, snapshot-write failure, and metadata-write failure. Failed snapshot or metadata writes remain pending for retry. Runtime failures appear in the status bar.
- Shutdown succeeds only after recovery is durable/no-op, an explicit Save All succeeds, or the user explicitly chooses **Exit and Abandon Recovery**. Retry repeats the checkpoint and Cancel keeps the application open.
- Recovery reads explicitly distinguish success (including a valid empty snapshot) from failure. Missing or unreadable snapshots restore as visibly dirty protected tabs, require an explicit Save/Discard/Cancel decision on tab close, and cannot be replaced by a blank checkpoint; Cancel retains the tab and metadata.
- Restored dirty named and untitled buffers remain logically dirty even when edit+undo reaches Scintilla's restore-time save point. Only a successful explicit Save/Save As clears this recovery-dirty state.
- Metadata that exists but cannot be opened, malformed/unsupported metadata, duplicate/invalid identities, and unmanaged snapshot paths produce diagnostics and put recovery in read-only mode for that run. This preserves the original metadata object and orphan snapshots rather than normalizing or deleting uncertain data. Missing managed snapshots remain represented and are not replaced with empty files during startup/shutdown.
- Snapshot paths loaded from metadata must match either the legacy SHA-256 document path or the current document-and-content SHA-256 generation path under the managed `snapshots/` directory, preventing absolute-path or `..` deletion attacks while retaining schema-2 compatibility.
- Legacy duplicate tab numbers are deduplicated only after a backup opens successfully, so a missing first candidate cannot suppress a later readable copy.

## Storage schema and location

Canonical storage remains:

```text
${QStandardPaths::AppDataLocation}/notepad++/sessions/
  session.json
  snapshots/<sha256(document-id)>-<sha256(content)>.snapshot
```

`AppDataLocation` includes the platform-specific organization/application component. The shipped Linux entry point now sets both organization and application to `TextPinnacle`, so canonical recovery data resolves under the TextPinnacle application-data prefix. Tests bypass that variability with `NPP_SESSION_DIR` and isolated `QSettings` directories.

On the first launch under the TextPinnacle identity, application settings are copied once from the former `Notepad++` / `Notepad++ Linux` QSettings store. Values already present in the TextPinnacle store are preserved, and a completion marker prevents later legacy changes from being re-imported. The legacy recovery-directory probes below remain unchanged for session compatibility.

`session.json` schema version 2 stores:

- `schemaVersion`
- `checkpointIntervalMs`
- `activeDocumentId`
- ordered `documents[]`, each with `id`, `filePath`, `untitledNumber`, `dirty`, relative `snapshot`, `originalExisted`, `originalSize`, `originalMtimeMs`, and `originalSha256`

Dirty documents use snapshots. Existing schema-2 metadata that references the earlier `snapshots/<sha256(document-id)>.snapshot` form remains readable. Clean named documents reload from their originals. Clean untitled tabs are retained as empty lifecycle entries.

### Legacy import

When canonical `session.json` is absent, recovery checks these prior/default candidates under `QStandardPaths::GenericDataLocation`:

```text
notepad++/sessions
npp_linux/notepad++/sessions
Notepad++/notepad++/sessions
Notepad++/Notepad++ Linux/notepad++/sessions
```

The legacy `untitledTabs` / `new_<number>.backup` format is imported into deterministic `legacy-untitled-<number>` identities. Duplicate tab numbers are deduplicated. Imported bytes are copied atomically into canonical snapshots; legacy metadata and backups are never modified or deleted. Malformed or incomplete legacy data is reported and retained.

The same unversioned `untitledTabs` schema is also recognized when it already occupies canonical `session.json`. Before current metadata can replace it, the exact legacy metadata is atomically preserved as `session.legacy.json`; the old backup files remain untouched. Versioned schemas other than schema 2 remain unsupported and write-blocked.

## Verification

Run from the repository root:

```sh
cmake -S . -B build-linux
cmake --build build-linux --clean-first -j1
ctest --test-dir build-linux --output-on-failure
QT_QPA_PLATFORM=offscreen ./build-linux/linux/linux_panel_tests
QT_QPA_PLATFORM=offscreen ./build-linux/linux/linux_recovery_tests
```

### Strict red-green evidence

Each slice added its regression before its production change. Commands used the focused target and executable:

```sh
cmake --build build-linux --target linux_recovery_tests -j2
QT_QPA_PLATFORM=offscreen ./build-linux/linux/linux_recovery_tests
```

Durable captured output:

- Slice 1: `/tmp/npp-recovery-slice1-red.txt` failed snapshot existence/latest-byte assertions; `/tmp/npp-recovery-slice1-green.txt` passed.
- Slice 2: `/tmp/npp-recovery-slice2-red.txt` captured the missing status API compile failure, `/tmp/npp-recovery-slice2-ui-red.txt` failed live visibility and close-cancel assertions, and `/tmp/npp-recovery-slice2-green.txt` passed blocked/snapshot/metadata/retry/UI coverage.
- Slice 3: `/tmp/npp-recovery-slice3-red.txt` captured the missing explicit read-result API compile failure; `/tmp/npp-recovery-slice3-green.txt` passed empty/unreadable and startup/shutdown preservation coverage.
- Slice 4: `/tmp/npp-recovery-slice4-red.txt` failed named and untitled edit→undo→restart assertions; `/tmp/npp-recovery-slice4-green.txt` passed.
- Slice 5: `/tmp/npp-recovery-slice5-red.txt` failed canonical and historical migration assertions; `/tmp/npp-recovery-slice5-green.txt` passed.
- Independent review issue 1: `/tmp/npp-review-1-red.txt` failed the missing-snapshot visible-dirty assertion; `/tmp/npp-review-1-green.txt` passed the focused recovery suite.
- Independent review issue 2: `/tmp/npp-review-2-red.txt` showed old committed metadata resolving to uncommitted new bytes after forced metadata failure; `/tmp/npp-review-2-green.txt` passed with immutable snapshot generations.
- Independent review issue 3: `/tmp/npp-review-3-red.txt` failed both later-readable legacy duplicate assertions; `/tmp/npp-review-3-green.txt` passed after successful-read deduplication.
- Final independent-review blocker: `/tmp/npp-review-final-red.txt` failed the write-block, rejected-update, and blocked-shutdown assertions when existing metadata could not be opened; `/tmp/npp-review-final-green.txt` passed after metadata open failure began blocking writes while preserving the existing metadata object.

### Current verification

Actual results for the current working tree:

- Configure and clean serial build: succeeded for `textpinnacle` and all eight test executables; the existing non-fatal missing CUPS development-files note remains.
- CTest: `8/8` passed, `0` failed, in 8.69 seconds.
- Focused panel executable: `All Linux panel tests passed`.
- Focused recovery executable: `All Linux recovery tests passed`; final-correction output is captured in `/tmp/npp-review-final-green.txt`.
- Focused failed-shutdown path (`--failed-shutdown-test`): passed.
- Isolated offscreen smoke: application remained running for 5 seconds.
- `git diff --check`: passed.
- The final independent-review blocker is addressed by `b9c46c63c` with a strict red-green regression using a `session.json` directory as a reliable `QFile` open-failure object while its parent remains writable. The prior three review issues remain addressed by `5d2f19d92`; no claim is made that the earlier review passed.
- A fresh independent review after `b9c46c63c` returned `PASS` with no remaining blocking/high data-loss finding and independently reran the focused recovery CTest successfully.

Recovery coverage uses disposable settings, standard-path, session, and file roots. It exercises exact Unicode restart for untitled and dirty named buffers; order, active selection, stable identities, and dirty state; Save, Save As, Discard, Cancel, Retry, and explicit failed-checkpoint abandonment; blocked/snapshot/metadata write failures; empty, missing, and unreadable snapshots; dirty reactivation; restored edit+undo semantics; failed atomic save preservation; missing/external originals; malformed/unsupported metadata and orphan retention; hostile snapshot paths; helper-process crash recovery after a real debounced MainWindow checkpoint; canonical legacy migration; and historical `npp_linux` discovery. Existing panel/replacement tests remain in the same CTest run.

## Known limitations / queued work

- Conflict handling is intentionally conservative: the recovered buffer opens with a status warning, but there is not yet a side-by-side diff/merge UI.
- Malformed or unsupported canonical metadata disables recovery writes for that process. The status bar reports why; repair or move the retained metadata before expecting new checkpoints.
- Legacy migration imports the repository's previous untitled-only schema. No older named-document schema existed to import.
- Function extraction remains heuristic, and Document Map remains a text-density overview.
- Printing, macro recording/playback, dual editor views, and synchronized scrolling remain queued.
