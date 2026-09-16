# Notepad++ Native Linux Port

## Bringing the Notepad++ experience natively to Linux

**Status: Alpha / Work in Progress**

This is an unofficial native Linux port of Notepad++ using Qt 6, Scintilla, and Lexilla. No Wine is required.

## Why I Started This Port

As a longtime Notepad++ user and now a full-time Linux user, I wanted the familiar Notepad++ workflow without having to run the Windows application through Wine.

Notepad++ is open source, so I decided to work on bringing the existing project to Linux natively. This is an independent porting effort; Notepad++ itself and the original source code are the work of the Notepad++ project and its contributors.

The goal is to preserve as much of the actual Notepad++ experience as practical while keeping the Linux-specific work maintainable against future upstream development.

## Current State

The Linux port builds and runs natively and already has a usable core editor. It is still under active development and is not yet ready to be considered feature-complete.

### Working

- Native Linux executable
- Qt 6 user interface
- Scintilla editing component
- Multiple document tabs
- New documents
- Open files
- Save / Save As / Save All
- Unsaved-document indicators
- Undo / Redo
- Cut / Copy / Paste
- Dynamic line numbers
- Word Wrap
- Zoom In / Zoom Out
- Show All Characters
- Indent Guides
- Notepad++ toolbar icons
- Tab close buttons
- Basic Notepad++-style menus and toolbar

### Working but still being expanded or validated

- Find / Replace
- Session persistence and unsaved-document recovery
- Lexilla integration and syntax highlighting
- Function List infrastructure
- File Browser infrastructure
- Document List infrastructure
- Printing support

### Not yet complete

- Document Map
- Macro recording / playback
- Synchronized scrolling
- Full language-selection workflow
- Complete encoding / EOL handling
- Preferences parity
- Plugin compatibility
- Broad Linux distribution packaging and testing

## Architecture

The Linux port currently uses:

- **Qt 6** for the native Linux user interface
- **Scintilla** for text editing
- **Lexilla** for lexer and syntax-highlighting support
- **CMake** for the Linux build system

Where practical, Linux-specific implementation is kept under `linux/` so that the port can continue following upstream Notepad++ without unnecessarily modifying the shared source tree.

## Building on Linux

The current development environment uses Qt 6, CMake, Ninja, and a C++17 compiler.

From the repository root:

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux -j$(nproc)
```

Run the application with:

```bash
./build-linux/npp_linux
```

## Current Development Priorities

The main priorities are:

1. Complete the dockable panels and connect them to real editor/document state.
2. Complete printing support.
3. Expand search and replace functionality.
4. Implement Document Map, macros, and synchronized scrolling.
5. Improve Lexilla/language integration.
6. Complete encoding and line-ending handling.
7. Improve session persistence and tab/document workflows.
8. Improve packaging and Linux desktop integration.
9. Keep the Linux branch synchronized with upstream Notepad++ development.

## Contributing

Contributions, testing, bug reports, and technical discussion are welcome.

Help is particularly useful in these areas:

- Qt 6 and C++ development
- Scintilla / Lexilla integration
- Notepad++ feature parity
- Linux packaging
- Fedora / Debian / Ubuntu and other distribution testing
- C++ code review
- documentation

If you would like to help, open an issue or submit a pull request.

## Credits

Notepad++ and its original source code are the work of the Notepad++ project and its contributors.

This project is an independent effort to port that existing work to native Linux. Credit for Notepad++ itself belongs to its original creators and contributors.
