# 🐧 Notepad++ Native Linux Port

# Notepad++ Native Linux Port

> Experimental native Linux port of Notepad++ using Qt 6, Scintilla, and Lexilla.

This project aims to bring the familiar Notepad++ editing experience to Linux
without requiring Wine.

**Current status: Alpha / Work in Progress**

This is an independent porting effort based on the open-source Notepad++ code.
Notepad++ itself and the original source code are the work of the Notepad++
project and its contributors.

## Current Features

- Native Linux executable
- Qt 6 interface
- Scintilla editor
- Lexilla syntax highlighting
- Multiple tabs
- Persistent unsaved notes
- Crash/restart recovery
- Open / Save / Save As / Save All
- Find / Replace
- Line numbers
- Word Wrap
- Zoom In / Zoom Out
- Show All Characters
- Indent Guides
- Notepad++ toolbar icons
- Fedora RPM package

## Build

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux -j$(nproc)
./build-linux/npp_linux


## More Information

See [LINUX-PORT.md](LINUX-PORT.md) for the project overview, roadmap, contribution notes, and architecture details.

## Contributing

Contributions, testing, bug reports, and technical discussion are welcome.
