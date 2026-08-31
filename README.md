<!-- LOGO -->
<h1>
<p align="center">
  <img src="vt.gif" alt="Logo" width="128">
  <br>vt
</h1>
  <p align="center">
     Hello world! I'm VT!
    <br />
     I'm a determined, lovable, hackable terminal built to survive!
    <br />
    <br />
    <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue?style=flat-square" alt="License: MIT"></a>
    <a href="https://github.com/valvesky/vt"><img src="https://img.shields.io/github/languages/top/valvesky/vt?style=flat-square" alt="Language"></a>
    <a href="https://www.khronos.org/vulkan/"><img src="https://img.shields.io/badge/Vulkan-1.4-red?style=flat-square&logo=vulkan&logoColor=white" alt="Vulkan 1.4"></a>
    <a href="https://github.com/valvesky/vt/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/valvesky/vt/ci.yml?style=flat-square" alt="CI"></a>
    <img src="https://img.shields.io/badge/Linux-FCC624?style=flat-square&logo=linux&logoColor=black" alt="Linux">
    <img src="https://img.shields.io/badge/Windows-0078D4?style=flat-square&logo=windows&logoColor=white" alt="Windows">
    <img src="https://img.shields.io/badge/macOS-000000?style=flat-square&logo=apple&logoColor=white" alt="macOS">
    <a href="https://github.com/valvesky/vt/commits/master"><img src="https://img.shields.io/github/last-commit/valvesky/vt?style=flat-square" alt="Last commit"></a>
    <a href="https://github.com/valvesky/vt/stargazers"><img src="https://img.shields.io/github/stars/valvesky/vt?style=flat-square" alt="Stars"></a>
    <br />
    <br />
    <a href="#features">Features</a>
    ·
    <a href="#supported-features">Supported</a>
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

## 😎 About

I was built for people who need a terminal to work on for the rest of their lives.
I'm fast, have all the features you probably need out-of-the-box and can be hacked 
with anything else you may need. I'm here to help!

## Main Features
- **I will survive!** Build me and I will survive. I promise to make the most of whatever hardware you give me! I'm not giving up on 5% of machines.
    - Currently I feel coziest on Linux. I'm working hard to support every feature I can on every listed platform. 
- **Build your own VT!** See the patches folder for optional or just cool features! No rc file, no plugin ABI.
    - **It's easy!** If you don't feel like hacking today, agents have instructions on applying patches, resolving merge conflicts and testing!
- **Super Fast!** Hardware is my bro 🤝
    - SIMD, circular buffers, kernel page mapping & all other sorts of black magic!
- **Multiplexing:** My Ctrl-b prefix. `%`/`|` split vertical, `"`/`-` split horizontal, hjkl/arrows focus, `o` next, `x` kill. Middle-drag a live pane; drop on an edge to split, center to swap. X11: release on another vt adopts that PTY (last donor pane exits). Else: middle-press source, then MMB dest. Shift+middle still pastes. ctl `split` / `focus` / `panes` / `move` / `adopt` / `give`. I have no sessions.

## Extras
- **File drop:** Drag and drop files to get the URL!
- **Kitty graphics:** APC `ESC _ G`. Direct PNG. `a=q` replies so icat can detect me.
- **Headless Mode:** I emulate a terminal without opening a window.
- **JSONL Socket:** Talk to my live grid. You can even get a screenshot!
- **Source is the config:** knobs in `config.h`. 

## Supported Features

| | X11 | Wayland | macOS | Windows |
| :--- | :---: | :---: | :---: | :---: |
| Vulkan | ✅ | ✅ | ✅ | ✅ |
| CPU raster | ✅ | ✅ | ✅ | ✅ |
| Panes (Ctrl-b) | ✅ | ✅ | ✅ | ✅ |
| Middle-drag split / swap | ✅ | ✅ | ✅ | ✅ |
| Pane drop (release on dest) | ✅ | ❌ | ❌ | ❌ |
| Pane drop (two-click) | ✅ | ✅ | ✅ | ❌ |
| File drop | ✅ | ❌ | ✅ | ✅ |
| Kitty graphics | ✅ | ✅ | ✅ | ✅ |
| ctl JSONL | ✅ | ✅ | ✅ | ✅ |
| Headless / `vt-live` | ✅ | ✅ | ✅ | ✅ |

Why a cell is empty: [`docs/features.md`](docs/features.md).

## Optional Features

Themes, undercurl, CRT, sixel: `patch -p1 < patches/vt-<version>-<name>.diff`.

## Build

```
git clone --recursive https://github.com/valvesky/vt.git
cd vt
./deps                 # packages + font (sudo if needed)
gcc -o build build.c   # once
./build                # release windowed: slangc + gcc -O2 src/main.c -o vt
```

Linux compile needs X11, Wayland, and Vulkan headers. `./deps` installs them. Peak still `dlopen`s the libs at run.
Windowed GPU also needs `slangc` and a Vulkan loader (or `./build cpu`). `-lutil` (`openpty`). TTF at the `config.h` path.
No `slangc` (Debian 11): `./build cpu` or `./build headless`.

```
./build debug          # -g -DDEBUG -O0
./build cpu            # no Vulkan; Rend CPU raster
./build headless       # vt-headless + vt-live (no window / Vulkan)
./build test           # current mode, ensures headless bins, then tests/check
./build deps           # same as ./deps, once build.c is compiled
sudo ./build install   # deps, then /usr/bin/vt and /usr/share/vt/
```

Don't worry, my `build` binary will rebuild itself if changes are made to it.

## Headless binaries

Separate apps — I have no `--headless` flag on `vt`.

```
./vt-headless tests/glyph.txt
./vt-headless tests/glyph.txt --screenshot out.ppm
./vt-headless --dump-runs tests/runs.bin
./vt-live [--cols N] [--rows N]
```
- `vt-headless` ingests a file (or stdin) and I print the cell grid as UTF-8.
- `--screenshot` renders my grid to a PPM image.
- `vt-live` is my PTY + control socket, no window. Default size is 80x24.

## Control socket

JSONL. My path is `$XDG_RUNTIME_DIR/vt/<pid>.sock` otherwise `/tmp/vt-<uid>/<pid>.sock`.

Protocol for agents: [`docs/ctl.md`](docs/ctl.md).

## Shoutouts

- [refterm](https://github.com/cmuratori/refterm) — how to black magic
- [kitty](https://sw.kovidgoyal.net/kitty/) — how to meow
- [ghostty](https://ghostty.org) — how to render glyph good
- [pi](https://github.com/earendil-works/pi) - how to agent

## License

- [MIT LICENSE](LICENSE).

## What does VT mean?


Very tastty? Vasco's Terminal?
Virtual terminal?


Believe in the terminal that believes in itself!
