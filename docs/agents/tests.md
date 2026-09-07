# Tests

```
./build test
./vt --headless tests/glyph.txt
```

Validate: `./build test` must pass before done. It keeps the last compile mode, then `tests/check` (`tests/headless`, then `tests/tui`).

`tests/headless` `cmp`s dumps to `tests/glyph.ok`, `tests/csi.ok`, `tests/esc.ok`, `tests/del.ok`, `tests/oscutf.ok`, `tests/kitty.ok`, plus kitty-chunk / kitty-nl / kitty-query / kitty-big. `--dump-runs` vs `tests/runs.ok`, `tests/utf8ascii.ok`, `tests/kitty-runs.ok` (and kitty-chunk / kitty-query run files). `tests/badutf.bin` must not abort. Consume-1MiB checks stderr `consume 1`. `--screenshot` writes P6 PPM. If `python3`: `tests/clip`, `tests/ctl`, `tests/mux`, `tests/kitty_query`, `tests/pty_read`, `tests/visual`. Pane-drop is manual (`docs/agents/features.md`).

CI: Linux, macOS, Windows. Only `./build headless` + `tests/headless`. No windowed tests on CI.

`tests/tui` starts `./vt --live --cols 120 --rows 36`, ctl JSONL, drives nvim/lf/ncmpcpp/pi when present. Screenshots under `tests/golden/.got/`; no golden compare. `*.ppm` and `.got/` are gitignored. Automated checks use `vt --headless` or ctl `dump` / `screenshot`. Do not scrape the PTY.

## Adding a check

Ship the regression with the core change or the patch that needs it. Record a new `*.ok` from `./vt --headless` only after the dump is correct. Default `tests/headless` must pass. Patch fixtures + `tests/headless` hunks belong in that diff (`docs/agents/patches.md`). Do not add a windowed test to CI.

| Kind | Files | Wire |
|------|-------|------|
| grid / parser | `tests/foo.txt` or `.bin` + `tests/foo.ok` | `cmp` in `tests/headless` |
| run split | fixture + `--dump-runs` vs `tests/foo.ok` | `tests/headless` |
| must not abort | e.g. `tests/badutf.bin` | run, ignore dump |
| pixels | `--screenshot` P6 or ctl `screenshot` | header check; `tests/visual` for packed-cell RGB |
| live TUI | ctl `write` / `dump` in `tests/tui` | skip if app missing |
| clipboard | `tests/clip` + `tests/osc52.bin` | `tests/headless` if python3 |
| ctl read / rg | `tests/ctl` | `tests/headless` if python3 |
| vtctl --help | `./vtctl --help` | `tests/headless` |
