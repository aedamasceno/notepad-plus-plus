# Contributing to the native Linux port

This is an independent, unofficial port of Notepad++ using Qt 6, Scintilla, and Lexilla. The project is in alpha; full feature parity and Windows plugin compatibility are not available.

## Where to participate

Report Linux-port bugs and propose changes in [this repository](https://github.com/aedamasceno/textpinnacle/issues). Please do not send port-specific reports to the upstream Windows project's tracker.

The existing CONTRIBUTING.md contains upstream contribution guidance. For changes to this Linux fork, use this guide and target the `linux-port` branch.

## Try the alpha

Visit [Releases](https://github.com/aedamasceno/textpinnacle/releases) for the published Fedora RPM and installation instructions. The published package was built and tested on Fedora 44 x86_64. Other distributions need separate validation. Test with disposable files or copies of documents.

A release and the latest development branch can have different behavior. Include your release version or commit hash in reports.

## Useful bug reports

Include your distribution/version, architecture, desktop environment, Wayland or X11 session, installation method, and application version or commit. Give reproduction steps, expected behavior, and actual behavior. For build failures, include the command and first relevant error. Attach a small non-sensitive example or screenshot when helpful.

## Ways to help

- Test editing, saving, search, and session recovery with disposable documents.
- Improve Qt interfaces and Scintilla/Lexilla integration.
- Reproduce reported bugs and document precise steps.
- Improve Linux build instructions, packaging, accessibility, and documentation.
- Validate additional distributions and report the exact environment and result.

See the [README status table](README.md#project-status) for current feature status. Features under development should not be described as complete based only on compilation.

## Submit a change

1. Fork this repository and create a branch from `linux-port`.
2. Keep the change focused and preserve upstream credits and licensing notices.
3. Follow nearby code conventions and keep Linux-specific integration under `linux/` where practical.
4. Follow the build commands in README.md and test the affected workflow.
5. Open a pull request against this repository's `linux-port` branch. Explain the problem, changes, actual checks performed, and limitations.

Avoid committing build output, generated editor bindings, personal settings, or unrelated changes. For substantial architecture work, open an issue first so contributors can coordinate.
