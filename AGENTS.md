# Agent contract

Godstack lives in `godstack/` (git submodule). Contract is `godstack/AGENTS.md`.
From this root: `-I godstack/Peak -I godstack/Rend -I godstack/Term`.
Peak before Rend. Include `term.h` then `term.c`, then `rend.h` / `peak.c` /
`rend.c`. `PEAK_VULKAN` is on the compile line. It does not compile shaders.

Skills (algorithm, C style, debug, commits): `godstack/AGENTS.md`. It points at
`godstack/skills/`. Do not restate them here.

## Fork

This tree is the config. There is no plugin ABI, no rc file, no dlopen.
Edit the C and rebuild. Default files: `src/vt.c` / `src/vt.h`. Knobs:
`config.h`. Optional features are `.diff` files in `patches/`
(`vt-<version>-<patch_name>`). Apply, then ask the user to rebuild. No
new module unless asked. Fuse stays out.

## Product

Standalone Linux GPU terminal. Parser is Godstack `Term`. Single process,
no threads. Child is `bash --login` on a PTY with `TERM=xterm-256color`.

Three file-scope objects:

| Object      | File           | Role |
|-------------|----------------|------|
| `Term term` | `src/vt.c`         | cell grid via `term_feed` |
| `PeakWindow win` | `src/vt_renderer.c` | window + epoll |
| `Renderer renderer` | `src/vt_renderer.c` | Rend handles + CPU glyph buffer |

Work in the named file.

## Docs (read on demand)

When asked about a topic, read the file completely and follow links.
Do not dump `godstack/**/*.c`, `log`, or `atlas.pgm`.

| Topic | File |
|-------|------|
| product / UX | `README.md` |
| agent contract | `AGENTS.md` (this file) |
| patches | `docs/patches.md` |
| ctl protocol | `docs/ctl.md` |
| renderer | `docs/renderer.md` |
| parser / grid | `docs/term.md` |
| tests | `docs/tests.md` |

The code as it exists. Not a proposal, not a complete VT.

## Files

```
src/vt.c            unity root. Includes term.c, rend.c, vt_circ_buf.c, vt_renderer.c.
src/vt.h            types.
src/vt_circ_buf.c   memfd + two MAP_FIXED views. Wrap is one memcpy.
src/vt_renderer.c   Peak + Rend + stb atlas. Incomplete Renderer in vt.h.
src/vt_debug.h      VT* logs to `log` (path in config.h).
config.h            font path/size, ANSI palettes. Edit this, do not add a rc file.
build.c             Poof. glslc then gcc src/vt.c -o vt.
docs/               on-demand agent docs. Index is the table above.
patches/            optional `.diff`. Name `vt-<version>-<patch_name>`. See docs/patches.md.
vulkan/vt.{vert,frag}  glslc writes gitignored .spv
lib/stb_truetype.h
fonts/              TTF at config.h path. Gitignored. Binary will not start without it.
tests/glyph.txt     cat-able fixture (UTF-8, box drawing, CUP)
tests/glyph.ok      expected --headless dump
tests/check         cmp dump against .ok; tests/tui
tests/tui           nvim, lf, ncmpcpp, pi via --headless --live; PPM cmp if golden present
tests/golden/       fixtures; *.ppm gitignored
```

One gcc invocation. One binary `vt`.

## Build

```
gcc -o build build.c   # once
./build                # release: glslc + gcc -O2 src/vt.c -o vt
./build debug          # -g -DDEBUG -O0
./build test           # current mode, then tests/check
sudo ./build install   # release, then /usr/bin/vt and /usr/share/vt/
```

Needs Vulkan, `glslc`, `-lutil` (openpty). Do not invoke gcc on `src/vt.c` by
hand unless you match `build.c` (`-I . -I godstack/Peak -I godstack/Rend
-I godstack/Term -DPEAK_VULKAN`).

Headless: `./vt --headless tests/glyph.txt`. Live ctl, no Peak/Vulkan:
`--headless --live [--cols N] [--rows N]`. Default 80x24.

## Reads

`rg` first. `read` with offset/limit. Never dump `godstack/**/*.c` to understand
an API. Never dump `log` or `atlas.pgm`.

## C

C99. Unity build. Main file includes `.c` files. Those `.c` files have
`#pragma once`. Integer typedefs, `MIN`/`MAX`/`BETWEEN` live in `src/vt.h`.
Logs go through `src/vt_debug.h`.
