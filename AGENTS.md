# Agent contract

## Agents
- If user asks for changes directly in his terminal use ctl protocol.
- Debug visual bugs ctl protocol plus headless live binary.

## Product
- Cross-platform terminal: Linux, macOS, and Windows (CI runs `./build headless` + `tests/headless` on all three).
- Windowed GPU path is Vulkan via Rend `AUTO` (falls back to CPU raster when needed). `./build cpu` skips Vulkan entirely.
- This tree is the config. Edit the C and rebuild. No plugin ABI, no rc file, no dlopen.
- Knobs live in `config.h` (font, `alpha`, `vsync`, `hz`, palettes). Optional features are `.diff` files in `patches/` (`vt-<version>-<patch_name>`). Apply, then ask the user to rebuild.
- Fast path: hardware present when available, SSE2 preparsing, memfd ring with paired `MAP_FIXED` views.
- Agent debug: Headless Live Mode plus JSONL ctl socket — `read` / `rg` / `write` / screenshot without a window (`docs/ctl.md`). Socket is `$XDG_RUNTIME_DIR/vt/latest.sock` (else `/tmp/vt-<uid>/latest.sock`).

## Commands
- `gcc -o build build.c` once, then:
- `./build` — release windowed (`slangc` + `gcc -O2 src/main.c -o vt`)
- `./build debug` — `-g -DDEBUG -O0`
- `./build cpu` — no Vulkan; Rend CPU raster
- `./build headless` — `vt-headless` + `vt-live` (no window / Vulkan)
- `./build test` — current mode, ensures headless bins, then `tests/check`
- `sudo ./build install` — `/usr/bin/vt` and `/usr/share/vt/`
- Headless dump: `./vt-headless tests/glyph.txt`
- Run split only: `./vt-headless --dump-runs tests/runs.bin`
- Live ctl (no window/Vulkan): `./vt-live [--cols N] [--rows N]` (default 80x24)
- Needs Vulkan (unless `cpu` / `headless`), `slangc`, `-lutil` (`openpty`), and a TTF at the `config.h` path.
- Do not invoke `gcc` on the mains by hand unless flags match `build.c`.

## Layout
- `godstack/` submodule. Libraries are black boxes. Contract: `godstack/AGENTS.md`.
- Includes from this root: `-I . -I godstack/Peak -I godstack/Rend -I godstack/Term`.
- Peak before Rend. Include `term.h` then `term.c`, then `rend.h` / `peak.c` / `rend.c`.
- `PEAK_VULKAN` is on the Vulkan compile line. It does not compile shaders (`build.c` runs `slangc`).
- Single process, no threads. Child is `bash --login` on a PTY with `TERM=xterm-256color`.
- C99 unity build: each app is one `gcc` on its `src/main*.c` (includes `vt.c`). Three binaries: `vt`, `vt-headless`, `vt-live`. Included `.c` files use `#pragma once`.
- Integer typedefs, `MIN` / `MAX` / `BETWEEN` live in `src/vt.h`. Logs go through `src/vt_debug.h` into a 64-line ring; read them with ctl `{op:"log"}`. Peak `PINFO` still goes to stdout.

| Name | Concern                                       |
|------|-----------------------------------------------|
| Peak | Platform layer (window, wait/poll, clipboard) |
| Rend | Rendering calls (Vulkan 1.4 and CPU raster)   |
| Term | Terminal emulation (parser and cell grid)     |

| Object               | File                | Role                                   |
|----------------------|---------------------|----------------------------------------|
| `Term term`          | `src/vt.c`          | cell grid via typed `term_feed_*`      |
| `PeakWindow win`     | `src/vt_renderer.c` | window + fds                           |
| `Renderer renderer`  | `src/vt_renderer.c` | Rend handles + CPU glyph buffer        |

| Path                 | Role                                                       |
|----------------------|------------------------------------------------------------|
| `src/vt.c`           | shared body (term, peak, rend, circ, lru, renderer, ctl)   |
| `src/main.c`         | windowed app root → `vt`                                   |
| `src/main_headless.c`| dump app root → `vt-headless`                              |
| `src/main_headless_live.c` | live ctl app root → `vt-live`                        |
| `src/vt.h`           | types                                                      |
| `src/vt_circ_buf.c`  | memfd ring; wrap is one memcpy                             |
| `src/vt_lru.c`       | hashmap + DLL over 900 atlas slots                         |
| `src/vt_renderer.c`  | Peak + Rend + stb atlas                                    |
| `src/vt_ctl.c`       | JSONL control socket                                       |
| `src/vt_debug.h`     | logging macros                                             |
| `config.h`           | font path/size, `alpha`, `vsync`, `hz`, ANSI palettes      |
| `build.c`            | Poof driver (`slangc` then `gcc`)                          |
| `vulkan/vt.slang`    | instanced glyph quads (`vertMain` / `fragMain`)            |
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
| tests          | `docs/tests.md`    |
