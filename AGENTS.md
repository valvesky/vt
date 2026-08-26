# Agent contract

Godstack lives in `godstack/` (git submodule). Contract is `godstack/AGENTS.md`.
From this root: `-I godstack/Peak -I godstack/Rend -I godstack/Term`.
Peak before Rend. Include `term.h` then `term.c`, then `rend.h` / `peak.c` /
`rend.c`. `PEAK_VULKAN` is on the compile line.
Rend is Vulkan 1.4 only. It does not compile shaders.

Header is a black box. Read `foo.h`. Call it. Do not open a library `.c` unless
you are changing that library, or the header was used correctly and the process
still dies.

Skills (algorithm, C style, debug, commits): `godstack/AGENTS.md`. It points at
`godstack/skills/`. Do not restate them here.

## Product

Standalone Linux GPU terminal. Parser is Godstack `Term`. Single process,
no threads. Config is compile-time (`config.h`). Child is `bash --login`
on a PTY with `TERM=xterm-256color`.

Three file-scope objects:

| Object      | File           | Role |
|-------------|----------------|------|
| `Term term` | `vt.c`         | cell grid via `term_feed` |
| `PeakWindow win` | `vt_renderer.c` | window + epoll |
| `Renderer renderer` | `vt_renderer.c` | Rend handles + CPU glyph buffer |

Work in the named file. Default: `vt.c` / `vt.h`. No new module unless asked.
Fuse stays out.

## Architecture

The code as it exists. Not a proposal, not a complete VT. Read this before
changing the parser, the screen, or the renderer.

Layers, bottom to top:

- Peak window and its X fd
- memfd ring (two `MAP_FIXED` views; wrap is one memcpy)
- SSE2 split on `\n` / ESC / high bit
- `term_feed` / `term_feed_ascii` into the live cell grid
- CPU atlas + instanced quads

Byte path: PTY master → `CBuffer` → SSE2 split → `term_feed` / `term_feed_ascii`
→ live `TermScreen` → instanced quads.

Opposite edge: Peak keys → `vt_sh_write` → PTY. Agents do not scrape the PTY.

Wait: `poll` on the PTY, the Peak X fd, the ctl listen socket, clients, and one
job pipe. Sleeps (`timeout -1`) when the grid is unchanged. `XPending` forces a
0 timeout so the X fd cannot spin. Present only on PTY bytes or resize.

Control socket: `$XDG_RUNTIME_DIR/vt/<pid>.sock`, else `/tmp/vt-<uid>/<pid>.sock`.
JSONL, one LF-terminated record per message. Works in a windowed session or
`--headless --live`.

| Op | What it does |
|----|----------------|
| `dump` | UTF-8 cell grid; same text as file `--headless` |
| `cursor` | cursor position |
| `size` | cols / rows |
| `write` | `write.data` is raw PTY bytes |
| `run` | one off-grid `bash -c` in the login shell's cwd; stdout+stderr stay off the grid; reply `{ev:exit,job,code,out}` when it reaps |
| `screenshot` | `screenshot.path` is a CPU-atlas P6 PPM |

Headless modes:

- `--headless [file] [--screenshot out.ppm] [--cols N] [--rows N]` — ingest
  through the ring (no Peak, no Vulkan, no PTY). UTF-8 dump, optional PPM.
  Default 80x24.
- `--headless --live [--cols N] [--rows N]` — PTY + ctl, still no Peak/Vulkan.

Display is the live cell grid (primary or alt). UTF-8 decodes into cell scalars.
Atlas bakes ASCII 32..127 and rasterizes other codepoints on miss. Box drawing
is CPU-stroked so lines join. SGR 256/truecolor, reverse, underline.

Keys: Peak covers digits, tab, Backspace, Delete. Printable punctuation comes
from `XLookupString`.

Wheel: on primary, walks Term hist (lines that left the top). On the alt screen,
CSI A/B into the PTY.

## Files

```
vt.c              unity root. Includes term.c, rend.c, vt_circ_buf.c, vt_renderer.c.
vt.h              types. Architecture lives in this file.
vt_circ_buf.c     memfd + two MAP_FIXED views. Wrap is one memcpy.
vt_renderer.c     Peak + Rend + stb atlas. Incomplete Renderer in vt.h.
vt_debug.h        VT* logs to `log` (path in config.h).
config.h          font path/size, ANSI palettes. Edit this, do not add a rc file.
build.c           Poof. glslc then gcc vt.c -o vt.
vulkan/vt.{vert,frag}  glslc writes gitignored .spv
lib/stb_truetype.h
fonts/            TTF at config.h path. Gitignored. Binary will not start without it.
tests/glyph.txt   cat-able fixture (UTF-8, box drawing, CUP)
tests/glyph.ok    expected --headless dump
tests/check       cmp dump against .ok; tests/tui
tests/tui         nvim, lf, ncmpcpp, pi via --headless --live; PPM cmp if golden present
tests/golden/     fixtures; *.ppm gitignored
```

One gcc invocation. One binary `vt`.

## Build

```
gcc -o build build.c   # once
./build                # release: glslc + gcc -O2 vt.c -o vt
./build debug          # -g -DDEBUG -O0
./build test           # current mode, then tests/check
sudo ./build install   # release, then /usr/bin/vt and /usr/share/vt/
```

Headless ingest (no Peak/Vulkan/PTY): `./vt --headless tests/glyph.txt` prints the
cell grid as UTF-8 to stdout. `cat tests/glyph.txt` is the same bytes in a
real terminal. Golden: `tests/glyph.ok`.
`--screenshot out.ppm` composites the CPU atlas (shader mix) to a P6 PPM.
`--headless --live [--cols N] [--rows N]` is PTY + ctl, no Peak. Default 80x24.

Needs Vulkan, `glslc`, `-lutil` (openpty). Do not invoke gcc on `vt.c` by hand
unless you match `build.c` (`-I godstack/Peak -I godstack/Rend -I godstack/Term -DPEAK_VULKAN`).

## Reads

`rg` first. `read` with offset/limit. Never dump `godstack/**/*.c` to understand
an API. Never dump `log` or `atlas.pgm`.

## C

C99. Unity build. Main file includes `.c` files. Those `.c` files have
`#pragma once`. Integer typedefs, `MIN`/`MAX`/`BETWEEN` live in `vt.h`.
Logs go through `vt_debug.h`.
