# Drag and drop

Two features. ctl: `docs/ctl.md`.

## Intra-window panes

Middle-drag a live pane of mine. Edge drop splits dest, center swaps. Same
process. Any Peak that delivers MMB. Shift+MMB stays paste.

## vt → vt pane handoff

Middle-release moves a live PTY into dest vt as a new pane. My single-pane
donor dies. I use ctl + SCM_RIGHTS, not OS window DnD.

| | One-shot (release on dest) | Two-click (MMB source, then dest) |
|---|---|---|
| Linux X11 | Yes. `_NET_WM_PID` + SCM_RIGHTS | Yes (`vt/offer`) |
| Linux Wayland | No. `pointer_pid` is 0; dest `hit` dead while source holds the button | Yes. Unix SCM_RIGHTS |
| macOS | No. `pointer_pid` / `pointer_local` stub 0 | Yes. Same SCM_RIGHTS |
| Windows | No | No. `peak_sock_send` drops `pass`; no fd passing |

Wayland one-shot and Win32 fd passing are Peak later. Mux calls the header.

## File drop

OS DnD inserts a quoted path into my focused pane (bracketed paste).
Not a PTY handoff. `PEAK_EVENT_DROP`.

| | |
|---|---|
| Linux X11 | Yes. XDND |
| Windows | Yes. `WM_DROPFILES` |
| macOS | Yes. `NSFilenamesPboardType` |
| Linux Wayland | No. `wl_data_device` created, no offer/drop listener |
