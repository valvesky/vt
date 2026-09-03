# Patches

I have no plugin ABI, rc file, or `dlopen`. My tree is the config. Optional features: unified diffs `patches/vt-<version>-<name>` (`VT_MAJOR`.`VT_MINOR`.`VT_PATCH` in `src/vt.h`). Knobs stay in `config.h`. Feature work: `src/vt.c` / `src/vt.h`, or `src/vt_<name>.c` with one `#include` from the owner file.

## Apply

```
patch -p1 < patches/vt-0.3.7-name.diff
```

`.rej` → hand reconcile. Then `./build` or `./build debug`. Unpatched `tests/headless` must pass. Behavior change: fixtures + `tests/headless` hunks in the same diff.

## Write

Clean tree. One feature per diff. Small hunks. Tests in the same file.

```
git diff -- src tests config.h > patches/vt-0.3.7-name.diff
git checkout -- src tests config.h
```

Do not commit optional features into my default `src/`. Version bump → rebase the diff; do not fold into core. May add `src/vt_<name>.c` + one include from the owner TU. No registry, vtable, or `dlopen`. Mux (`src/vt_mux.c`) and kitty graphics (`src/vt_kitty.c`) are core, not patches.

## Inventory

| Patch | What |
|-------|------|
| `vt-0.4.2-theme-monokai.diff` | `config.h` ANSI palette: Monokai |
| `vt-0.4.2-theme-nord.diff` | `config.h` ANSI palette: Nord |
| `vt-0.4.2-undercurl.diff` | SGR underline as curl (GPU, CPU, screenshot) |
| `vt-0.5.3-crt.diff` | CRT scanline in `vulkan/vt.frag` |
| `vt-0.5.3-sixel.diff` | Sixel (`src/vt_sixel.c`, `VT_RUN_SIXEL`) |
