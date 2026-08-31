# Renderer

`src/vt_renderer.c`: file-scope `PeakWindow win` and `Renderer renderer`. Incomplete `Renderer` is in `src/vt.h`. Peak owns my window + event fd. Rend owns GPU/CPU. I do not compile shaders at runtime. SPIR-V is shipped (`vulkan/vt.vert.spv`, `vulkan/vt.frag.spv`). `build.c` runs `glslangValidator` on `vulkan/vt.vert` / `vulkan/vt.frag` only when those are newer. CPU path is `vulkan/vt.cpu.c` via `rend_pipeline_create_graphics_c`. `./build cpu` skips Vulkan. Rend `AUTO` may fall back to `REND_BACKEND_CPU`. API: `rend.h` and Peak headers, not library `.c`.

## Glyphs

stb_truetype (`lib/stb_truetype.h`) → RGBA atlas. I pin ASCII 32..127 and U+FFFD at init. Other cps rasterize on miss. Missing outlines try `font_fallback_path`. Color emoji: `font_emoji_path` (OpenType CBDT/CBLC PNG). Wide emoji (U+1F300..U+1FAFF) occupy two cells. Atlas slots are two cells wide; instance bit `wide` draws a two-cell quad (narrow glyphs sample the left half). I skip missing optional font files; I still start. Box-drawing is CPU-stroked. Slot 0 is space. Cache: hashmap + DLL, 900 slots (`src/vt_lru.c`). Evict deletes one hash key (`vt_lru_hash_del`); do not rebuild the map. Kitty graphics stamps color tiles into PUA (`src/vt_kitty.c`). Color glyphs set bit 7 of instance fg low byte so the shader samples RGB+A instead of tinting coverage. CPU Rend sample is R-only: `./build cpu` shows emoji as coverage silhouette.

Font path, pixel size, `alpha`: `config.h`. No readable primary TTF → I do not start. Glyph coverage is always opaque. `alpha` < 1 → `PEAK_WINDOW_TRANSPARENT` (X11 ARGB). Compositors often stall `frame_end` 16–32 ms under IMMEDIATE, uncorrelated with CPU fill. Default `alpha` is 1.

## Present path

I have no render thread. `peak_wait` on window fd, ctl listen, clients, one job pipe. PTY is in that set only when I am idle (`timeout == -1`) or a frame is due (`timeout == 0`). Positive timeout = `hz` hold: no PTY, no ingest. Timeout is `-1` while `!redraw`, `0` when `now >= next_present`, else remaining ms (min 1) while dirty and before `hz`. After present: `redraw` clears, `next_present = now + 1s/hz`. Do not wake me when `!redraw`. Peak still wakes on the window fd; do not use Display-wide `XPending` for timeout 0.

`peak_window_pending` (Peak 0.6.6) is this window only (`XCheckIfEvent` + `XPutBackEvent`). Display-wide `XPending` stays true for Vulkan WSI leftovers on the shared Xlib Display. Using that for timeout 0 spins after my first present (idle CPU 100%, GPU clocks follow). Idle must block on PTY + window fds.

`rend_renderer_create(..., AUTO, vsync)` takes `vsync` from `config.h`. False: MAILBOX, IMMEDIATE, FIFO. True: MAILBOX, FIFO. Many NVIDIA setups have no MAILBOX, so false → IMMEDIATE. DEBUG: `present %llu ns` around `vt_present`; `present fill / begin / draw / end` inside `renderer_sync`. On quit, I send averages (parse, fill, begin, draw, end, present) to my ctl log ring. Peak `PINFO Present mode` / `Composite alpha` go to stdout, not the ring.

`fill` = CPU instance writes. `begin` acquires the image; it does not re-query surface caps. `end` = submit + `vkQueuePresentKHR` — not “grid finished.” Fill time and end time are uncorrelated. Rend 1.5.2 presents on the graphics family when that family can present (older last-wins could pick NVIDIA compute). Composite prefers OPAQUE; swapchain keeps an extra image.

## Grid to quads

Term owns heap `TermCell[]` (screen + alt), including `vt-headless` / `vt-live`. `renderer_fill` walks that grid at an origin, skips empty cells, bakes reverse/invisible/bold/faint into instance colors, overlays mouse selection as reverse without mutating `TermCell`. `renderer_flush` begins the frame and draws. One pane: `renderer_sync` = fill at (0,0) then flush. Split: `vt_mux_present` fills every pane at its origin, then box walls, then one flush. Each drawn cell → 12-byte `VtInstance` (position, fg+column, bg+row). Draw: `rend_cmd_draw(4, n)` triangle-strip quads. ASCII ids: 128-entry table. Other cps: `vt_lru_peek` (no LRU touch on present). Cursor is a CPU color swap on its cell. Instances are host-visible vertex memory. I have no cell SSBO, no compute dest, no grid blit.

## Headless

`vt-headless` / `vt-live`: no window, no Vulkan. `--screenshot` and ctl `screenshot` composite my CPU atlas to P6 PPM with the same mix as the shader. Inspect via ctl `dump` / `screenshot`, not a display scrape.
