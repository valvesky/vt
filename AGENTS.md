# Agent contract

Always-on rules. Files are cheap — read the named doc when the row matches. Do not load docs you are not using.

| When | Read |
|------|------|
| Live grid, TUI debug, user terminal changes | `docs/agents/ctl.md` |
| Frame loop, present, idle CPU, glyphs, atlas | `docs/agents/renderer.md` |
| Mux, middle-drag, pane drop, file drop | `docs/agents/features.md` |
| Apply or write a `.diff` | `docs/agents/patches.md` |
| `config.h` knobs / SIGUSR1 palette | `docs/agents/config.md` |
| Tests / fixtures | `docs/agents/tests.md` |
| Library API / includes | `godstack/AGENTS.md` |

`docs/index.html` is the human site. Handwritten. No docgen.

## Agents
- Visual bugs → ctl + `vt --live`.
- Human docs: edit `docs/index.html` directly.

## Product
- Cross-platform GPU terminal (vt). Linux, macOS, Windows. Not an st fork. Not an xterm. Not Xorg-only. Survivor: keep listed platforms working. Do not break a living Peak/Rend backend to tidy Linux.
- CI: `./build headless` + `tests/headless` on all three.
- Windowed GPU: Vulkan via Rend `AUTO` (CPU raster fallback). `./build` skips Vulkan when no ICD/SPIR-V. `./build cpu` always skips Vulkan.
- This tree is the config. Edit the C and rebuild. No plugin ABI, no rc file, no dlopen.
- Knobs: `config.h`. Optional extras: `patches/vt-<version>-<patch_name>`. Apply, then ask the user to rebuild. Mux and kitty graphics are core.
- Fast path: hardware present when available, AVX2 preparsing, `peak_mirror_map` ring (wrap is one view). SIMD is optional; scalar must still parse.
- OS dirt is Peak. GPU dirt is Rend. Grid dirt is Term. vt calls `peak_*` / `rend_*` / `term_*`.
- OS-specific code is banned in `src/`. No `_WIN32` / POSIX headers / `getpid` / `waitpid` / `opendir`. Need a feature: add it to Peak, then call Peak. `build.c` is the exception.
- Ctl socket: `$XDG_RUNTIME_DIR/vt/latest.sock` else `/tmp/vt-<uid>/latest.sock`.

## Commands

```
gcc -o build build.c          # once
./deps                        # or ./build deps; sudo if needed
./build                       # windowed
./build debug                 # -g -DDEBUG -O0
./build cpu                   # no Vulkan
./build headless              # vt + vtctl, no Vulkan
./build test                  # current mode, then tests/check
sudo ./build install          # /usr/bin/vt, vtctl, vt-fast.so, /usr/share/vt/
./build package               # Linux tarballs in packages/
./vt --headless tests/glyph.txt
./vt --headless --dump-runs tests/runs.bin
./vt --live [--cols N] [--rows N]   # default 80x24
./vtctl --help
```

Linux needs X11, Wayland, and Vulkan headers (`./deps`). Windowed GPU: shipped `vulkan/*.spv` + loader. `glslangValidator` only if GLSL is newer than SPIR-V. TTF at the `config.h` path. Do not `gcc` the mains by hand unless flags match `build.c`.

## Layout
- `godstack/` submodule. Black boxes. Peak before Rend. `-I . -I godstack/Peak -I godstack/Rend`. Unity root is `src/main.c`: `rend.h` / `peak.c` / `rend.c`, then `vt.h`, then `term.c` / `ringbuffer.c` / glyph / `renderer_gpu.c` / mux / kitty / osc / ctl.
- `PEAK_VULKAN` is on the Vulkan compile line. It does not compile shaders. `build.c` runs `glslangValidator` only when `vulkan/vt.vert` / `vulkan/vt.frag` is newer than the shipped `.spv`.
- Single process, no threads. Child is `bash --login` on a PTY (`peak_pty_spawn`). `vt_shell_fast_pipe` in `config.h` switches to `peak_pipe_spawn`. `TERM=xterm-256color` is the terminfo apps already have, not the product.
- C99 unity: one `gcc` on `src/main.c`. `vtctl` is Peak-only (`src/main_ctl.c`). `main` dispatches `main_windowed` / `main_headless` / `main_live` from `--headless` / `--live`. Included `.c` files use `#pragma once`.
- Types, macros, and prototypes live in `src/vt.h`. Logs → stderr (`2>log`). Peak `PINFO` still goes to stdout.

