# Agent contract

## Agents
- If user asks for changes directly in his terminal use ctl protocol.
- Debug visual bugs ctl protocol plus headless live binary.
- Don't overwrite HTML YOU IDIOT!

## Product
- Cross-platform GPU terminal (vt). Linux, macOS, Windows. Not an st fork. Not an xterm. Not Xorg-only. Survivor: keep listed platforms working. Do not break a living Peak/Rend backend to tidy Linux.
- CI runs `./build headless` + `tests/headless` on all three.
- Windowed GPU path is Vulkan via Rend `AUTO` (falls back to CPU raster when needed). `./build` skips Vulkan when no ICD/SPIR-V. `./build cpu` always skips Vulkan.
- This tree is the config. Edit the C and rebuild. No plugin ABI, no rc file, no dlopen.
- Knobs live in `config.h` (font, `alpha`, `vsync`, `hz`, palettes, keys). Optional extras are `.diff` files in `patches/` (`vt-<version>-<patch_name>`). Apply, then ask the user to rebuild. Mux and kitty graphics are core.
- Fast path: hardware present when available, AVX2 preparsing, `peak_mirror_map` ring (wrap is one view). SIMD is optional speed; scalar must still parse.
- OS dirt is Peak. GPU dirt is Rend. Grid dirt is Term. vt calls `peak_*` / `rend_*` / `term_*`.
- OS-specific code is banned in `src/`. No `_WIN32` / POSIX headers / `getpid` / `waitpid` / `opendir`. Need a feature: add it to Peak, then call Peak. `build.c` is the exception.
- Agent debug: Headless Live Mode plus JSONL ctl socket — `read` / `rg` / `write` / screenshot without a window (`docs/ctl.md`). Socket is `$XDG_RUNTIME_DIR/vt/latest.sock` (else `/tmp/vt-<uid>/latest.sock`).

## Commands
- `./deps` — distro packages + font symlink (sudo if needed). Works before gcc.
- `gcc -o build build.c` once, then:
- `./build deps` — same as `./deps`
- `./build` — release windowed (Vulkan if ICD + SPIR-V, else CPU raster)
- `./build debug` — `-g -DDEBUG -O0`
- `./build cpu` — force no Vulkan; Rend CPU raster
- `./build headless` — `vt-headless` + `vt-live` + `vtctl` (no window / Vulkan)
- `./build test` — current mode, ensures headless bins, then `tests/check`
- `sudo ./build install` — runs `deps`, then `/usr/bin/vt`, `/usr/bin/vtctl`, and `/usr/share/vt/`
- `./build package` — Linux tarballs in `packages/` (Vulkan + CPU)
- Headless dump: `./vt-headless tests/glyph.txt`
- Run split only: `./vt-headless --dump-runs tests/runs.bin`
- Live ctl (no window/Vulkan): `./vt-live [--cols N] [--rows N]` (default 80x24)
- Ctl client: `./vtctl --help` then `./vtctl read` / `rg` / `write` (JSONL on stdout)
- Linux compile needs X11, Wayland, and Vulkan headers (`./deps`). Windowed GPU: shipped `vulkan/*.spv` + Vulkan loader (or `cpu` / `headless`). `glslangValidator` only if GLSL is newer. `-lutil` (`openpty`). TTF at the `config.h` path.
- Do not invoke `gcc` on the mains by hand unless flags match `build.c`.

## Layout
- `godstack/` submodule. Libraries are black boxes. Contract: `godstack/AGENTS.md`.
- Includes from this root: `-I . -I godstack/Peak -I godstack/Rend -I godstack/Term`.
- Peak before Rend. Include `term.h` then `term.c`, then `rend.h` / `peak.c` / `rend.c`.
- `PEAK_VULKAN` is on the Vulkan compile line. It does not compile shaders. `build.c` runs `glslangValidator` only when `vulkan/vt.vert` / `vulkan/vt.frag` is newer than the shipped `.spv`.
- Single process, no threads. Child is `bash --login` on a PTY. `TERM=xterm-256color` is the terminfo apps already have, not the product.
- C99 unity build: each app is one `gcc` on `src/main.c`. `vt` / `vt-headless` / `vt-live` include `vt.c`. `vtctl` is Peak-only (`-DVT_CTL`). Headless/live `-DVT_HEADLESS` (live also `-DVT_LIVE`). `main` dispatches `vt_main_windowed` / `vt_main_headless` / `vt_main_live` / `vt_main_ctl` from argv0 or `--windowed` / `--headless` / `--live` / `--ctl`. Included `.c` files use `#pragma once`.
- Integer typedefs, `MIN` / `MAX` / `BETWEEN` live in `src/vt.h`. Logs go through `src/vt_debug.h` into a 64-line ring; read them with ctl `{op:"log"}`. Peak `PINFO` still goes to stdout.

