# Tests

```
./build test          # current mode, ensures headless bins, then tests/check
./vt-headless tests/glyph.txt
```

`./build test` keeps the last compile mode, builds `vt-headless` and `vt-live` if needed, then `tests/check` (`tests/headless`, then `tests/tui`).

## Headless

`tests/headless` `cmp`s UTF-8 dumps to `tests/glyph.ok`, `tests/csi.ok`, `tests/esc.ok`, `tests/oscutf.ok` (OSC title with SMP UTF-8 must not leak onto my grid), `tests/kitty.ok` (APC is `VT_RUN_KITTY`, no payload on my grid). `--dump-runs` vs `tests/runs.ok`, `tests/utf8ascii.ok`, `tests/kitty-runs.ok` lock the `VtRun` split in `vt_feed_ringbuffer_to_runs`.

`tests/badutf.bin` must feed without abort. `--screenshot` must write P6 PPM. `vtctl --help` must list ops. If `python3`: `tests/clip` (ctl clipboard + OSC 52 via `vt-live`), `tests/ctl` (`read`, `rg`, `log`, `latest.sock`, `vtctl size`), `tests/mux` (ctl `split`/`focus`/`panes`/`move`; two-live `adopt`/`give` SCM_RIGHTS; dump/write stay on focus), `tests/kitty_query` (APC `a=q` reply before DA). Mouse pane-drop is manual (`docs/features.md`).

CI (`.github/workflows/ci.yml`): Linux, macOS, Windows. Only `./build headless` + `tests/headless`. I have no windowed tests on CI.

## Live TUI

`tests/tui` starts `./vt-live --cols 120 --rows 36`, speaks ctl JSONL, drives nvim, lf, ncmpcpp, and pi when present. Screenshots under `tests/golden/.got/`; no golden compare today. `*.ppm` and `.got/` are gitignored.

`cat tests/glyph.txt` is a normal terminal byte stream. Automated checks use `vt-headless` or ctl `dump` / `screenshot`. Do not scrape my PTY.

## Adding a check

Ship the regression with the core change or the patch that needs it.

| Kind | Files | Wire |
|------|-------|------|
| grid / parser | `tests/foo.txt` (cat-able) or `.bin`, expected `tests/foo.ok` | `cmp` in `tests/headless` |
| run split | fixture + `--dump-runs` vs `tests/foo.ok` | `tests/headless` |
| must not abort | e.g. `tests/badutf.bin` | run, ignore dump |
| pixels | `--screenshot` P6 or ctl `screenshot` | header check in headless; optional PPM under `tests/golden/` |
| live TUI | ctl `write` / `dump` in `tests/tui` | skip if the app is missing |
| clipboard | `tests/clip` + `tests/osc52.bin` | `tests/headless` if python3 |
| ctl read / rg / log | `tests/ctl` | `tests/headless` if python3 |
| vtctl --help | `./vtctl --help` | `tests/headless` |

Record a new `*.ok` from `./vt-headless` only after the dump is correct. Do not add a windowed test to CI. Default `tests/headless` must pass. Optional-patch fixtures + `tests/headless` hunks belong in that diff (`docs/patches.md`).
