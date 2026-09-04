# Control socket

Client: `vtctl --help`. I speak JSONL: one object + LF. `$XDG_RUNTIME_DIR/vt/<pid>.sock` else `/tmp/vt-<uid>/<pid>.sock`. `latest.sock` → newest. Windowed and `vt-live`.

Loop `vtctl read` → `vtctl rg needle` → `vtctl write keys` → `vtctl read`. `write` = my PTY bytes (parser). `read`/`rg` = my live grid. `dump` = full grid (tests). `screenshot` = CPU atlas. Present/lag: stderr (`2>log`). Idle/present: `docs/agents/renderer.md`. Pane drop: `PLAN.md`. Ingest: `docs/agents/term.md`.

I echo optional `"id"`. I ignore unknown keys. I reject nested objects/arrays. Errors `{ok:false,"error":"..."}`.

Do not scrape my PTY. Do not drive my TUI through `run`. Never full `dump` unless asked.

## Operations

| Op | Request | Reply |
|----|---------|-------|
| `read` | `{op:"read"}` or `{op:"read","y":20,"n":8}` | `ok`, `x`, `y`, `cols`, `rows`, `text` (those rows, same trim as `vt_dump_walk`) |
| `rg` | `{op:"rg","data":"..."}` | `ok`, `n`, `text` (`<y>:<row>`, 0-based y). Cap → `"trunc":true` |
| `dump` | `{op:"dump"}` | `ok`, `cols`, `rows`, `text` (`vt-headless` dump shape) |
| `cursor` | `{op:"cursor"}` | `ok`, `x`, `y` |
| `size` | `{op:"size"}` | `ok`, `cols`, `rows` |
| `write` | `{op:"write","data":"..."}` | `ok`, `n` (PTY bytes). `data` is raw PTY |
| `run` | `{op:"run","cmd":"..."}` | `{ok:true,job:N}`, later `{ev:"exit",job,code,out}` |
| `screenshot` | `{op:"screenshot","path":"..."}` | `ok`. CPU-atlas P6 PPM at `path` |
| `clipboard` | `{op:"clipboard"}` or `{op:"clipboard","data":"..."}` | get: `ok`, `data`. set: `ok`. Process-local Peak slot; windowed set owns CLIPBOARD |
| `split` | `{op:"split"}` or `{op:"split","data":"h"}` | `ok`, `pane`. `data` `v` (default) or `h` |
| `focus` | `{op:"focus","n":1}` | `ok`, `pane` |
| `panes` | `{op:"panes"}` | `ok`, `n`, `focus` |
| `move` | `{op:"move","n":1}` or `{op:"move","n":1,"data":"h"}` | `ok`, `pane`. Focus onto `n`. `data` `v`/`h`/`l`/`r`/`u`/`d` split, `s` swap |
| `adopt` | `{op:"adopt","n":pid}` plus SCM_RIGHTS PTY | `ok`, `pane`. Take a live PTY from another vt. Split dest pane under pointer, else vertical on focus |
| `give` | `{op:"give"}` or `{op:"give","n":0}` | pane id: `ok`, `n`:1, `pid` (child) then one SCM_RIGHTS PTY. Bare: `ok`, `n` then `n` PTYs, no pid list. Donor detaches |
| `hit` | `{op:"hit"}` | `ok`, `hit` 0/1. `x`,`y` cells if pointer is in this window. `vt-live` is always 0 |

Pane handoff: dest `adopt` + SCM_RIGHTS. X11 one-shot: `_NET_WM_PID` then `connect` to `$runtime/vt/<pid>.sock` (not a leftover sock file); else sock-scan `hit`. Wayland/mac: source MMB writes `vt/offer`, dest MMB `give`/pull. `give` pane id includes child `pid` so dest SIGCHLD maps. I export on release before local paste. Debug: stderr (`2>log`).

`read` default `n` 8. Omit `y` → band on `term.cursor`. Reply `x`/`y` = cursor; `cols`/`rows` = screen. One round trip; no prior `cursor`+`dump`.

`rg` is `memmem` per row, not regex. One string, rg shape, e.g. `{"ok":true,"n":2,"text":"24:  vt_sel_utf8(char *dst, size_t cap)\n87:  static size_t vt_sel_utf8(...)"}`. No match objects.

`write` is raw PTY. `gd`, `:w`, `ESC` are my child. Long paste = clipboard. Cap = 8192-byte request line.

`run` is one off-grid `sh -c` in `peak_pid_cwd`. stdout/stderr never hit my grid. Second job → `busy`. Out cap 64KiB then `"trunc":true`.

Besides my child, only ctl `write` and `vt-headless` file ingest enter my parser.

## Limits

4 clients. Line 8192. `rg` 64 hits / 8192 `text`. No window or Vulkan; `vt-live` is enough.
