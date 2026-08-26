# Renderer

`PeakWindow win` and `Renderer renderer` live in `src/vt_renderer.c`.
Incomplete `Renderer` type is in `src/vt.h`.

Peak is the window and its X fd. Rend is Vulkan 1.4 only. It does not
compile shaders. `build.c` runs `slangc` on `vulkan/vt.slang` `cellMain` to
`vulkan/vt.comp.spv`. Read `rend.h` / Peak headers. Do not open
library `.c` files to learn the API.

## Glyphs

CPU atlas via stb_truetype (`lib/stb_truetype.h`). ASCII 32..127 and
U+FFFD are pinned at init. Other codepoints rasterize on miss. Box
drawing is CPU-stroked so corners meet. Slot 0 is space. LRU is hashmap + DLL over 900 atlas slots.

Font path, size, and background `alpha` are `config.h`. No TTF, no start.
`alpha` < 1 asks Peak for an ARGB window; glyph coverage stays opaque.

## Present

No render thread. `poll` on the PTY, the Peak X fd, the ctl listen
socket, clients, and one job pipe. Sleeps (`timeout -1`) when the grid is
unchanged. `XPending` forces a 0 timeout so the X fd cannot spin.

Term writes `TermCell[]` in host-visible SSBOs (screen, alt, view).
Present pushes buffer device addresses and `dispatch(cols, rows, 1)`
(one compute thread per cell) into a dest buffer sized from
`color_target`, `rend_cmd_copy_buffer_to_texture`s that to a dest
texture, and `rend_cmd_blit`s onto `rend_renderer_color_target`. No
vertex/fragment present shaders. No instanced quads.

`--headless` has no Peak and no Vulkan. `--screenshot` still composites
the CPU atlas to a P6 PPM (same mix as the shader). Agents verify the
grid with ctl `dump` and the CPU atlas with ctl `screenshot`.
