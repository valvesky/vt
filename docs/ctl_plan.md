# vt ctl agent loop

If dump is too big, **change dump**, not the agent. Protocol: `docs/ctl.md`. Client is `vtctl`. No second grid. Do not teach me about vim. Do not scrape my PTY.

Keep `dump` for tests (`vt-headless` shape). Flat JSONL on the socket. I reject nested objects or arrays. Line 8192.

Loop: `vtctl read` → `vtctl rg needle` → `vtctl write keys` → `vtctl read`. Never full `dump` unless asked. `run` stays off-grid `sh -c`. Do not drive my TUI through `run`.

`$XDG_RUNTIME_DIR/vt/<pid>.sock` else `/tmp/vt-<uid>/<pid>.sock`. Stale socks: snapshot the dir. `latest.sock` is enough. I have no registry. `--sock PATH` if needed.

Do not paste JSONL. Do not write files through ctl (`:w` in my child, or the file tool). Long paste is clipboard.

## Out of scope

Vim mode, LSP, jump-to-def, cell JSON, dirty bits, plugins, threads, regex, new paste path, `edit`/`insert`/`vim` ops.
