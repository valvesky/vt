---
name: ctl
description: >
  Drive a running vt via JSONL ctl. Use when the user asks for terminal
  changes, TUI debug, or live grid inspect. Only read / rg / write.
  Never dump unless asked. Never scrape the PTY. Triggers on ctl, vt-live,
  live grid, or /ctl.
---

Protocol: `docs/agents/ctl.md`. Client is `vtctl`. Do not paste JSONL. Do not
teach vt about vim.

# Socket

```
$XDG_RUNTIME_DIR/vt/latest.sock
```

Else `/tmp/vt-$UID/latest.sock`. Snapshot the dir if `latest.sock` is stale.
Line cap 8192. `./vtctl --help`. `--sock PATH` if `latest.sock` is wrong.

```
./vtctl read
./vtctl read 20 8
./vtctl rg needle
./vtctl log
./vtctl log present fill
./vtctl write $'\x1b'
./vtctl split
./vtctl split h
./vtctl focus 1
./vtctl panes
./vtctl move 1
./vtctl move 1 h
```

# Loop

```
vtctl read → vtctl rg needle → vtctl write keys → vtctl read
```

Default `read` is 8 rows around the tty cursor. `rg` is substring per row,
not regex. Reply `text` is `y:row` lines, 0-based grid y. Prints JSONL.

`write` is stupid: `gd`, `:w`, `ESC` (`\u001b`) are the child. Do not add
`edit` / `insert` / `vim`. Do not write files through ctl.

Never `vtctl dump` unless asked. Never `vtctl run` to drive the TUI.
`run` is off-grid `sh -c` only.
