# vt ctl agent loop

vt is the product. The JSONL socket is the agent API. If dump is too big to
read, **change dump**, not the agent.

Agents already drive ctl (`docs/ctl.md`). Full-grid `dump` of a live vim
screen is the token cost. `write` is fine. Do not scrape the PTY. Do not
teach vt about vim. Do not add a `vtctl` binary. Do not invent a second grid.

## Now

Keep `dump` for tests (`vt-headless` shape). Add a windowed read and a grid
search. Flat JSONL. No nested objects or arrays. Request line still 8192.

Parser today lifts string keys (`op`, `data`, `cmd`, `path`) and `id`.
`read` / `rg` need integer `y` / `n` on `VtCtlReq`.

### `read`

```
{op:"read"}
{op:"read","y":20,"n":8}
```

Reply: `x`, `y`, `cols`, `rows`, `text`. `text` is those rows only, same trim
as `vt_dump_walk`. Default band is around `term.cursor` (vim already moves
the tty cursor). One round trip. Do not require a prior `cursor` + `dump`.

### `rg`

```
{op:"rg","data":"vt_sel_utf8"}
```

Substring `memmem` per row. Not a regex engine. Reply is one string, rg
shape:

```
{"ok":true,"n":2,"text":"24:  vt_sel_utf8(char *dst, size_t cap)\n87:  static size_t vt_sel_utf8(...)"}
```

Cap hits and bytes. `"trunc":true` like `run`. No match objects.

### `write`

Leave it stupid. `{op:"write","data":"..."}` is raw PTY bytes. `gd`, `:w`,
`ESC` are the child, not vt. Do not add `edit` / `insert` / `vim`. Do not
write files through ctl; `:w` in the child, or the agent's file tool.

Long paste is clipboard. The 8192-byte request line is the write cap.

## Loop

```
read → rg needle → write keys → read
```

Never full `dump` unless asked. `run` stays off-grid `sh -c`. Do not drive
the TUI through `run`.

## Socket

`$XDG_RUNTIME_DIR/vt/<pid>.sock` (else `/tmp/vt-<uid>/<pid>.sock`). Stale
socks are why agents snapshot the dir. A `latest.sock` symlink is enough.
No registry.

## Agent side

Protocol is half. Stop pasting a 40-line Python RPC every turn. A skill that
finds the sock and only calls `read` / `rg` / `write` is enough. `tests/tui`
already speaks JSONL.

## Out of scope

Vim mode, LSP, jump-to-def, cell JSON, dirty bits, a fourth binary, plugins,
threads, regex, new paste path.
