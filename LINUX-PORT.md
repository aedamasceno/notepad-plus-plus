# Notepad++ Native Linux Port

### Bringing the Notepad++ experience natively to Linux

**Experimental / Work in Progress**

A native Linux port of Notepad++ using Qt 6 and Scintilla — no Wine required.

---

## Why I Started This Port

As a longtime Notepad++ user and now a full-time Linux user, I was surprised that I couldn't find a native Linux editor that gave me everything I had grown accustomed to in Notepad++. I tried several alternatives, but there was always a feature missing or something that simply didn't work the way I was used to.

Since Notepad++ is open source, I decided to take on the challenge of porting it to Linux myself. I knew this would be a significant undertaking, but it has been a fun and interesting project so far, and I'm looking forward to seeing how far we can take it.

I want to be very clear about credit: **Notepad++ and the work that makes this project possible belong to its original creators and contributors.** I did not create Notepad++. My goal is simply to bring the Notepad++ experience I know and love to Linux natively.

If other developers, testers, or Notepad++ users would like to help with the Linux port, contributions are welcome.

---

## Current Status

The project is under active development and is **not yet ready for production use**.

### Working

- Native Linux executable
- Qt 6 user interface
- Scintilla editing component
- Multiple document tabs
- New documents
- Open files
- Save files
- Unsaved document indicators
- Undo / Redo
- Cut / Copy / Paste
- Find
- Replace
- Notepad++ toolbar icons
- Tab close buttons
- Basic Notepad++-style menus and toolbar

### In Progress / Not Yet Implemented

A significant amount of Notepad++ functionality still needs to be ported or implemented, including:

- Additional search and replace functionality
- Session persistence and restoration
- Preferences
- Syntax highlighting integration
- Language support
- Dark mode
- Printing
- Remaining toolbar commands
- Plugins / plugin compatibility
- Linux desktop integration
- Packaging and installation

## Building on Linux

The current development environment uses Qt 6, CMake, Ninja, and a C++17 compiler.

From the repository root:

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux -j$(nproc)

Run the application with:
./build-linux/npp_linux


