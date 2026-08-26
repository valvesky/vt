# Renderer

`PeakWindow win` and `Renderer renderer` live in `src/vt_renderer.c`.
Incomplete `Renderer` type is in `src/vt.h`.

Peak is the window and its X fd. Rend is Vulkan 1.4 only. It does not
compile shaders. `build.c` runs `glslc` on `vulkan/vt.{vert,frag}` first.
Read `rend.h` / Peak headers. Do not open library `.c` files to learn the
API.

## Glyphs

CPU atlas via stb_truetype (`lib/stb_truetype.h`). ASCII 32..127 is baked
at init. Other codepoints rasterize on miss. Box drawing is CPU-stroked
so corners meet. Instanced quads; cell instance is `Renderer_Cell`.

Font path, size, and background `alpha` are `config.h`. No TTF, no start. `alpha` < 1 asks Peak for an ARGB window; glyph coverage stays opaque.

## Present

No render thread. `poll` on the PTY, the Peak X fd, the ctl listen
socket, clients, and one job pipe. Sleeps (`timeout -1`) when the grid is
unchanged. `XPending` forces a 0 timeout so the X fd cannot spin. Present
only on PTY bytes or resize.

`--headless` has no Peak and no Vulkan. `--screenshot` still composites
the CPU atlas to a P6 PPM (shader mix).
