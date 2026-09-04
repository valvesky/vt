#pragma once

PeakProc
vt_shell_spawn(u32 cols, u32 rows, u32 xpixel, u32 ypixel)
{
	static const char *argv[] = { "bash", "--login", NULL };

	if (vt_shell_fast_pipe)
		return peak_pipe_spawn("bash", argv, cols, rows);
	return peak_pty_spawn("bash", argv, cols, rows, xpixel, ypixel);
}

void
vt_shell_setup_term(Term *t)
{
	(void)t;
}

void
vt_shell_resize(PeakProc *sh, u32 cols, u32 rows, u32 xpixel, u32 ypixel)
{
	if (vt_shell_fast_pipe) {
		peak_pipe_resize(sh, cols, rows);
		return;
	}
	peak_pty_resize(sh, cols, rows, xpixel, ypixel);
}

int
vt_shell_read(PeakProc *sh, void *buf, size_t n)
{
	if (!sh || sh->fd == PEAK_HANDLE_INVALID)
		return 0;
	return peak_fd_read(sh->fd, buf, n);
}

int
vt_shell_write(PeakProc *sh, const void *buf, size_t n)
{
	if (!sh || sh->fd == PEAK_HANDLE_INVALID || !buf)
		return 0;
	return peak_fd_write(sh->fd, buf, n);
}

int
vt_shell_wait(PeakProc *sh, int timeout_ms)
{
	PEAK_HANDLE fd;

	if (!sh || sh->fd == PEAK_HANDLE_INVALID)
		return 0;
	fd = sh->fd;
	return peak_wait(NULL, &fd, 1, timeout_ms);
}

int
vt_shell_reap(PeakProc *sh)
{
	return peak_pty_reap(sh);
}

void
vt_shell_close(PeakProc *sh)
{
	peak_pty_close(sh);
}
