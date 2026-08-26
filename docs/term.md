# Parser and cell grid

Parser is Godstack `Term`. Read `godstack/Term/term.h`. Call it. Do not
open `term.c` unless the header was used correctly and the process still
dies.

`Term term` lives in `src/vt.c`. Feed is `term_feed` / `term_feed_ascii`
into the live cell grid (primary or alt).

## Byte path

PTY master → `CBuffer` (memfd, two `MAP_FIXED` views; wrap is one memcpy)
→ SSE2 split on `\n` / ESC / high bit → `term_feed` / `term_feed_ascii`
→ live `TermScreen`.

UTF-8 decodes into cell scalars. SGR 256 / truecolor, reverse, underline.
Box drawing is CPU-stroked in the renderer so lines join; the grid still
stores the codepoints.

Scrollback on primary is Term hist (lines that left the top). Wheel walks
that hist. On the alt screen, wheel sends CSI A/B into the PTY.

This is not an xterm audit and not a claim of ECMA-48 completeness.
`TERM=xterm-256color`. Child is `bash --login`.

Display and `dump` are the live grid, not a second model.
