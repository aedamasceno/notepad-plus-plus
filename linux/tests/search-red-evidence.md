# Search TDD RED evidence

Baseline: `fe4e5533c779e02d8ebcc95c8d53e5e4005c6cc0`.

- **Bulk-search performance:** building the new dense-document instrumentation test failed because `SearchWorkStats` and the diagnostic constructor did not exist. This established the bounded-work seam before implementation. A follow-up RED compile failed because `SearchWorkStats::columnBytesDecoded` did not exist; the completed test bounds cumulative Unicode-column decoding to one document and single-line preview construction to 500 bytes, detecting per-hit prefix/line rescans without wall-clock timing.
- **Asynchronous Find in Files:** building the lifecycle test failed with `startFileSearchInMainWindow was not declared in this scope`. The test covers immediate return/event-loop responsiveness, disabled/re-enabled control, latest-request stale suppression, completion, and destruction cancellation.
- **Scintilla regex grammar:** a detached baseline build with direct find/count/replace-all assertions produced six failures (`RED regex grammar uses Scintilla C++11 status`, `RED regex error propagates to count`, and `RED regex error propagates to replace-all`, for malformed grouping and unsupported lookbehind).
- **Coverage and file bounds:** building the encoding/aggregate-cap test failed because `FileSearchRequest::maximumTotalBytes` and `FileSearchResult::bytesScanned` did not exist. Activation/navigation, closed-document safety, and active-pane routing were added as characterization coverage around the changed workflow.
- **Unicode columns:** the detached baseline test failed `RED columns count Unicode code points`; a second RED test failed file-regex UTF-8 byte offsets after a supplementary code point.
- **History:** the focused test ran and failed `uncommitted editable search values survive dialog recreation` and `menu and F3 commands commit bounded deduplicated search history`.
