# vt 0.2 present

vt is the product. Rend is the library. If Rend cannot present compute, **change Rend**.

## Now

`cellMain` dispatch into dest buffer, in-frame copy to dest tex, blit dest →
`color_target`. Dest size is `color_target`, not Peak window size. No CPU fill
of dest. No `copy_buffer` one-shot in present. No vs/fs triangle.

Scrollback is gone from vt: no `view_off`, no view SSBO, no hist wheel.
Wheel: mouse SGR/X10 64/65 if tracking; else alt CSI A/B; else ignore.
Keep `VtRing` (ingest). Do not touch Term hist internals.

`vt_present` does not walk cells. `vt_glyph_get` runs on UTF-8 PARSE atoms
in `vt_feed_runs`. Shader ignores `is_dirty`.

`glyph_map_upload` skips if `!glyph_map_gpu_dirty`.

`--headless` file `cmp` vs `.ok` is not the live PTY (CUP is absolute).
Agents drive ctl JSONL (`dump` / `screenshot` / `write`).

## Rend

`rend_cmd_copy_buffer_to_texture`: frame cmdbuf, compute-write barrier, copy,
leave dest TRANSFER_DST. `rend_cmd_blit` onto `color_target` leaves it
TRANSFER_DST. `frame_end` present barrier includes TRANSFER.

## Out of scope

Tabs, mux, sixel, ligatures, IME, wide cells, selection, OSC 52, new ctl
ops, plugins, threads, growing the atlas, instanced quads.