| Name | Concern |
|------|---------|
| Peak | Platform (window, wait/poll, clipboard) |
| Rend | Vulkan 1.4 and CPU raster |
| Term | Parser and cell grid (`src/term.c`) |

| Object | File | Role |
|--------|------|------|
| `Multiplexor multiplexor` | `src/main.c` | panes; type in `src/vt.h` |
| `Renderer *renderer` | `src/main.c` | Peak window + Rend; type in `src/vt.h` |
| `PeakWindow win` | `src/vt.h` | field of `Renderer` |

| Path | Role |
|------|------|
| `src/main.c` | app root → `vt` (`--headless` / `--live`); wait, ingest, present |
| `src/vt.h` | types, macros, prototypes |
| `src/term.c` | parser and cell grid |
| `src/ringbuffer.c` | mirror ring, line ranges, typed runs |
| `src/glyph_cache.c` | atlas LRU; reserved ASCII + U+FFFD |
| `src/glyph_generator.c` | stb raster + CBDT emoji into atlas |
| `src/multiplexing.c` | panes; Ctrl-b; Middle-drag; ctl split/focus/panes/move/adopt/give |
| `src/kitty.c` | Kitty APC; PUA color glyphs |
| `src/osc.c` | OSC 52 clipboard |
| `src/shell.c` | PTY (or fast pipe) spawn/read/write |
| `src/renderer_gpu.c` | Peak + Rend + atlas; tile SSBO or instance quads |
| `src/renderer_cpu.c` | includes `vulkan/vt.cpu.c` (Rend CPU vert/frag) |
| `src/ctl.c` | JSONL ctl |
| `src/main_ctl.c` | `vtctl` |
| `src/stb_truetype.h` / `src/stb_image.h` | CPU atlas + PNG emoji |
| `config.h` | font, `alpha`, `vsync`, palettes, keys |
| `build.c` | Poof driver; also builds `vt-fast.so` |
| `vulkan/vt.vert` / `vt.frag` / `vt.*.spv` | fullscreen triangle + shipped SPIR-V |
| `vulkan/vt.cpu.c` | CPU raster matching the shaders |
| `patches/` | optional `.diff` |
| `docs/` | handwritten human site (`index.html`) |
| `docs/agents/` | on-demand agent refs (table above) |
| `fonts/` | primary TTF at `config.h` path (gitignored; no font, no start) |

Work in the named file. No new module unless asked. Fuse stays out.

## Rules
- `rg` first. `read` with offset/limit.
- Never dump `godstack/**/*.c` to learn an API — header first.
- Never dump `atlas.pgm`. Timings: stderr from `src/vt.h` (`2>log`).
- Drive the live grid with ctl. Do not scrape the PTY. Never `vtctl dump` unless asked. Never `vtctl run` to drive a TUI (`run` is off-grid `sh -c` only).
- Edit `config.h` for knobs. No rc file, plugin registry, or `dlopen`.
- Before changing the frame loop or byte path, read `docs/agents/renderer.md`.
- Before mux / pane-drop work, read `docs/agents/features.md`.
- Before apply/write of a patch, read `docs/agents/patches.md`.

## Ctl (80%)

```
./vtctl read
./vtctl read 20 8
./vtctl rg needle
./vtctl write $'\x1b'
```

Loop: `read` → `rg` → `write` keys → `read`. `write` is raw PTY bytes (the child). `read`/`rg` are the live grid. Default `read` is 8 rows around the tty cursor. `rg` is substring per row, not regex. Do not add `edit` / `insert` / `vim`. Do not paste JSONL. Rest: `./vtctl --help` then `docs/agents/ctl.md`.

# Tests

```
./build test
./vt --headless tests/glyph.txt
```

Validate: `./build test` must pass before done. Details: `docs/agents/tests.md`.
