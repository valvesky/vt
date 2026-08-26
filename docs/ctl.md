# Control socket

JSONL, one LF-terminated record per message. Path is
`$XDG_RUNTIME_DIR/vt/<pid>.sock`, else `/tmp/vt-<uid>/<pid>.sock`.
Works in a windowed session or `--headless --live`.

Agents do not scrape the PTY. They connect here.

Optional `"id"` is echoed on the reply. Unknown keys are ignored. Nested
objects and arrays are not accepted.

## Ops

| Op | Request | Reply |
|----|---------|-------|
| `dump` | `{op:"dump"}` | `ok`, `cols`, `rows`, `text` (UTF-8 grid, same as file `--headless`) |
| `cursor` | `{op:"cursor"}` | `ok`, `x`, `y` |
| `size` | `{op:"size"}` | `ok`, `cols`, `rows` |
| `write` | `{op:"write","data":"..."}` | `ok`, `n` (bytes written to the PTY). `data` is raw PTY bytes |
| `run` | `{op:"run","cmd":"..."}` | immediate `{ok:true,job:N}` then later `{ev:"exit",job,code,out}` |
| `screenshot` | `{op:"screenshot","path":"..."}` | `ok`. CPU-atlas P6 PPM at `path` |

Errors: `{ok:false,"error":"..."}`.

`run` is one off-grid `sh -c` in the login shell's cwd (`peak_pid_cwd`).
stdout+stderr stay off the grid. One job at a time (`busy` if another is
live). `out` truncates at 64KiB and then sets `"trunc":true`.

`write` and `--headless` file ingest are the only ways bytes enter the
parser from outside the child.

## Limits

Four clients. Line cap 8192 bytes. No Peak, no Vulkan required for ctl
(`--headless --live`).
