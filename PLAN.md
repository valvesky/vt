# vt 0.5 packed cell

vt is the product. Term is the library. If TermCell cannot pack, **change Term**.

## Now

`TermCell` is 16B: `u32` fg, `u32` bg, `u32` codepoint, `bool is_dirty` (+3 pad).
Present walks that grid into 12B `VtInstance`. Ghostty is 8B/cell.
Alacritty is 24B/cell + 32B/row. The gap is style-on-cell and a grapheme
pointer, not the parser.

Drop `is_dirty`. Nothing in vt reads it. Do not keep a dirty bit in the
packed cell. No 32B row header.

Pack `TermCell` to 8 bytes. `STATIC_ASSERT`.

Styles: 16-bit style id, intern into a refcounted hash. Do not store
fg/bg/attr on the cell. Most cells unstyled; most styles shared; most
shared styles are runs. Lookup is not the cost. Term owns the table.
Fill resolves id → colors, then bakes as today.

Codepoints: one scalar inline. Multi-codepoint graphemes in a look-aside
table (bitmap-tracked chunks). 2-bit content tag in the `u64`. No per-cell
pointer, no heap `Vec` per emoji.

16-bit keys are offsets in a page, not global ids. Grid pages ~400KB
(`2^16` cells). Next page if a table fills. Hist is the same cell type.
Do not invent a second grid in vt.

No cell SSBO. No `cellMain` dest blit. No vs/fs triangle. Instance store
stays 12B. `vt_glyph_get` stays on UTF-8 ingest.

Keep `VtRing` (ingest). Wheel: mouse SGR/X10 64/65 if tracking; else alt
CSI A/B; else ignore. Do not walk Term hist from present.

`--headless` file `cmp` vs `.ok` is not the live PTY (CUP is absolute).
Agents drive ctl JSONL (`dump` / `screenshot` / `write`).

## Rend

Instanced glyph quads. Do not resurrect compute dest.

## Out of scope

Tabs, mux, sixel, ligatures, IME, wide-cell render, new ctl ops,
plugins, threads, growing the atlas, compute present.
