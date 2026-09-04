#pragma once

static PeakProc vt_shell_spawn(u32 cols, u32 rows, u32 xpixel, u32 ypixel); // bash --login
static void vt_shell_setup_term(Term *t);
static void vt_shell_resize(PeakProc *sh, u32 cols, u32 rows, u32 xpixel, u32 ypixel);
static int  vt_shell_read(PeakProc *sh, void *buf, size_t n);
static int  vt_shell_write(PeakProc *sh, const void *buf, size_t n);
static int  vt_shell_wait(PeakProc *sh, int timeout_ms);
static int  vt_shell_reap(PeakProc *sh);
static void vt_shell_close(PeakProc *sh);
