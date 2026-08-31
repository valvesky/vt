# vt ctl agent loop

If dump is too big, **change dump**, not the agent. Protocol: `docs/ctl.md`. Skill is the client. I have no `vtctl` binary. No second grid. Do not teach me about vim. Do not scrape my PTY.

Keep `dump` for tests (`vt-headless` shape). Flat JSONL. I reject nested objects or arrays. Line 8192.

Loop: `read` → `rg` → `write` → `read`. Never full `dump` unless asked. `run` stays off-grid `sh -c`. Do not drive my TUI through `run`.

`$XDG_RUNTIME_DIR/vt/<pid>.sock` else `/tmp/vt-<uid>/<pid>.sock`. Stale socks: snapshot the dir. `latest.sock` is enough. I have no registry.

Do not paste a 40-line Python RPC. Do not write files through ctl (`:w` in my child, or the file tool). Long paste is clipboard.

## Out of scope

Vim mode, LSP, jump-to-def, cell JSON, dirty bits, a fourth binary, plugins, threads, regex, new paste path, `edit`/`insert`/`vim` ops.
