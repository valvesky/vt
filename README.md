<!-- LOGO -->
<h1>
<p align="center">
  <img src="vt.gif" alt="Logo" width="128">
  <br>vt
</h1>
  <p align="center">
     Highly customizable self-modifying cross-platform terminal where each fork is unique and your own.
    <br />
     It didn't exist yet so I had to make one.
    <br />
    <br />
    <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue?style=flat-square" alt="License: MIT"></a>
    <a href="https://github.com/valvesky/vt"><img src="https://img.shields.io/github/languages/top/valvesky/vt?style=flat-square" alt="Language"></a>
    <a href="https://www.khronos.org/vulkan/"><img src="https://img.shields.io/badge/Vulkan-1.4-red?style=flat-square&logo=vulkan&logoColor=white" alt="Vulkan 1.4"></a>
    <a href="https://github.com/valvesky/vt/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/valvesky/vt/ci.yml?style=flat-square" alt="CI"></a>
    <img src="https://img.shields.io/badge/Linux-FCC624?style=flat-square&logo=linux&logoColor=black" alt="Linux">
    <img src="https://img.shields.io/badge/Windows-0078D4?style=flat-square&logo=windows&logoColor=white" alt="Windows">
    <img src="https://img.shields.io/badge/macOS-000000?style=flat-square&logo=apple&logoColor=white" alt="macOS">
    <img src="https://img.shields.io/badge/X11-000000?style=flat-square&logo=xdotorg&logoColor=white" alt="X11">
    <a href="https://github.com/valvesky/vt/commits/master"><img src="https://img.shields.io/github/last-commit/valvesky/vt?style=flat-square" alt="Last commit"></a>
    <a href="https://github.com/valvesky/vt/stargazers"><img src="https://img.shields.io/github/stars/valvesky/vt?style=flat-square" alt="Stars"></a>
    <br />
    <br />
    <a href="#features">Features</a>
    ·
    <a href="#about">About</a>
    ·
    <a href="#build">Build</a>
    ·
    <a href="#control-socket">Control</a>
    ·
    <a href="#license">License</a>
    ·
    <a href="#shoutouts">Shoutouts</a>
  </p>
</p>

## Features
- **Hardware-accelerated or NOT**: Have the latest hardware? Great! Don't? Great!
- **Cross-platform:** First-class Linux and Windows support. MacOS support also first class, I just don't own one.
- **Self-modifying:** If you have an Agent on your system, it can modify not just configuration but the **source code** via pre-built patches!
- **Highly customizable:** Every copy is your own. Apply patches manually or speed up the process with agents!
- **Fast:** SIMD, circular buffers that use kernel page mapping & all other sorts of black magic!
- **Headless Mode:** Emulate a terminal without opening a window.
- **JSONL Socket:** Talk to a headless window. You can even get a screenshot.

## Optional Features

Features that not everyone wants built in but that can be added easily.

- Multiplexing: if you want it use TMUX, see patches or just ask an agent.

## About

VT is a highly customizable cross-platform terminal where each installation is unique and your own.

Modifications are made by editing the source code, namely `config.h` and recompiling the program.

You can install plugins by applying patches (`.diff` files in the `patches` folder).
Feel freat to contribute to the patches as well or to share your own forks. 
And, don't worry, every single line of code is MIT licensed.

To facilitate changes, agents can follow `AGENTS.md` and read `docs/` on demand.

## Build

Needs Vulkan (or `./build cpu`), `slangc`, and `-lutil` (`openpty`).
And a valid TTF at the path in `config.h` (default is may not be on your system).

```
gcc -o build build.c   # once
./build                # release windowed: slangc + gcc -O2 src/main.c -o vt
./build debug          # -g -DDEBUG -O0
./build cpu            # no Vulkan; Rend CPU raster
./build headless       # vt-headless + vt-live (no window / Vulkan)
./build test           # current mode, ensures headless bins, then tests/check
sudo ./build install   # /usr/bin/vt and /usr/share/vt/
```

Don't worry, the `build` binary will rebuild itself if changes are made to it.

## Headless binaries

Separate apps — no `--headless` flag on `vt`.

```
./vt-headless tests/glyph.txt
./vt-headless tests/glyph.txt --screenshot out.ppm
./vt-headless --dump-runs tests/runs.bin
./vt-live [--cols N] [--rows N]
```
- `vt-headless` ingests a file (or stdin) and prints the cell grid as UTF-8.
- `--screenshot` renders the grid to a PPM image.
- `vt-live` is PTY + control socket, no window. Default size is 80x24.

## Control socket

JSONL. Path is `$XDG_RUNTIME_DIR/vt/<pid>.sock` otherwise `/tmp/vt-<uid>/<pid>.sock`.

Protocol for agents: [`docs/ctl.md`](docs/ctl.md).

## Shoutouts

- [refterm](https://github.com/cmuratori/refterm) — how to black magic
- [st](https://st.suckless.org) — how to suck less
- [kitty](https://sw.kovidgoyal.net/kitty/) — how to meow
- [ghostty](https://ghostty.org) — how to render glyth good
- [pi](https://github.com/earendil-works/pi) - how to agent

## License

- [MIT LICENSE](LICENSE).

## What does VT mean?

Very tastty? Vasco's Terminal?
Virtual terminal?
