# Parser and cell grid

Parser is Godstack `Term`. Read `godstack/Term/term.h`. Call it. Do not
open `term.c` unless the header was used correctly and the process still
dies.

`Term term` lives in `src/vt.c`. vt does not call `term_feed` (any).
Init is `term_init` (Term-owned heap cells) for windowed and `--headless`.

## Byte path

PTY master → `VtRing` (`src/vt_circ_buf.c`: memfd, two `MAP_FIXED` views;
unread is `w - r`, cap `VT_RING_PAGES`) → `vt_ingest` (read only ring
room until EAGAIN; drain complete runs; return if unread did not shrink;
no frame clock) → `vt_feed_ring_drain`.

Drain loops `vt_feed_ringbuffer_to_runs` + feed until the ring head is
empty or an incomplete atom. If classify yields `nruns == 0` with bytes
still unread, `vt_ingest` must return and wait — that is an incomplete
ESC/UTF-8 head, not a spin. `VT_RUN_MAX` is 256 per classify call, not a
total cap.

SSE2 prepass emits `VT_RUN_PRINTABLE` / `VT_RUN_ESCAPE` / `VT_RUN_UTF8`.
Matching feeds: `term_feed_printable` (0x20–0x7E), `term_feed_escape`
(C0 / ESC / CSI / OSC; 7-bit; complete sequences), `term_feed_utf8`
(complete high bytes). Ground state between feeds.

CSI and OSC **include** their ASCII (`[`, params, `m`, OSC payload) in
the ESCAPE run. A color PS1 is SGR runs interleaved with printable
prompt text, not a mix inside one run. The visible `[` after `ESC[1;34m`
is PRINTABLE.

UTF-8 atoms require real continuation bytes. A lead followed by ASCII is
one high byte, then PRINTABLE. Invalid leads feed U+FFFD
(`term_feed_utf8` of `EF BF BD`), not a half-sequence. Incomplete atoms
stay in the ring.

`--headless --dump-runs` prints `TYPE LEN` and does not feed Term.
`tests/runs.bin` is the note in `vt_feed_ringbuffer_to_runs` (90-byte
CSI, 1KiB ASCII, 8-byte UTF-8, 1KiB ASCII).

DEBUG: `parse %llu ns` around drain (`peak_get_time`). Present `end` spikes are
WSI / compositor (`docs/renderer.md` / `PLAN.md`), not this prepass.

## Grid

UTF-8 decodes into cell scalars. SGR 256 / truecolor, reverse, underline.
Box drawing is CPU-stroked in the renderer so lines join; the grid still
stores the codepoints. `vt_glyph_get` on UTF-8 ingest.

Scrollback on primary is Term hist (lines that left the top). Wheel walks
that hist. If the child enabled mouse tracking (1000/1002/1003), wheel is
SGR/X10 buttons 64/65. Else on the alt screen, wheel sends CSI A/B.

This is not an xterm audit and not a claim of ECMA-48 completeness.
`TERM=xterm-256color`. Child is `bash --login`.

Display and ctl `dump` are the live grid, not a second model.
