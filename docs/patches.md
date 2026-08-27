# Patches

vt has no plugin ABI, no rc file, and no `dlopen` layer. The source tree is
the configuration. Small optional features ship as unified diffs under
`patches/` so a fork can stay close to default `src/` and still grow.

Patch files are named `vt-<version>-<patch_name>`, where the version comes
from `src/vt.h` (`VT_MAJOR`.`VT_MINOR`.`VT_PATCH`). An example name is
`patches/vt-0.3.7-mux.diff`. Day-to-day knobs stay in `config.h`. Most
feature work lands in `src/vt.c` / `src/vt.h`, or in a new
`src/vt_<name>.c` pulled in with a single `#include` from the owner file.

## Applying a patch

Copy the diff somewhere you can track (for example an `applied/` folder),
then from the repo root:

```
patch -p1 < patches/vt-0.3.7-name.diff
```

If the patch does not apply cleanly, inspect any `.rej` files and reconcile
by hand. After a successful apply, rebuild with `./build` or
`./build debug`. Do not invoke `gcc` on the mains yourself unless the flags
match `build.c`.

Default `tests/headless` must still pass on an unpatched tree. A patch that
changes behaviour should carry its own fixtures and any `tests/headless`
hunks inside the same diff.

## Writing a patch

Keep the working tree clean and change only what the feature needs. One
feature per diff, small hunks, tests in the same file. Prefer the guidance
in `AGENTS.md` and `PLAN.md` for where code belongs.

```
git diff -- src tests config.h > patches/vt-0.3.7-name.diff
git checkout -- src tests config.h
```

Do not commit optional features into default `src/`. When `vt.h` version
moves, rebase the diff rather than folding the feature into core to avoid
that work. A patch may add `src/vt_<name>.c` and one include from the owner
translation unit. It must not add a registry, vtable, or `dlopen` loader.

Default `src/` remains the mux-less GPU terminal. Multiplexing and similar
extras stay optional.

## Inventory

| Patch | What |
|-------|------|
| `vt-0.4.2-theme-monokai.diff` | `config.h` ANSI palette: Monokai |
| `vt-0.4.2-theme-nord.diff` | `config.h` ANSI palette: Nord |
| `vt-0.4.2-undercurl.diff` | Draw SGR underline as a curl (GPU, CPU, screenshot) |
