# Parser and cell grid

Godstack Term. Public surface: `godstack/Term/term.h`. Open `term.c` only when changing Term, or when the header was used correctly and the process still dies.

Per-pane `Term` lives in `src/vt_mux.c` (`VtPane.term`). Focused pane is `vt_term` / `vt_term_p`. I never call generic `term_feed`; typed feeds after a run split. Windowed and headless both `term_init`, so Term owns heap cells either way. ctl `dump`/`write` stay on my focused pane, not a second copy. Drive from outside my child with ctl (`docs/ctl.md`).

## Byte path

PTY master → `VtRing` → run split → typed Term feeds.

`VtRing` (`src/vt_circ_buf.c`): `peak_mirror_map`; wrap is one contiguous view. Unread = `w - r`, cap `VT_RING_PAGES`. Linux backend is memfd + `MAP_FIXED`; callers do not mention that.

`vt_ingest` walks used panes, binds each, fills until full or EAGAIN/EOF, drains that unread (at most `ring.size`), returns. It does not refill. Windowed main presents that parse; the next loop takes the next ring. Headless: `while (vt_ingest())` so a file larger than the ring still parses. No frame clock inside ingest. Drain loops `vt_feed_ringbuffer_to_runs` + matching feeds until the ring head is empty or holds an incomplete atom. If classify yields `nruns == 0` while bytes remain, that head is incomplete ESC or UTF-8: return and wait. Looping there spins the CPU. `VT_RUN_MAX` (256) is a per-classify budget, not a session cap. `timeout > 0` hz hold does not wait on the PTY and does not ingest, so a flood cannot parse past one ring before present. After the walk, ingest rebinds my focused pane.

SSE2 prepass: `VT_RUN_PRINTABLE`, `VT_RUN_ESCAPE`, `VT_RUN_UTF8`, `VT_RUN_KITTY`. Feeds: `term_feed_printable` (0x20–0x7E), `term_feed_escape` (C0 / ESC / CSI / OSC, 7-bit, complete sequences only), `term_feed_utf8` (complete high bytes). Kitty is not a Term feed. Ground state holds between feeds.

CSI and OSC include payload in the ESCAPE span (`[`, params, final, OSC text). Kitty graphics is APC `ESC _ G … ST`: classify splits it from CSI/OSC as `VT_RUN_KITTY`. Drain calls `vt_kitty` (`src/vt_kitty.c`); Term never sees the APC. Session is per pane (`VtPane.kitty`) so mux icat does not share the b64 accum. Direct PNG (`f=100`, `t=d`), `a=T` stamp, `a=q` PTY reply. Stamp does not shrink tiles to unused LRU slots (`vt_lru_alloc` evicts). Taller than the rest of the screen → IND then NEL after. `C=0` is col 0; `C=1` keeps the cursor cell (`--place`). Do not clip `c`/`r` to the remaining rectangle — that squishes. OSC 52 is still peeled from an ESCAPE run in drain. With default `TERM_MODE_UTF8`, `0x80–0x9F` inside OSC are payload, not 8-bit C1. A colored prompt is SGR escape runs interleaved with printable, not mixed in one run. The visible `[` after `ESC[1;34m` is PRINTABLE.

UTF-8 needs real continuation bytes. Lead + ASCII = one high byte, then PRINTABLE. Invalid leads → U+FFFD via `term_feed_utf8` of `EF BF BD`; never a half-sequence in Term. Incomplete atoms stay in the ring.

`vt-headless --dump-runs` prints `TYPE LEN` and does not feed Term. `tests/runs.bin` matches the note on `vt_feed_ringbuffer_to_runs` (90-byte CSI, 1KiB ASCII, 8-byte UTF-8, 1KiB ASCII).

DEBUG: `parse %llu ns` around drain (`peak_get_time`). Spikes on present `end` are WSI/compositor (`docs/renderer.md`), not this prepass.

## Grid

UTF-8 → per-cell scalars with SGR 256-color and truecolor, reverse, underline. Box-drawing stays in my grid; renderer CPU-strokes joins. `vt_glyph_get` on UTF-8 ingest for atlas traffic.

Scrollback on primary = Term history (lines that left the top). Wheel walks that history. If my child enabled mouse 1000/1002/1003, wheel → SGR/X10 buttons 64/65. Else on alt screen, wheel → CSI A/B.

Clipboard paste (`vt_clip_take_write`) maps `\n` → `\r`. CSI `? 2004` (`TERM_MODE_BRKTPASTE`) wraps `ESC [ 200 ~` … `ESC [ 201 ~`.

I'm vt. I own my grid, my present, my ctl. Not an st fork. Not an xterm. My child still gets `TERM=xterm-256color` so ncurses has a terminfo. `bash --login`.