| Name | Concern                                       |
|------|-----------------------------------------------|
| Peak | Platform layer (window, wait/poll, clipboard) |
| Rend | Rendering calls (Vulkan 1.4 and CPU raster)   |
| Term | Terminal emulation (parser and cell grid)     |

| Object               | File                | Role                                   |
|----------------------|---------------------|----------------------------------------|
| `VtPane vt_panes[]`  | `src/vt_mux.c`      | per-pane Term + ring + PTY             |
| `PeakWindow win`     | `src/vt_renderer.c` | window + fds                           |
| `Renderer renderer`  | `src/vt_renderer.c` | Rend handles + CPU glyph buffer        |

| Path                 | Role                                                       |
|----------------------|------------------------------------------------------------|
| `src/vt.c`           | shared body (term, peak, rend, circ, lru, renderer, mux, kitty, ctl) |
| `src/main.c`         | app root: windowed / dump / live / ctl → `vt` `vt-headless` `vt-live` `vtctl` |
| `src/vt.h`           | types                                                      |
| `src/vt_circ_buf.c`  | `peak_mirror_map` ring; wrap is one view                   |
| `src/vt_lru.c`       | hashmap + DLL over 900 atlas slots                         |
| `src/vt_mux.c`       | panes; Ctrl-b; Middle-drag; drop other vt; ctl `split`/`focus`/`panes`/`move`/`adopt`/`give` |
| `src/vt_kitty.c`     | Kitty graphics APC; PUA color glyphs                       |
| `src/vt_renderer.c`  | Peak + Rend + stb atlas                                    |
| `src/vt_ctl.c`       | JSONL control socket                                       |
| `src/vt_debug.h`     | logging macros                                             |
| `config.h`           | font path/size, `alpha`, `vsync`, `hz`, palettes, keys     |
| `build.c`            | Poof driver (glslang if GLSL stale, then `gcc`)            |
| `vulkan/vt.vert`     | instanced glyph quads (GLSL 450)                           |
| `vulkan/vt.frag`     | coverage mix + underline/strike                            |
| `vulkan/vt.*.spv`    | shipped SPIR-V                                             |
| `lib/stb_truetype.h` | CPU atlas                                                  |
| `patches/`           | optional `.diff` features                                  |
| `docs/`              | on-demand agent docs (index below)                         |
| `fonts/`             | primary TTF at `config.h` path (gitignored; no font, no start). Optional fallback/emoji paths too. |

Work in the named file. No new module unless asked. Fuse stays out.

## Rules
- `rg` first. `read` with offset/limit.
- Never dump `godstack/**/*.c` to learn an API — read the header first.
- Never dump `atlas.pgm`. Stage timings: ctl `{op:"log","data":"present fill"}` / `{op:"log","data":"avg parse"}`.
- Drive the live grid with ctl (`docs/ctl.md`). Do not scrape the PTY.
- Edit `config.h` for knobs. Do not add an rc file, plugin registry, or `dlopen` layer.
- Present, idle, and ingest footguns live in `docs/renderer.md` and `docs/term.md` — read them before changing the frame loop or byte path.
- Library API and include rules: `godstack/AGENTS.md`.

## Docs (read on demand)

When asked about a topic, read the file completely and follow links.

| Topic          | File               |
|----------------|--------------------|
| product / UX   | `README.md`        |
| agent contract | `AGENTS.md`        |
| patches        | `docs/patches.md`  |
| ctl protocol   | `docs/ctl.md`      |
| renderer       | `docs/renderer.md` |
| parser / grid  | `docs/term.md`     |
| drag / drop    | `docs/features.md` |
| tests          | `docs/tests.md`    |
