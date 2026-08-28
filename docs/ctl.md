# Control socket

vt exposes a JSONL control socket so tools and agents can drive a running
instance without scraping the child PTY. Each message is one JSON object
terminated by a single line feed. The socket path is
`$XDG_RUNTIME_DIR/vt/<pid>.sock` when that directory exists, otherwise
`/tmp/vt-<uid>/<pid>.sock`. A `latest.sock` symlink in that directory points
at the most recently started instance. The same protocol works in a normal
windowed session and under `vt-live`.

The intended loop is `read` → `rg` → `write` → `read`. `write` sends bytes
into the PTY (and therefore into the parser). `read` and `rg` look at the
live cell grid; `dump` is the full-grid form kept for tests. `screenshot`
writes the CPU atlas. For present or lag questions, fill the grid, inspect
it, and `{"op":"log","data":"present fill"}` for stage lines. Idle and present coupling is
covered in `PLAN.md`; ingest edge cases are covered in `docs/term.md`.

An optional `"id"` field on a request is echoed on the reply. Unknown keys
are ignored. Nested objects and arrays are rejected. Errors always look like
`{ok:false,"error":"..."}`.

Do not scrape the PTY. Do not drive the TUI through `run`. Never full `dump`
unless asked.

## Operations

| Op           | Request                                               | Reply                                                                                         |
|--------------|-------------------------------------------------------|-----------------------------------------------------------------------------------------------|
| `read`       | `{op:"read"}` or `{op:"read","y":20,"n":8}`           | `ok`, `x`, `y`, `cols`, `rows`, `text` (those rows only, same trim as `vt_dump_walk`)          |
| `rg`         | `{op:"rg","data":"..."}`                              | `ok`, `n` (hits), `text` (`<y>:<row>` lines, 0-based grid y). Caps; then `"trunc":true`        |
| `log`        | `{op:"log"}` or `{op:"log","data":"..."}`             | `ok`, `n`, `text` (ring lines, oldest first). Optional substring filter. Caps; then `"trunc":true` |
| `dump`       | `{op:"dump"}`                                         | `ok`, `cols`, `rows`, `text` (UTF-8 grid, same shape as `vt-headless` dump)                   |
| `cursor`     | `{op:"cursor"}`                                       | `ok`, `x`, `y`                                                                                |
| `size`       | `{op:"size"}`                                         | `ok`, `cols`, `rows`                                                                          |
| `write`      | `{op:"write","data":"..."}`                           | `ok`, `n` (bytes written to the PTY). `data` is raw PTY bytes                                 |
| `run`        | `{op:"run","cmd":"..."}`                              | immediate `{ok:true,job:N}`, then later `{ev:"exit",job,code,out}`                            |
| `screenshot` | `{op:"screenshot","path":"..."}`                      | `ok`. Writes a CPU-atlas P6 PPM at `path`                                                     |
| `clipboard`  | `{op:"clipboard"}` or `{op:"clipboard","data":"..."}` | get: `ok`, `data`. set: `ok`. Process-local Peak slot; windowed set also owns CLIPBOARD       |

`read` default `n` is 8. If `y` is omitted, the band is centered on
`term.cursor`. Reply `x`/`y` are the cursor; `cols`/`rows` are the screen.
One round trip; do not require a prior `cursor` + `dump`.

`rg` is substring `memmem` per row, not a regex engine. Hits are one string
in rg shape, for example
`{"ok":true,"n":2,"text":"24:  vt_sel_utf8(char *dst, size_t cap)\n87:  static size_t vt_sel_utf8(...)"}`.
No match objects.

`write` is raw PTY bytes. `gd`, `:w`, `ESC` are the child, not vt. Long
paste is clipboard. The 8192-byte request line is the write cap.

`run` starts one off-grid `sh -c` in the login shell's working directory
(`peak_pid_cwd`). Its stdout and stderr never touch the terminal grid. Only
one job may be live at a time; a second request returns `busy`. Captured
output truncates at 64KiB and then sets `"trunc":true`.

Outside the child process itself, the only ways bytes enter the parser are
ctl `write` and `vt-headless` file ingest.

## Limits

At most four clients may connect. Each line is capped at 8192 bytes. The
control path does not require a window or Vulkan; `vt-live` is enough.
`rg` caps at 64 hits and 8192 bytes of `text`.
