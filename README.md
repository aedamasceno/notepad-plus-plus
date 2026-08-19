# 🐧 Notepad++ Native Linux Port

### Experimental native Linux port of Notepad++ using Qt 6 and Scintilla

> No Wine required.  
> This project is under active development and is not yet production-ready.

## Why This Exists

As a longtime Notepad++ user and now a full-time Linux user, I was surprised that I couldn't find a native Linux editor that gave me everything I had grown accustomed to in Notepad++. I tried several alternatives, but there was always a feature missing or something that simply didn't work the way I was used to.

Since Notepad++ is open source, I decided to take on the challenge of porting it to Linux myself.

I want to be clear about credit: **Notepad++ and the work that makes this project possible belong to its original creators and contributors.** I did not create Notepad++. My goal is simply to bring the Notepad++ experience I know and love to Linux natively.

## Current Status

Working today:

- Native Qt 6 application
- Scintilla editor
- Multiple document tabs
- New / Open / Save
- Undo / Redo
- Cut / Copy / Paste
- Find / Replace
- Dirty-state tracking
- Tab close buttons
- Status bar
- Native Notepad++ toolbar icons

Still in progress:

- Remaining toolbar actions
- Advanced search
- Syntax highlighting integration
- Preferences
- Sessions
- Printing
- Dark mode
- Plugins
- Linux packaging and desktop integration

## Build

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux -j$(nproc)
./build-linux/npp_linux


## More Information

See [LINUX-PORT.md](LINUX-PORT.md) for the project overview, roadmap, contribution notes, and architecture details.

## Contributing

Contributions, testing, bug reports, and technical discussion are welcome.
