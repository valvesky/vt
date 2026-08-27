# Renderer

`PeakWindow win` and `Renderer renderer` live in `src/vt_renderer.c`.
Incomplete `Renderer` type is in `src/vt.h`.

Peak is the window and its X fd. Rend is Vulkan 1.4 only. It does not
compile shaders. `build.c` runs `slangc` on `vulkan/vt.slang` `vertMain` / `fragMain` to
`vulkan/vt.vert.spv` and `vulkan/vt.frag.spv`. Read `rend.h` / Peak headers. Do not open
library `.c` files to learn the API.

## Glyphs

CPU atlas via stb_truetype (`lib/stb_truetype.h`). ASCII 32..127 and
U+FFFD are pinned at init. Other codepoints rasterize on miss. Box
drawing is CPU-stroked so corners meet. Slot 0 is space. LRU is hashmap + DLL over 900 atlas slots.

Font path, size, and background `alpha` are `config.h`. No TTF, no start.
`alpha` < 1 asks Peak for `PEAK_WINDOW_TRANSPARENT` (X11 ARGB). The compositor
then stalls `frame_end` (`end` 16–32 ms on IMMEDIATE, uncorrelated with fill).
Default `alpha` is 1 (opaque visual). Glyph coverage stays opaque either way.

## Present

No render thread. `peak_wait` on the PTY, the Peak X fd, the ctl listen socket,
clients, and one job pipe. Timeout `-1` when `redraw` is false, `0` when
a frame is pending or this window has X events. After present, `redraw = false`.

`peak_window_pending` (Peak 0.6.6) is this window only (`XCheckIfEvent` +
`XPutBackEvent`). Display-wide `XPending` stays true for Vulkan WSI events
on the shared Xlib Display; using that for timeout 0 spins after the first
present. Idle must `poll` sleep. See `PLAN.md`.

`rend_renderer_create(..., vsync)` from `config.h`. `vsync=false` picks
MAILBOX else IMMEDIATE else FIFO. `true` picks MAILBOX else FIFO. This NVIDIA
box has no MAILBOX, so false is IMMEDIATE. DEBUG logs `present %llu ns` around
`vt_present`, and `present fill / begin / draw / end` inside `renderer_sync`.
Quit logs `avg parse / fill / begin / draw / end / present ns` to `log`.
Peak `PINFO Present mode` / `Composite alpha` go to stdout, not `log`.

`fill` is CPU instance writes. `begin` acquires; it does not query surface caps.
`end` is submit plus `vkQueuePresentKHR`. Rend 1.5.2 presents on the graphics
family when that family can present (last-wins used to pick NVIDIA compute).
OPAQUE composite first. Extra swapchain image. See `PLAN.md`.

Term owns heap `TermCell[]` (screen and alt), same as `--headless`.
Present walks that grid, skips empty cells, bakes reverse/invisible/bold/faint
into instance colors, writes `VtInstance` (pos, fg+col, bg+row; 12 B),
and `rend_cmd_draw(4, n)` triangle-strip quads. ASCII glyph ids are a 128-entry
table; other codepoints `vt_lru_peek` (no LRU touch). Atlas is an R8 texture.
Cursor is a CPU color swap on that cell. Instance buffer is host-visible
vertex data. No cell SSBO, no compute dest, no blit.

`--headless` has no Peak and no Vulkan. `--screenshot` still composites
the CPU atlas to a P6 PPM (same mix as the shader). Agents verify the
grid with ctl `dump` and the CPU atlas with ctl `screenshot`.
