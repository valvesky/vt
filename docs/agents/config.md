# Config

Knobs: `config.h`. Rebuild. No rc file, plugin ABI, or `dlopen`.

Font path, `font_size_px`, `alpha`, `vsync`, `hz`, `vt_shell_fast_pipe`, mux prefix / split / focus / kill strings, clipboard chords, `ansi_fg` / `ansi_bg`. Primary TTF missing → I do not start. Fallback and emoji paths may miss. `hz` paces consume/present (`1/hz`; 0 = unpaced).

SIGUSR1 (`peak_usr1_arm` / `theme_poll`) re-reads palette only:

1. `~/.config/omarchy/current/theme/alacritty.toml`
2. `~/.config/vt/config.toml`

First file that parses wins that reload. Missing both keeps `config.h`. Subset: `[colors.primary]` `background` / `foreground`, `[colors.normal]` and `[colors.bright]` ANSI names (`black`…`white`). Hex `#rrggbb`. Not a full settings file.

```toml
# The most boring color theme in the world
[colors.primary]
background = '#050505'
foreground = '#f5f5f5'
dim_foreground = '#6e6e6e'
bright_foreground = '#ffffff'

[colors.cursor]
cursor = '#f5f5f5'
text = '#050505'

[colors.vi_mode_cursor]
cursor = '#ffffff'
text = '#050505'

[colors.selection]
background = '#3a3a3a'
text = '#f5f5f5'

[colors.normal]
black   = '#111111'
red     = '#8f8f8f'
green   = '#c8c8c8'
yellow  = '#e0e0e0'
blue    = '#6e6e6e'
magenta = '#9a9a9a'
cyan    = '#b4b4b4'
white   = '#e8e8e8'

[colors.bright]
black   = '#3a3a3a'
red     = '#b0b0b0'
green   = '#f5f5f5'
yellow  = '#ffffff'
blue    = '#9a9a9a'
magenta = '#c8c8c8'
cyan    = '#ececec'
white   = '#ffffff'

[colors.dim]
black   = '#050505'
red     = '#5a5a5a'
green   = '#8a8a8a'
yellow  = '#9a9a9a'
blue    = '#3a3a3a'
magenta = '#6e6e6e'
cyan    = '#7a7a7a'
white   = '#9a9a9a'
```
