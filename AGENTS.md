# Agent contract

Always-on rules. Files are cheap — read the named doc when the row matches. Do not load docs you are not using.

| When | Read |
|------|------|
| Live grid, TUI debug, user terminal changes | `docs/agents/ctl.md` |
| Frame loop, present, idle CPU, glyphs, atlas | `docs/agents/renderer.md` |
| Mux, middle-drag, pane drop, file drop | `docs/agents/features.md` |
| Apply or write a `.diff` | `docs/agents/patches.md` |
| Library API / includes | `godstack/AGENTS.md` |

`docs/index.html` is the human site. Handwritten. No docgen.

## Agents
- User asks for changes in his terminal → ctl. Do not scrape the PTY.
- Visual bugs → ctl + `vt-live`.
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
./build headless              # vt-headless + vt-live + vtctl
./build test                  # current mode, then tests/check
sudo ./build install          # /usr/bin/vt, vtctl, /usr/share/vt/
./build package               # Linux tarballs in packages/
./vt-headless tests/glyph.txt
./vt-headless --dump-runs tests/runs.bin
./vt-live [--cols N] [--rows N]   # default 80x24
./vtctl --help
```

Linux needs X11, Wayland, and Vulkan headers (`./deps`). Windowed GPU: shipped `vulkan/*.spv` + loader. `glslangValidator` only if GLSL is newer than SPIR-V. TTF at the `config.h` path. Do not `gcc` the mains by hand unless flags match `build.c`.

## Layout
- `godstack/` submodule. Black boxes. Peak before Rend. Include `vt_term.h` then `vt_term.c`, then `rend.h` / `peak.c` / `rend.c`. `-I . -I godstack/Peak -I godstack/Rend`.
- `PEAK_VULKAN` is on the Vulkan compile line. It does not compile shaders. `build.c` runs `glslangValidator` only when `vulkan/vt.vert` / `vulkan/vt.frag` is newer than the shipped `.spv`.
- Single process, no threads. Child is `bash --login` on a PTY. `TERM=xterm-256color` is the terminfo apps already have, not the product.
- C99 unity: one `gcc` on `src/main.c`. `vt` / `vt-headless` / `vt-live` include `vt.c`. `vtctl` is Peak-only (`-DVT_CTL`). Headless/live `-DVT_HEADLESS` (live also `-DVT_LIVE`). `main` dispatches `vt_main_windowed` / `vt_main_headless` / `vt_main_live` / `vt_main_ctl` from argv0 or `--windowed` / `--headless` / `--live` / `--ctl`. Included `.c` files use `#pragma once`.
- Integer typedefs, `MIN` / `MAX` / `BETWEEN` live in `src/vt.h`. Logs: `src/vt_debug.h` → stderr (`2>log`). Peak `PINFO` still goes to stdout.

| Name | Concern |
|------|---------|
| Peak | Platform (window, wait/poll, clipboard) |
| Rend | Vulkan 1.4 and CPU raster |
| vt_term | Parser and cell grid |

| Object | File | Role |
|--------|------|------|
| `VtPane vt_panes[]` | `src/vt_mux.c` | per-pane Term + ring + PTY |
| `PeakWindow win` | `src/vt_renderer.c` | window + fds |
| `Renderer renderer` | `src/vt_renderer.c` | Rend handles + CPU glyph buffer |

| Path | Role |
|------|------|
| `src/vt.c` | shared body |
| `src/main.c` | app root → `vt` `vt-headless` `vt-live` `vtctl` |
| `src/vt.h` | types |
| `src/vt_term.h` / `src/vt_term.c` | parser and cell grid |
| `src/vt_ring_buffer.h` / `src/vt_ring_buffer.c` | mirror ring, line ranges, typed runs |
| `src/vt_glyth_cache.h` / `src/vt_glyth_cache.c` | atlas LRU; reserved ASCII + U+FFFD |
| `src/vt_mux.c` | panes; Ctrl-b; Middle-drag; ctl split/focus/panes/move/adopt/give |
| `src/vt_kitty.c` | Kitty APC; PUA color glyphs |
| `src/vt_renderer.c` | Peak + Rend + stb atlas |
| `src/vt_ctl.c` | JSONL ctl |
| `src/vt_debug.h` | log macros |
| `config.h` | font, `alpha`, `vsync`, palettes, keys |
| `build.c` | Poof driver |
| `vulkan/vt.vert` / `vt.frag` / `vt.*.spv` | quads + shipped SPIR-V |
| `lib/stb_truetype.h` | CPU atlas |
| `patches/` | optional `.diff` |
| `docs/` | handwritten human site (`index.html`) |
| `docs/agents/` | on-demand agent refs (table above) |
| `fonts/` | primary TTF at `config.h` path (gitignored; no font, no start) |

Work in the named file. No new module unless asked. Fuse stays out.

## Rules
- `rg` first. `read` with offset/limit.
- Never dump `godstack/**/*.c` to learn an API — header first.
- Never dump `atlas.pgm`. Timings: stderr from `src/vt_debug.h` (`2>log`).
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
./vt-headless tests/glyph.txt
```

Validate: `./build test` must pass before done. It keeps the last compile mode, builds `vt-headless` and `vt-live` if needed, then `tests/check` (`tests/headless`, then `tests/tui`).

`tests/headless` `cmp`s dumps to `tests/glyph.ok`, `tests/csi.ok`, `tests/esc.ok`, `tests/oscutf.ok`, `tests/kitty.ok`. `--dump-runs` vs `tests/runs.ok`, `tests/utf8ascii.ok`, `tests/kitty-runs.ok`. `tests/badutf.bin` must not abort. `--screenshot` writes P6 PPM. If `python3`: `tests/clip`, `tests/ctl`, `tests/mux`, `tests/kitty_query`. Pane-drop is manual (`docs/agents/features.md`).

CI: Linux, macOS, Windows. Only `./build headless` + `tests/headless`. No windowed tests on CI.

`tests/tui` starts `./vt-live --cols 120 --rows 36`, ctl JSONL, drives nvim/lf/ncmpcpp/pi when present. Screenshots under `tests/golden/.got/`; no golden compare. `*.ppm` and `.got/` are gitignored. Automated checks use `vt-headless` or ctl `dump` / `screenshot`. Do not scrape the PTY.

## Adding a check

Ship the regression with the core change or the patch that needs it. Record a new `*.ok` from `./vt-headless` only after the dump is correct. Default `tests/headless` must pass. Patch fixtures + `tests/headless` hunks belong in that diff (`docs/agents/patches.md`). Do not add a windowed test to CI.

| Kind | Files | Wire |
|------|-------|------|
| grid / parser | `tests/foo.txt` or `.bin` + `tests/foo.ok` | `cmp` in `tests/headless` |
| run split | fixture + `--dump-runs` vs `tests/foo.ok` | `tests/headless` |
| must not abort | e.g. `tests/badutf.bin` | run, ignore dump |
| pixels | `--screenshot` P6 or ctl `screenshot` | header check; optional PPM under `tests/golden/` |
| live TUI | ctl `write` / `dump` in `tests/tui` | skip if app missing |
| clipboard | `tests/clip` + `tests/osc52.bin` | `tests/headless` if python3 |
| ctl read / rg | `tests/ctl` | `tests/headless` if python3 |
| vtctl --help | `./vtctl --help` | `tests/headless` |
