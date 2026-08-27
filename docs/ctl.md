# Control socket

vt exposes a JSONL control socket so tools and agents can drive a running
instance without scraping the child PTY. Each message is one JSON object
terminated by a single line feed. The socket path is
`$XDG_RUNTIME_DIR/vt/<pid>.sock` when that directory exists, otherwise
`/tmp/vt-<uid>/<pid>.sock`. The same protocol works in a normal windowed
session and under `vt-live`.

The intended loop is simple: `write` sends bytes into the PTY (and therefore
into the parser), while `dump` and `screenshot` read back the live cell grid
and the CPU atlas. For present or lag questions, fill the grid, inspect it,
and search `log` for `present fill`. Idle and present coupling is covered in
`PLAN.md`; ingest edge cases are covered in `docs/term.md`.

An optional `"id"` field on a request is echoed on the reply. Unknown keys
are ignored. Nested objects and arrays are rejected. Errors always look like
`{ok:false,"error":"..."}`.

## Operations

| Op           | Request                                               | Reply                                                                                         |
|--------------|-------------------------------------------------------|-----------------------------------------------------------------------------------------------|
| `dump`       | `{op:"dump"}`                                         | `ok`, `cols`, `rows`, `text` (UTF-8 grid, same shape as `vt-headless` dump)                   |
| `cursor`     | `{op:"cursor"}`                                       | `ok`, `x`, `y`                                                                                |
| `size`       | `{op:"size"}`                                         | `ok`, `cols`, `rows`                                                                          |
| `write`      | `{op:"write","data":"..."}`                           | `ok`, `n` (bytes written to the PTY). `data` is raw PTY bytes                                 |
| `run`        | `{op:"run","cmd":"..."}`                              | immediate `{ok:true,job:N}`, then later `{ev:"exit",job,code,out}`                            |
| `screenshot` | `{op:"screenshot","path":"..."}`                      | `ok`. Writes a CPU-atlas P6 PPM at `path`                                                     |
| `clipboard`  | `{op:"clipboard"}` or `{op:"clipboard","data":"..."}` | get: `ok`, `data`. set: `ok`. Process-local Peak slot; windowed set also owns CLIPBOARD       |

`run` starts one off-grid `sh -c` in the login shell's working directory
(`peak_pid_cwd`). Its stdout and stderr never touch the terminal grid. Only
one job may be live at a time; a second request returns `busy`. Captured
output truncates at 64KiB and then sets `"trunc":true`.

Outside the child process itself, the only ways bytes enter the parser are
ctl `write` and `vt-headless` file ingest.

## Limits

At most four clients may connect. Each line is capped at 8192 bytes. The
control path does not require a window or Vulkan; `vt-live` is enough.
