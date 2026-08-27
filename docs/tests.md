# Tests

From repo root, after `./build`:

```
./build test          # current mode, then tests/check
./vt --headless tests/glyph.txt
```

`tests/headless` `cmp`s UTF-8 dumps against `tests/glyph.ok`,
`tests/csi.ok`, and `tests/esc.ok`, `--dump-runs` against `tests/runs.ok`
and `tests/utf8ascii.ok` (the VtRun split in `vt_feed_ringbuffer_to_runs`),
feeds `tests/badutf.bin` without aborting, and checks that `--screenshot` writes a P6 PPM.
`tests/check` runs that, then `tests/tui`.
CI (`.github/workflows/ci.yml`) is Linux, macOS, and Windows; it runs
`./build headless` and `tests/headless` only.

`tests/tui` starts `./vt --headless --live --cols 120 --rows 36`, talks
ctl JSONL, and drives nvim, lf, ncmpcpp, pi. It writes ctl screenshots
to `tests/golden/.got/` but does not compare goldens. `*.ppm` and `.got/`
are gitignored.

`cat tests/glyph.txt` is the same bytes in a real terminal. Agents look
at the grid through `--headless` or ctl `dump` / `screenshot`. They do
not scrape the PTY.
