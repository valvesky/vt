# Tests

From the repo root, build first, then run the suite:

```
./build test          # current mode, ensures headless bins, then tests/check
./vt-headless tests/glyph.txt
```

`./build test` keeps whatever compile mode you last used, builds `vt-headless`
and `vt-live` if needed, and hands off to `tests/check`. That script runs
`tests/headless`, then `tests/tui`.

## What headless covers

`tests/headless` compares UTF-8 grid dumps to golden files:
`tests/glyph.ok`, `tests/csi.ok`, `tests/esc.ok`, and `tests/oscutf.ok`
(an OSC title with SMP UTF-8 must not leak onto the grid). It also checks
`--dump-runs` against `tests/runs.ok` and `tests/utf8ascii.ok`, which lock
the `VtRun` split inside `vt_feed_ringbuffer_to_runs`.

Malformed input is part of the contract: `tests/badutf.bin` must feed without
aborting. `--screenshot` must write a P6 PPM. When `python3` is available,
`tests/clip` exercises ctl clipboard get/set and OSC 52 through `vt-live`.

CI (`.github/workflows/ci.yml`) runs on Linux, macOS, and Windows. It only
builds `./build headless` and runs `tests/headless`. Windowed tests stay
off CI on purpose.

## Live TUI checks

`tests/tui` starts `./vt-live --cols 120 --rows 36`, speaks ctl JSONL, and
drives nvim, lf, ncmpcpp, and pi when those binaries exist. It writes ctl
screenshots under `tests/golden/.got/` but does not compare goldens today.
`*.ppm` and `.got/` are gitignored.

`cat tests/glyph.txt` is the same byte stream you can send through a normal
terminal. Automated checks should look at the grid through `vt-headless` or
ctl `dump` / `screenshot`, never by scraping the PTY.

## Adding a check

Ship a regression with the core change or with the patch that needs it.

| Kind           | Files                                                         | Wire                                                            |
|----------------|---------------------------------------------------------------|-----------------------------------------------------------------|
| grid / parser  | `tests/foo.txt` (cat-able) or `.bin`, expected `tests/foo.ok` | `cmp` in `tests/headless`                                       |
| run split      | fixture + `--dump-runs` vs `tests/foo.ok`                     | `tests/headless`                                                |
| must not abort | e.g. `tests/badutf.bin`                                       | run, ignore dump                                                |
| pixels         | `--screenshot` P6 or ctl `screenshot`                         | header check in headless; optional PPM under `tests/golden/`    |
| live TUI       | ctl `write` / `dump` in `tests/tui`                           | skip if the app is missing                                      |
| clipboard      | `tests/clip` + `tests/osc52.bin`                              | `tests/headless` if python3                                     |

Record a new `*.ok` from `./vt-headless` only after the dump is correct.
Do not add a windowed test to CI. Default `tests/headless` must pass with no
patch applied. A patch's fixtures and any `tests/headless` hunks belong in
that diff; see `docs/patches.md` and `PLAN.md`.
