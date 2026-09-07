# Renderer

`src/renderer_gpu.c` owns `Renderer` (`src/vt.h`). `src/main.c` holds `Renderer *renderer`. Peak owns the window + event fd (`renderer->win`). Rend owns GPU/CPU. SPIR-V is shipped (`vulkan/vt.vert.spv`, `vulkan/vt.frag.spv`). `build.c` runs `glslangValidator` on `vulkan/vt.vert` / `vulkan/vt.frag` only when those are newer. CPU path is `vulkan/vt.cpu.c` via `src/renderer_cpu.c` and `rend_pipeline_create_graphics_c`. `./build` skips Vulkan when no ICD/SPIR-V. `./build cpu` always skips Vulkan. Rend `AUTO` may fall back to `REND_BACKEND_CPU`. API: `rend.h` and Peak headers, not library `.c`.

Two present modes. `r->tile` is 1 when SPIR-V graphics succeeded: fullscreen triangle (`rend_cmd_draw(3, 1)`), atlas sampler, cell SSBO of packed `VtInstance` (not raw `TermCell`). Else instanced triangle-strip quads (`rend_cmd_draw(4, n)`), host-visible vertex buffer. Both use 12-byte `VtInstance` (`pos`, `foreground`, `background`). `vulkan/vt.slang` is unused.

## Glyphs

stb_truetype (`src/stb_truetype.h`) → RGBA atlas via `src/glyph_generator.c`. I pin ASCII 32..127 and U+FFFD at init. Other cps rasterize on miss (`renderer_glyph_get` → `glyph_table_find_hash`). Missing outlines try `font_fallback_path`. Color emoji: `font_emoji_path` (OpenType CBDT/CBLC PNG via `src/stb_image.h`). Wide emoji occupy two cells. Atlas slots are two cells wide; CPU path sets instance `wide` for a two-cell quad (narrow glyphs sample the left half). Tile path writes the neighbor SSBO cell with `pos` bit 0. I skip missing optional font files; I still start. Cache: `src/glyph_cache.c`. ASCII 32..127 and U+FFFD are reserved tiles, not LRU. Kitty graphics stamps color tiles into PUA (`src/kitty.c`). Color glyphs set bit 7 of instance fg low byte so the shader samples RGB+A instead of tinting coverage. CPU Rend sample is R-only: `./build cpu` shows emoji as coverage silhouette.

Font path, pixel size, `alpha`: `config.h`. No readable primary TTF → I do not start. Glyph coverage is always opaque. `alpha` < 1 → `PEAK_WINDOW_TRANSPARENT` (X11 ARGB). Compositors often stall `frame_end` 16–32 ms under IMMEDIATE, uncorrelated with CPU fill.

## Present path

I have no render thread. `wait_io` in `src/main.c` calls `peak_wait` on window fd, PTY, ctl listen, clients, one job pipe, SIGUSR1 (`peak_usr1_fd`). `hz` in `config.h` (0 = unpaced). Timeout is `-1` while idle, remaining `1/hz` while dirty, `0` when the frame is due (`vsync` dirty is `0`; the swapchain waits). `ingest` receives until EAGAIN and only `ringbuffer_produce`s; the mirror ring drops unread bytes when it wraps. Do not consume on EAGAIN — that empties the ring and parses every byte. Consume/present at most once per `1/hz` (`drain` of the tail). `r == 0` is EOF: `shell_gone`. Do not wait for all PTYs to go idle before a due frame. SIGUSR1: `theme_poll` / `peak_usr1_ack` re-reads `~/.config/omarchy/current/theme/alacritty.toml` then `~/.config/vt/config.toml` into the palette and presents. Missing file keeps `config.h`. After present: `redraw` clears. Do not wake me when `!redraw`. Peak still wakes on the window fd; do not use Display-wide `XPending` for timeout 0.

`peak_window_pending` is this window only. Display-wide `XPending` stays true for Vulkan WSI leftovers on the shared Xlib Display. Using that for timeout 0 spins after my first present (idle CPU 100%, GPU clocks follow). Idle must block on PTY + window fds.

`rend_renderer_create(..., AUTO, vsync)` takes `vsync` from `config.h`. False: MAILBOX, IMMEDIATE, FIFO. True: MAILBOX, FIFO. Many NVIDIA setups have no MAILBOX, so false → IMMEDIATE. Peak `PINFO Present mode` / `Composite alpha` go to stdout.

`begin` acquires the image; it does not re-query surface caps. `end` = submit + `vkQueuePresentKHR` — not “grid finished.” Fill time and end time are uncorrelated. Composite prefers the alpha path when `alpha` < 1 (`REND_VK_COMPOSITE_PREFER_ALPHA`).

## Grid to instances

Term owns `TermScreen` line rings (screen + alt), including `--headless` / `--live`. `renderer_fill_term` walks `term_row` at an origin, skips empty / default-bg spaces, bakes reverse/invisible/bold/faint into instance colors, overlays mouse selection as reverse without mutating `TermCell`. Cursor is a CPU color swap on its cell. Glyph ids: reserved ASCII table, else `glyph_table_find_hash`. `present` in `src/main.c` fills every used pane, then `mux_fill_walls` / `mux_fill_drop`, then `renderer_frame_begin` + `renderer_flush` + `renderer_frame_end`. No `renderer_sync`. No compute dest blit.

## Headless

`--headless` / `--live`: no window unless `--screenshot` / ctl `screenshot` creates an offscreen `Renderer` (`params.offscreen`). Those composite the CPU atlas to P6 PPM. Inspect via ctl `dump` / `screenshot`, not a display scrape.
