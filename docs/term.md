# Parser and cell grid

Parsing and the cell grid are Godstack Term. The public surface is
`godstack/Term/term.h`. Call that API. Open `term.c` only when changing Term
itself, or when the header was used correctly and the process still dies.

The process-wide `Term term` object lives in `src/vt.c`. vt never calls the
generic `term_feed` entry points; it uses the typed feeds after a run split.
Both windowed and headless apps init with `term_init`, so Term owns the
heap cell storage either way.

The live grid that ctl `dump` returns is that same model, not a second copy.
To observe or drive a session from outside the child, use the control socket
(`docs/ctl.md`) instead of scraping the PTY.

## Byte path

Bytes move PTY master → `VtRing` → run split → typed Term feeds.

`VtRing` (`src/vt_circ_buf.c`) is a memfd ring mapped twice with `MAP_FIXED`
so a wrap is one contiguous view. Unread length is `w - r`, capped by
`VT_RING_PAGES`.

`vt_ingest` fills the ring until it is full or the PTY hits EAGAIN/EOF, drains
that unread (at most `ring.size`), and returns. It does not refill. Windowed
main presents that parse; the next loop takes the next ring. Headless loops
`while (vt_ingest())` so a file larger than the ring still parses. There is no
frame clock inside ingest. Drain loops `vt_feed_ringbuffer_to_runs` plus the
matching feeds until the ring head is empty or holds an incomplete atom. If
classify yields `nruns == 0` while bytes are still unread, that head is an
incomplete ESC or UTF-8 sequence: ingest must return and wait. Looping there
spins the CPU. `VT_RUN_MAX` (256) is a per-classify budget, not a total
session cap. A `timeout > 0` hz hold does not wait on the PTY and does not
ingest, so a flood cannot parse past one ring before present.

An SSE2 prepass classifies bytes into `VT_RUN_PRINTABLE`, `VT_RUN_ESCAPE`,
and `VT_RUN_UTF8`. Matching feeds are `term_feed_printable` (0x20–0x7E),
`term_feed_escape` (C0 / ESC / CSI / OSC, 7-bit, complete sequences only),
and `term_feed_utf8` (complete high bytes). Ground state holds between feeds.

CSI and OSC runs include their payload in the ESCAPE span (`[`, parameters,
final byte, OSC text). With default `TERM_MODE_UTF8`, bytes `0x80–0x9F`
inside OSC are payload, not 8-bit C1. A colored prompt is SGR escape runs
interleaved with printable text, not a mixture inside one run. The visible
`[` after `ESC[1;34m` is PRINTABLE.

UTF-8 atoms need real continuation bytes. A lead followed by ASCII is one
high byte, then PRINTABLE. Invalid leads become U+FFFD via `term_feed_utf8`
of `EF BF BD`, never a half-sequence left in Term. Incomplete atoms stay in
the ring until more bytes arrive.

`vt-headless --dump-runs` prints `TYPE LEN` pairs and does not feed Term. The
fixture `tests/runs.bin` matches the note on `vt_feed_ringbuffer_to_runs`
(90-byte CSI, 1KiB ASCII, 8-byte UTF-8, 1KiB ASCII).

DEBUG builds log `parse %llu ns` around drain (`peak_get_time`). Spikes on
present `end` come from WSI or the compositor (`docs/renderer.md`,
`PLAN.md`), not from this prepass.

## Grid

UTF-8 decodes into per-cell scalars with SGR 256-color and truecolor,
reverse, and underline. Box-drawing codepoints stay in the grid; the
renderer strokes them on the CPU so line joins look right. `vt_glyph_get`
runs on UTF-8 ingest for atlas traffic.

Scrollback on the primary screen is Term history (lines that left the top).
The wheel walks that history. If the child enabled mouse tracking modes
1000/1002/1003, wheel events become SGR/X10 buttons 64/65. Otherwise, on the
alternate screen, wheel sends CSI A/B.

Clipboard paste (`vt_clip_take_write`) still maps `\n` to `\r`. If the child
enabled CSI `? 2004` (`TERM_MODE_BRKTPASTE`), that write is wrapped in
`ESC [ 200 ~` … `ESC [ 201 ~`.

This is not an xterm audit and not a claim of full ECMA-48 coverage. The
child runs `bash --login` with `TERM=xterm-256color`.
