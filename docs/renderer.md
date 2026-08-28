# Renderer

Windowed drawing lives in `src/vt_renderer.c` as two file-scope objects:
`PeakWindow win` and `Renderer renderer`. The incomplete `Renderer` type is
declared in `src/vt.h`.

Peak owns the window and its platform event fd. Rend owns the GPU (or CPU)
backend. vt does not compile shaders itself: `build.c` runs `slangc` on
`vulkan/vt.slang` (`vertMain` / `fragMain`) to produce
`vulkan/vt.vert.spv` and `vulkan/vt.frag.spv`. The CPU path uses
`vulkan/vt.cpu.c` through `rend_pipeline_create_graphics_c`, because slang
`-target c` does not emit separate vert/frag hosts. `./build cpu` skips
Vulkan entirely. Rend `AUTO` can also fall back to `REND_BACKEND_CPU` at
runtime. Read `rend.h` and the Peak headers for the API; do not open library
`.c` files to learn call shapes.

## Glyphs

Glyphs are rasterized on the CPU with stb_truetype (`lib/stb_truetype.h`) into
an RGBA atlas. ASCII 32..127 and U+FFFD are pinned at init. Other codepoints
rasterize on miss. Missing outlines try `font_fallback_path` from `config.h`.
Color emoji use `font_emoji_path` (OpenType CBDT/CBLC PNG, Noto Color Emoji).
Wide emoji (U+1F300..U+1FAFF) fit into two cells. Atlas slots are two cells
wide so the PNG is not crushed; the instance bit `wide` draws a two-cell quad
(narrow glyphs sample the left half). Those extra paths are optional: a missing
file is skipped and vt still starts.
Box-drawing characters are CPU-stroked so corners meet. Slot 0 is space. The
atlas cache is a hashmap plus doubly linked list over 900 slots (`src/vt_lru.c`).
Color glyphs set bit 7 of the instance fg low byte so the shader samples RGB+A
instead of tinting coverage with cell fg. The CPU Rend sample path is R-only,
so `./build cpu` shows emoji as a coverage silhouette.

Font path, pixel size, and background `alpha` come from `config.h`. Without
a readable TTF at the primary path, vt will not start. Glyph coverage is always
opaque. Background `alpha` below 1 asks Peak for `PEAK_WINDOW_TRANSPARENT`
(X11 ARGB). On a compositing manager that path often stalls `frame_end`
with 16–32 ms spikes under IMMEDIATE present, uncorrelated with CPU fill
cost. The default `alpha` is 1 (opaque visual) for that reason.

## Present path

There is no render thread. The main loop waits with `peak_wait` on the Peak
window fd, the ctl listen socket, connected clients, and one job pipe.
The PTY is in that set only when idle (`timeout == -1`) or a frame is due
(`timeout == 0`). A positive timeout is the `hz` hold: no PTY, no ingest.
Timeout is `-1` while `redraw` is false (idle), `0` when a frame is due
(`now >= next_present`), and the remaining milliseconds (min 1) when the
grid is dirty but the `hz` deadline has not arrived. After a successful
present, `redraw` clears and `next_present` becomes `now + 1s/hz`.
Do not wake when `!redraw`. Peak still wakes on the window fd; do not
use Display-wide `XPending` for timeout 0.

`peak_window_pending` (Peak 0.6.6) reports only this window, via
`XCheckIfEvent` and `XPutBackEvent`. Display-wide `XPending` stays true for
Vulkan WSI leftovers on the shared Xlib Display. Using that signal for
timeout 0 spins the loop after the first present (idle CPU at 100%, GPU
clocks following). Idle must block on PTY and window fds the way a classical
terminal does. Deeper notes live in `PLAN.md`.

`rend_renderer_create(..., AUTO, vsync)` takes `vsync` from `config.h`.
With `vsync` false the preference order is MAILBOX, then IMMEDIATE, then
FIFO. With `vsync` true it is MAILBOX then FIFO. Many NVIDIA setups have no
MAILBOX, so false becomes IMMEDIATE. DEBUG builds log `present %llu ns`
around `vt_present` and `present fill / begin / draw / end` inside
`renderer_sync`. On quit, averages for parse, fill, begin, draw, end, and
present go to the ctl log ring (`{op:"log"}`). Peak lines such as `PINFO Present mode` and
`Composite alpha` go to stdout, not that ring.

Inside a frame, `fill` is CPU instance writes. `begin` acquires the image; it
does not re-query surface caps. `end` is submit plus `vkQueuePresentKHR` —
it is not “the cell grid finished.” Fill time and end time are uncorrelated.
Rend 1.5.2 presents on the graphics family when that family can present
(older last-wins queue selection could pick an NVIDIA compute family).
Composite prefers OPAQUE, and the swapchain keeps an extra image. See
`PLAN.md` for the packed-grid / instance-store leftover.

## Grid to quads

Term owns the heap `TermCell[]` for screen and alt buffers, including under
`vt-headless` / `vt-live`. Present walks that grid, skips empty cells, bakes reverse,
invisible, bold, and faint into instance colors, and overlays mouse
selection as reverse without mutating `TermCell`. Each drawn cell becomes a
12-byte `VtInstance` (position, fg+column, bg+row). Draw is
`rend_cmd_draw(4, n)` triangle-strip quads. ASCII glyph ids use a 128-entry
table; other codepoints use `vt_lru_peek` (no LRU touch on the present path).
The cursor is a CPU color swap on its cell. Instance data is host-visible
vertex memory. There is no cell SSBO, no compute destination, and no blit
path for the grid.

## Headless

`vt-headless` and `vt-live` build without a window stack and without Vulkan.
`--screenshot` and ctl `screenshot` still composite the CPU atlas to a P6
PPM with the same mix as the shader. Inspect the grid with ctl `dump` and
the atlas with ctl `screenshot` rather than scraping a display.
