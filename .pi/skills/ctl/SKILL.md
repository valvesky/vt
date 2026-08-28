---
name: ctl
description: >
  Drive a running vt via JSONL ctl. Use when the user asks for terminal
  changes, TUI debug, or live grid inspect. Only read / rg / write.
  Never dump unless asked. Never scrape the PTY. Triggers on ctl, vt-live,
  live grid, or /ctl.
---

Protocol: `docs/ctl.md`. This skill is the client. Do not paste a 40-line
RPC. Do not add a `vtctl` binary. Do not teach vt about vim.

# Socket

```
$XDG_RUNTIME_DIR/vt/latest.sock
```

Else `/tmp/vt-$UID/latest.sock`. Snapshot the dir if `latest.sock` is stale.
Each request is one JSON object plus a single line feed. Line cap 8192.

```
python3 -c '
import json,os,socket,sys
if os.environ.get("XDG_RUNTIME_DIR"):
    p=os.path.join(os.environ["XDG_RUNTIME_DIR"],"vt","latest.sock")
else:
    p="/tmp/vt-%d/latest.sock"%os.getuid()
s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); s.settimeout(8); s.connect(p)
s.sendall((json.dumps(json.loads(sys.argv[1]),separators=(",",":"))+"\n").encode()); s.shutdown(socket.SHUT_WR)
print(s.recv(65536).decode(),end="")
' '{"op":"read"}'
```

# Loop

```
read → rg needle → write keys → read
```

| Op | Request |
|----|---------|
| `read` | `{"op":"read"}` or `{"op":"read","y":20,"n":8}` |
| `rg` | `{"op":"rg","data":"needle"}` |
| `log` | `{"op":"log"}` or `{"op":"log","data":"present fill"}` |
| `write` | `{"op":"write","data":"..."}` raw PTY bytes |

Default `read` is 8 rows around the tty cursor. `rg` is substring per row,
not regex. Reply `text` is `y:row` lines, 0-based grid y.

`write` is stupid: `gd`, `:w`, `ESC` (`\u001b`) are the child. Do not add
`edit` / `insert` / `vim`. Do not write files through ctl.

Never `{op:"dump"}` unless asked. Never `{op:"run"}` to drive the TUI.
`run` is off-grid `sh -c` only.
