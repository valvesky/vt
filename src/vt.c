#define _GNU_SOURCE

#ifdef DEBUG
#define P_LOG_DEBUG_ENABLED 1
#define P_LOG_TRACE_ENABLED 1
#endif

#include "term.h"
#include "term.c"

#ifndef VT_HEADLESS
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "rend.h"
#endif
#include "peak.c"
#ifndef VT_HEADLESS
#include "rend.c"
#pragma GCC diagnostic pop
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "lib/stb_truetype.h"

#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <immintrin.h>
#include <emmintrin.h>

#include "vt.h"
#include "config.h"
#include "vt_debug.h"
#include "vt_circ_buf.c"
#include "vt_lru.c"
#include "vt_renderer.c"

#define VT_CTL_CLIENTS 4
#define VT_CTL_LINE 8192
#define VT_CTL_JOB_OUT 65536

typedef struct VtCtlClient {
	PEAK_HANDLE fd;
	u32 n;
	char buf[VT_CTL_LINE];
} VtCtlClient;

typedef struct VtCtlReq {
	const char *id;
	int id_n;
	const char *op;
	int op_n;
	const char *data;
	int data_n;
	const char *cmd;
	int cmd_n;
	const char *path;
	int path_n;
} VtCtlReq;

typedef struct VtCtlJob {
	int pid;
	PEAK_HANDLE fd;
	int client;
	int id_n;
	u32 seq;
	u32 out_n;
	bool trunc;
	char id[96];
	char out[VT_CTL_JOB_OUT];
} VtCtlJob;

static Term term;
static VtRing ring;
static PeakProc sh = { PEAK_HANDLE_INVALID, 0 };
static PEAK_HANDLE ctl_listen = PEAK_HANDLE_INVALID;
static char ctl_path[256];
static VtCtlClient ctl_clients[VT_CTL_CLIENTS];
static VtCtlJob ctl_job;

static bool running = true;
static bool vt_headless;

static bool vt_init_term(u32 cols, u32 rows);
static bool vt_init(u32 cols, u32 rows);
static void vt_destroy(void);
static int vt_utf8_encode(codepoint_t c, char out[4]);
static codepoint_t vt_utf8_decode(const char *s, u32 n);
static void vt_dump_screen(FILE *out);
static int vt_headless_run(const char *path, const char *shot, u32 cols, u32 rows);
static int vt_headless_live_run(u32 cols, u32 rows);
#ifndef VT_HEADLESS
static void vt_resize(u32 cols, u32 rows);
#endif
static void vt_shell_gone(void);
static size_t vt_sh_read(void);
static size_t vt_sh_write(const char *const src, size_t len);
static void vt_ingest(const char *data, size_t len);
#ifndef VT_HEADLESS
static void vt_key(const PeakEvent *event);
#endif
static void vt_ring_drain(void);
#ifndef VT_HEADLESS
static void vt_mouse_wheel(const PeakEvent *event);
static void vt_peak_pump(bool *dirty);
#endif
static void vt_wait(bool ready);
#ifndef VT_HEADLESS
static void vt_present(void);
#endif
static int vt_ctl_skip_ws(const char *s, int i);
static int vt_ctl_parse_string(const char *s, int i, const char **out, int *n);
static int vt_ctl_parse(const char *s, VtCtlReq *req);
static int vt_ctl_unescape(const char *s, int n, char *dst, size_t cap);
static int vt_ctl_put(PEAK_HANDLE fd, const char *p, size_t n);
static int vt_ctl_puts(PEAK_HANDLE fd, const char *s);
static int vt_ctl_put_escaped(PEAK_HANDLE fd, const char *p, size_t n);
static int vt_ctl_put_prefix(PEAK_HANDLE fd, const char *id, int id_n, int ok);
static void vt_ctl_client_close(VtCtlClient *c);
static void vt_ctl_reply_err(VtCtlClient *c, const char *id, int id_n, const char *err);
static void vt_ctl_job_reap(void);
static void vt_ctl_job_read(void);
static void vt_ctl_job_kill(void);
static void vt_ctl_handle_line(VtCtlClient *c, char *line);
static void vt_ctl_client_read(VtCtlClient *c);
static void vt_ctl_accept(void);
static void vt_ctl_pump(void);
static bool vt_ctl_init(void);
static void vt_ctl_destroy(void);

static bool
vt_init_term(u32 cols, u32 rows)
{
	TermColors colors;

	memset(&colors, 0, sizeof colors);
	memcpy(colors.fg, ansi_fg, sizeof colors.fg);
	memcpy(colors.bg, ansi_bg, sizeof colors.bg);
	colors.fg_default = (uint32_t)fg_color;
	colors.bg_default = (uint32_t)bg_color;
	sh.fd = PEAK_HANDLE_INVALID;
	sh.pid = 0;
	if (!vt_ring_init(&ring, VT_RING_PAGES)) {
		VTFATAL("vt_ring_init");
		return false;
	}
#ifndef VT_HEADLESS
	if (!vt_headless) {
		if (!renderer_cells_init(cols, rows)) {
			vt_ring_destroy(&ring);
			return false;
		}
		if (!term_init_on(&term, cols, rows, &colors,
				renderer_screen_cells(), renderer_alt_cells(), cols * rows)) {
			vt_ring_destroy(&ring);
			return false;
		}
		return true;
	}
#endif
	if (!term_init(&term, cols, rows, &colors)) {
		vt_ring_destroy(&ring);
		return false;
	}
	return true;
}

static bool
vt_init(u32 cols, u32 rows)
{
	static const char *argv[] = { "bash", "--login", NULL };
	PeakProc proc;

	if (!vt_init_term(cols, rows))
		return false;

#if defined(_WIN32)
	SetEnvironmentVariableA("TERM", "xterm-256color");
	SetEnvironmentVariableA("COLUMNS", NULL);
	SetEnvironmentVariableA("LINES", NULL);
#else
	setenv("TERM", "xterm-256color", 1);
	unsetenv("COLUMNS");
	unsetenv("LINES");
#endif
	proc = peak_pty_spawn("bash", argv, cols, rows,
			cols * atlas.cell_width, rows * atlas.cell_height);
	if (proc.fd == PEAK_HANDLE_INVALID) {
		VTFATAL("Could not open tty.");
		return false;
	}
	sh = proc;
	if (!vt_ctl_init())
		VTERROR("ctl socket disabled");
	return true;
}

static void
vt_destroy(void)
{
	vt_ctl_destroy();
	VTINFO("[Shell %-d] Exited successfully", sh.pid);
	peak_pty_close(&sh);
	term_destroy(&term);
	vt_ring_destroy(&ring);
}

#ifndef VT_HEADLESS
static void
vt_resize(u32 cols, u32 rows)
{
	TermScreen *s;

	if (!cols || !rows)
		return;
	s = term_screen(&term);
	if (s && s->cols == cols && s->rows == rows)
		return;
	if (!renderer_cells_resize(cols, rows))
		return;
	term_resize_on(&term, cols, rows,
			renderer_screen_cells(), renderer_alt_cells(), cols * rows);
	peak_pty_resize(&sh, cols, rows,
			cols * atlas.cell_width, rows * atlas.cell_height);
}

static void
vt_key(const PeakEvent *event)
{
	static const struct {
		char s[5];
		u8 n;
	} seq[] = {
		[PEAK_KEY_UP] = { "\033[A", 3 },
		[PEAK_KEY_DOWN] = { "\033[B", 3 },
		[PEAK_KEY_LEFT] = { "\033[D", 3 },
		[PEAK_KEY_RIGHT] = { "\033[C", 3 },
		[PEAK_KEY_ESCAPE] = { "\x1b", 1 },
		[PEAK_KEY_ENTER] = { "\r", 1 },
		[PEAK_KEY_BACKSPACE] = { "\x7f", 1 },
		[PEAK_KEY_TAB] = { "\t", 1 },
		[PEAK_KEY_DELETE] = { "\033[3~", 4 },
	};
	PeakKeyCode key;
	PeakKeyMod mod;
	uint32_t code;
	char ch;

	key = event->key.key;
	mod = event->key.mod;
	code = event->key.code;

	if (key == PEAK_KEY_TAB && mod == PEAK_KEYMOD_SHIFT) {
		vt_sh_write("\033[Z", 3);
		return;
	}
	if ((u32)key < LEN(seq) && seq[key].n) {
		vt_sh_write(seq[key].s, seq[key].n);
		return;
	}

	if (mod == PEAK_KEYMOD_CTRL) {
		if (key >= PEAK_KEY_A && key <= PEAK_KEY_Z) {
			ch = (char)(1 + (key - PEAK_KEY_A));
			vt_sh_write(&ch, 1);
			return;
		}
		if (code >= 1 && code < 32) {
			ch = (char)code;
			vt_sh_write(&ch, 1);
			return;
		}
	}

	if (code >= 32 && code < 127) {
		ch = (char)code;
		vt_sh_write(&ch, 1);
		return;
	}

	if (key >= PEAK_KEY_0 && key <= PEAK_KEY_9) {
		ch = (char)('0' + (key - PEAK_KEY_0));
		vt_sh_write(&ch, 1);
		return;
	}
	if (key >= PEAK_KEY_A && key <= PEAK_KEY_Z) {
		ch = (char)('a' + (key - PEAK_KEY_A));
		if (mod == PEAK_KEYMOD_SHIFT || mod == PEAK_KEYMOD_CAPS)
			ch = (char)(ch - 32);
		vt_sh_write(&ch, 1);
	}
}
#endif

static void
vt_shell_gone(void)
{
	if (sh.pid > 0)
		peak_pty_reap(&sh);
	running = false;
}

static size_t
vt_sh_read(void)
{
	size_t total;

	total = 0;
	if (sh.fd == PEAK_HANDLE_INVALID)
		return 0;
	for (;;) {
		int r;
		char data[16000];

		r = peak_fd_read(sh.fd, data, sizeof data);
		if (r > 0) {
			vt_ingest(data, (size_t)r);
			total += (size_t)r;
			continue;
		}
		if (r == 0) {
			vt_shell_gone();
			break;
		}
		break;
	}
	return total;
}

static size_t
vt_sh_write(const char *const src, size_t len)
{
	int r;

	if (sh.fd == PEAK_HANDLE_INVALID)
		return 0;
	r = peak_fd_write(sh.fd, src, len);
	if (r <= 0) {
		if (r == 0)
			vt_shell_gone();
		return 0;
	}
	return (size_t)r;
}

#ifndef VT_HEADLESS
static void
vt_mouse_wheel(const PeakEvent *event)
{
	u32 cols, rows;
	u32 cw, ch;
	int x, y;
	int btn;
	char buf[32];
	int n;

	renderer_get_grid(&renderer, &cols, &rows);
	cw = atlas.cell_width;
	ch = atlas.cell_height;
	if (!cw || !ch || !cols || !rows)
		return;
	x = (int)event->pointer.x / (int)cw + 1;
	y = (int)event->pointer.y / (int)ch + 1;
	if (x < 1)
		x = 1;
	if (y < 1)
		y = 1;
	if (x > (int)cols)
		x = (int)cols;
	if (y > (int)rows)
		y = (int)rows;
	btn = event->pointer.type == PEAK_POINTER_WHEEL_UP ? 64 : 65;
	if (term.mode & TERM_MODE_MOUSESGR) {
		n = snprintf(buf, sizeof buf, "\033[<%d;%d;%dM", btn, x, y);
		if (n > 0)
			vt_sh_write(buf, (size_t)n);
		return;
	}
	if (x > 223)
		x = 223;
	if (y > 223)
		y = 223;
	buf[0] = '\033';
	buf[1] = '[';
	buf[2] = 'M';
	buf[3] = (char)(32 + btn);
	buf[4] = (char)(32 + x);
	buf[5] = (char)(32 + y);
	vt_sh_write(buf, 6);
}

static void
vt_peak_pump(bool *dirty)
{
	PeakEvent event;

	while (peak_window_epoll(&win, &event)) {
		switch (event.type) {
		case PEAK_EVENT_WINDOW_CLOSE:
			running = false;
			break;
		case PEAK_EVENT_WINDOW_RESIZE:
			renderer_resize(event.resize.width, event.resize.height);
			if (sh.fd != PEAK_HANDLE_INVALID) {
				u32 cols, rows;

				renderer_get_grid(&renderer, &cols, &rows);
				vt_resize(cols, rows);
			}
			if (dirty)
				*dirty = true;
			break;
		case PEAK_EVENT_KEY_DOWN:
			vt_key(&event);
			break;
		case PEAK_EVENT_POINTER:
			if (event.pointer.state == PEAK_POINTER_PRESSED
					&& (event.pointer.type == PEAK_POINTER_WHEEL_UP
							|| event.pointer.type == PEAK_POINTER_WHEEL_DOWN)) {
				if (term.mode & TERM_MODE_MOUSE)
					vt_mouse_wheel(&event);
				else if (term.mode & TERM_MODE_ALTSCREEN)
					vt_sh_write(event.pointer.type == PEAK_POINTER_WHEEL_UP
							? "\033[A" : "\033[B", 3);
			}
			break;
		default:
			break;
		}
	}
}
#endif

static int
vt_ctl_skip_ws(const char *s, int i)
{
	while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')
		i++;
	return i;
}

static int
vt_ctl_parse_string(const char *s, int i, const char **out, int *n)
{
	int start;

	if (s[i] != '"')
		return -1;
	i++;
	start = i;
	while (s[i] && s[i] != '"') {
		if (s[i] == '\\') {
			if (!s[i + 1])
				return -1;
			i += 2;
			continue;
		}
		i++;
	}
	if (s[i] != '"')
		return -1;
	*out = s + start;
	*n = i - start;
	return i + 1;
}

static int
vt_ctl_parse(const char *s, VtCtlReq *req)
{
	int i;

	memset(req, 0, sizeof *req);
	i = vt_ctl_skip_ws(s, 0);
	if (s[i++] != '{')
		return 0;
	for (;;) {
		const char *key;
		int kn;
		int raw0;

		i = vt_ctl_skip_ws(s, i);
		if (s[i] == '}')
			return 1;
		i = vt_ctl_parse_string(s, i, &key, &kn);
		if (i < 0)
			return 0;
		i = vt_ctl_skip_ws(s, i);
		if (s[i++] != ':')
			return 0;
		i = vt_ctl_skip_ws(s, i);
		if (s[i] == '"') {
			const char *val;
			int vn;

			raw0 = i;
			i = vt_ctl_parse_string(s, i, &val, &vn);
			if (i < 0)
				return 0;
			if (kn == 2 && key[0] == 'i' && key[1] == 'd') {
				req->id = s + raw0;
				req->id_n = i - raw0;
			} else if (kn == 2 && key[0] == 'o' && key[1] == 'p') {
				req->op = val;
				req->op_n = vn;
			} else if (kn == 4 && memcmp(key, "data", 4) == 0) {
				req->data = val;
				req->data_n = vn;
			} else if (kn == 3 && memcmp(key, "cmd", 3) == 0) {
				req->cmd = val;
				req->cmd_n = vn;
			} else if (kn == 4 && memcmp(key, "path", 4) == 0) {
				req->path = val;
				req->path_n = vn;
			}
		} else {
			raw0 = i;
			if (s[i] == '-')
				i++;
			if (s[i] >= '0' && s[i] <= '9') {
				while (s[i] >= '0' && s[i] <= '9')
					i++;
			} else if (strncmp(s + i, "true", 4) == 0) {
				i += 4;
			} else if (strncmp(s + i, "false", 5) == 0) {
				i += 5;
			} else if (strncmp(s + i, "null", 4) == 0) {
				i += 4;
			} else {
				return 0;
			}
			if (kn == 2 && key[0] == 'i' && key[1] == 'd') {
				req->id = s + raw0;
				req->id_n = i - raw0;
			}
		}
		i = vt_ctl_skip_ws(s, i);
		if (s[i] == ',') {
			i++;
			continue;
		}
		if (s[i] == '}')
			return 1;
		return 0;
	}
}

static int
vt_ctl_unescape(const char *s, int n, char *dst, size_t cap)
{
	int i, o;

	o = 0;
	for (i = 0; i < n; i++) {
		unsigned char ch;

		if ((size_t)o + 5 >= cap)
			return -1;
		if (s[i] != '\\') {
			dst[o++] = s[i];
			continue;
		}
		i++;
		if (i >= n)
			return -1;
		ch = (unsigned char)s[i];
		if (ch == '"' || ch == '\\' || ch == '/')
			dst[o++] = (char)ch;
		else if (ch == 'n')
			dst[o++] = '\n';
		else if (ch == 'r')
			dst[o++] = '\r';
		else if (ch == 't')
			dst[o++] = '\t';
		else if (ch == 'b')
			dst[o++] = '\b';
		else if (ch == 'f')
			dst[o++] = '\f';
		else if (ch == 'u') {
			int v, h, k;
			char tmp[4], hc;
			int tn;

			if (i + 4 >= n)
				return -1;
			v = 0;
			for (k = 0; k < 4; k++) {
				hc = s[i + 1 + k];
				if (hc >= '0' && hc <= '9')
					h = hc - '0';
				else if (hc >= 'a' && hc <= 'f')
					h = hc - 'a' + 10;
				else if (hc >= 'A' && hc <= 'F')
					h = hc - 'A' + 10;
				else
					return -1;
				v = (v << 4) | h;
			}
			i += 4;
			if (v > 0x10FFFF)
				return -1;
			tn = vt_utf8_encode((codepoint_t)v, tmp);
			if ((size_t)o + (size_t)tn >= cap)
				return -1;
			memcpy(dst + o, tmp, (size_t)tn);
			o += tn;
		} else {
			return -1;
		}
	}
	dst[o] = 0;
	return o;
}

static int
vt_ctl_put(PEAK_HANDLE fd, const char *p, size_t n)
{
	while (n) {
		int w;

		w = peak_fd_write(fd, p, n);
		if (w <= 0)
			return -1;
		p += (size_t)w;
		n -= (size_t)w;
	}
	return 0;
}

static int
vt_ctl_puts(PEAK_HANDLE fd, const char *s)
{
	return vt_ctl_put(fd, s, strlen(s));
}

static int
vt_ctl_put_escaped(PEAK_HANDLE fd, const char *p, size_t n)
{
	static const char hex[] = "0123456789abcdef";
	size_t i, start;

	start = 0;
	for (i = 0; i < n; i++) {
		unsigned char ch;
		const char *esc;
		size_t elen;
		char u[6];

		ch = (unsigned char)p[i];
		if (ch != '"' && ch != '\\' && ch >= 0x20)
			continue;
		if (i > start && vt_ctl_put(fd, p + start, i - start) < 0)
			return -1;
		if (ch == '"') {
			esc = "\\\"";
			elen = 2;
		} else if (ch == '\\') {
			esc = "\\\\";
			elen = 2;
		} else if (ch == '\n') {
			esc = "\\n";
			elen = 2;
		} else if (ch == '\r') {
			esc = "\\r";
			elen = 2;
		} else if (ch == '\t') {
			esc = "\\t";
			elen = 2;
		} else {
			u[0] = '\\';
			u[1] = 'u';
			u[2] = '0';
			u[3] = '0';
			u[4] = hex[ch >> 4];
			u[5] = hex[ch & 15];
			esc = u;
			elen = 6;
		}
		if (vt_ctl_put(fd, esc, elen) < 0)
			return -1;
		start = i + 1;
	}
	if (n > start)
		return vt_ctl_put(fd, p + start, n - start);
	return 0;
}

static int
vt_ctl_put_prefix(PEAK_HANDLE fd, const char *id, int id_n, int ok)
{
	if (vt_ctl_put(fd, "{", 1) < 0)
		return -1;
	if (id && id_n > 0) {
		if (vt_ctl_puts(fd, "\"id\":") < 0)
			return -1;
		if (vt_ctl_put(fd, id, (size_t)id_n) < 0)
			return -1;
		if (vt_ctl_put(fd, ",", 1) < 0)
			return -1;
	}
	if (vt_ctl_puts(fd, "\"ok\":") < 0)
		return -1;
	return vt_ctl_puts(fd, ok ? "true" : "false");
}

static void
vt_ctl_client_close(VtCtlClient *c)
{
	if (!c || c->fd == PEAK_HANDLE_INVALID)
		return;
	if (ctl_job.client >= 0 && c == &ctl_clients[ctl_job.client])
		ctl_job.client = -1;
	peak_fd_close(c->fd);
	c->fd = PEAK_HANDLE_INVALID;
	c->n = 0;
}

static void
vt_ctl_reply_err(VtCtlClient *c, const char *id, int id_n, const char *err)
{
	if (c->fd == PEAK_HANDLE_INVALID)
		return;
	if (vt_ctl_put_prefix(c->fd, id, id_n, 0) < 0
			|| vt_ctl_puts(c->fd, ",\"error\":\"") < 0
			|| vt_ctl_puts(c->fd, err) < 0
			|| vt_ctl_puts(c->fd, "\"}\n") < 0)
		vt_ctl_client_close(c);
}

static void
vt_ctl_job_reap(void)
{
	PeakProc job;
	VtCtlClient *c;
	int code, n;
	char head[80];

	if (ctl_job.pid <= 0)
		return;
	job.fd = ctl_job.fd;
	job.pid = ctl_job.pid;
	if (!peak_job_reap(&job, &code))
		return;
	ctl_job.pid = 0;
	if (ctl_job.fd != PEAK_HANDLE_INVALID) {
		peak_fd_close(ctl_job.fd);
		ctl_job.fd = PEAK_HANDLE_INVALID;
	}
	if (ctl_job.client < 0 || ctl_job.client >= VT_CTL_CLIENTS)
		return;
	c = &ctl_clients[ctl_job.client];
	if (c->fd == PEAK_HANDLE_INVALID)
		return;
	n = snprintf(head, sizeof head, "{\"ev\":\"exit\",\"job\":%u,", ctl_job.seq);
	if (n < 0 || (size_t)n >= sizeof head
			|| vt_ctl_put(c->fd, head, (size_t)n) < 0)
		goto drop;
	if (ctl_job.id_n > 0) {
		if (vt_ctl_puts(c->fd, "\"id\":") < 0
				|| vt_ctl_put(c->fd, ctl_job.id, (size_t)ctl_job.id_n) < 0
				|| vt_ctl_put(c->fd, ",", 1) < 0)
			goto drop;
	}
	n = snprintf(head, sizeof head, "\"code\":%d,\"out\":\"", code);
	if (n < 0 || (size_t)n >= sizeof head
			|| vt_ctl_put(c->fd, head, (size_t)n) < 0
			|| vt_ctl_put_escaped(c->fd, ctl_job.out, ctl_job.out_n) < 0)
		goto drop;
	if (ctl_job.trunc && vt_ctl_puts(c->fd, "\",\"trunc\":true}\n") < 0)
		goto drop;
	if (!ctl_job.trunc && vt_ctl_puts(c->fd, "\"}\n") < 0)
		goto drop;
	return;
drop:
	vt_ctl_client_close(c);
}

static void
vt_ctl_job_read(void)
{
	if (ctl_job.pid <= 0 || ctl_job.fd == PEAK_HANDLE_INVALID)
		return;
	for (;;) {
		char buf[4096];
		int r;
		u32 room;

		r = peak_fd_read(ctl_job.fd, buf, sizeof buf);
		if (r > 0) {
			room = VT_CTL_JOB_OUT - ctl_job.out_n;
			if ((u32)r > room) {
				if (room)
					memcpy(ctl_job.out + ctl_job.out_n, buf, room);
				ctl_job.out_n = VT_CTL_JOB_OUT;
				ctl_job.trunc = true;
			} else {
				memcpy(ctl_job.out + ctl_job.out_n, buf, (size_t)r);
				ctl_job.out_n += (u32)r;
			}
			continue;
		}
		if (r < 0)
			return;
		peak_fd_close(ctl_job.fd);
		ctl_job.fd = PEAK_HANDLE_INVALID;
		vt_ctl_job_reap();
		return;
	}
}

static void
vt_ctl_job_kill(void)
{
	PeakProc job;

	job.fd = ctl_job.fd;
	job.pid = ctl_job.pid;
	peak_job_kill(&job);
	ctl_job.fd = PEAK_HANDLE_INVALID;
	ctl_job.pid = 0;
	ctl_job.client = -1;
	ctl_job.out_n = 0;
	ctl_job.trunc = false;
}

static void
vt_ctl_handle_line(VtCtlClient *c, char *line)
{
	VtCtlReq req;

	if (!line[0])
		return;
	if (!vt_ctl_parse(line, &req) || !req.op) {
		vt_ctl_reply_err(c, req.id, req.id_n, "bad json");
		return;
	}
	if (req.op_n == 4 && memcmp(req.op, "dump", 4) == 0) {
		TermScreen *s;
		FILE *m;
		char *text;
		size_t len;
		long nlong;
		char mid[80];
		int n;

		s = term_screen(&term);
		text = NULL;
		len = 0;
		m = tmpfile();
		if (!m)
			goto dump_fail;
		vt_dump_screen(m);
		if (fseek(m, 0, SEEK_END) != 0)
			goto dump_fail;
		nlong = ftell(m);
		if (nlong < 0 || fseek(m, 0, SEEK_SET) != 0)
			goto dump_fail;
		len = (size_t)nlong;
		text = malloc(len + 1);
		if (!text)
			goto dump_fail;
		if (len && fread(text, 1, len, m) != len)
			goto dump_fail;
		if (fclose(m) != 0) {
			m = NULL;
			goto dump_fail;
		}
		m = NULL;
		text[len] = '\0';
		n = snprintf(mid, sizeof mid, ",\"cols\":%u,\"rows\":%u,\"text\":\"",
				s->cols, s->rows);
		if (n < 0 || (size_t)n >= sizeof mid
				|| vt_ctl_put_prefix(c->fd, req.id, req.id_n, 1) < 0
				|| vt_ctl_put(c->fd, mid, (size_t)n) < 0
				|| vt_ctl_put_escaped(c->fd, text, len) < 0
				|| vt_ctl_puts(c->fd, "\"}\n") < 0)
			vt_ctl_client_close(c);
		free(text);
		return;
	dump_fail:
		if (m)
			fclose(m);
		free(text);
		vt_ctl_reply_err(c, req.id, req.id_n, "dump failed");
	} else if (req.op_n == 6 && memcmp(req.op, "cursor", 6) == 0) {
		char tail[64];
		int n;

		n = snprintf(tail, sizeof tail, ",\"x\":%u,\"y\":%u}\n",
				term.cursor.x, term.cursor.y);
		if (n < 0 || (size_t)n >= sizeof tail) {
			vt_ctl_reply_err(c, req.id, req.id_n, "cursor failed");
			return;
		}
		if (vt_ctl_put_prefix(c->fd, req.id, req.id_n, 1) < 0
				|| vt_ctl_put(c->fd, tail, (size_t)n) < 0)
			vt_ctl_client_close(c);
	} else if (req.op_n == 4 && memcmp(req.op, "size", 4) == 0) {
		TermScreen *s;
		char tail[64];
		int n;

		s = term_screen(&term);
		n = snprintf(tail, sizeof tail, ",\"cols\":%u,\"rows\":%u}\n", s->cols, s->rows);
		if (n < 0 || (size_t)n >= sizeof tail) {
			vt_ctl_reply_err(c, req.id, req.id_n, "size failed");
			return;
		}
		if (vt_ctl_put_prefix(c->fd, req.id, req.id_n, 1) < 0
				|| vt_ctl_put(c->fd, tail, (size_t)n) < 0)
			vt_ctl_client_close(c);
	} else if (req.op_n == 5 && memcmp(req.op, "write", 5) == 0) {
		char data[VT_CTL_LINE];
		char tail[32];
		int n, wn;

		if (!req.data) {
			vt_ctl_reply_err(c, req.id, req.id_n, "missing data");
			return;
		}
		n = vt_ctl_unescape(req.data, req.data_n, data, sizeof data);
		if (n < 0) {
			vt_ctl_reply_err(c, req.id, req.id_n, "bad json");
			return;
		}
		if (sh.fd == PEAK_HANDLE_INVALID) {
			vt_ctl_reply_err(c, req.id, req.id_n, "no pty");
			return;
		}
		wn = (int)vt_sh_write(data, (size_t)n);
		n = snprintf(tail, sizeof tail, ",\"n\":%d}\n", wn);
		if (n < 0 || (size_t)n >= sizeof tail
				|| vt_ctl_put_prefix(c->fd, req.id, req.id_n, 1) < 0
				|| vt_ctl_put(c->fd, tail, (size_t)n) < 0)
			vt_ctl_client_close(c);
	} else if (req.op_n == 10 && memcmp(req.op, "screenshot", 10) == 0) {
		char path[VT_CTL_LINE];
		TermScreen *s;
		int n;

		if (!req.path) {
			vt_ctl_reply_err(c, req.id, req.id_n, "missing path");
			return;
		}
		n = vt_ctl_unescape(req.path, req.path_n, path, sizeof path);
		if (n < 0) {
			vt_ctl_reply_err(c, req.id, req.id_n, "bad json");
			return;
		}
		if (n == 0 || path[0] == 0) {
			vt_ctl_reply_err(c, req.id, req.id_n, "empty path");
			return;
		}
		if (!atlas.atlas) {
			vt_ctl_reply_err(c, req.id, req.id_n, "no atlas");
			return;
		}
		s = term_screen(&term);
		if (!renderer_screenshot_ppm(s, term.cursor.x, term.cursor.y,
				(term.cursor.fg << 8) | term.cursor.attr, term.cursor.bg << 8, path)) {
			vt_ctl_reply_err(c, req.id, req.id_n, "screenshot failed");
			return;
		}
		if (vt_ctl_put_prefix(c->fd, req.id, req.id_n, 1) < 0
				|| vt_ctl_puts(c->fd, "}\n") < 0)
			vt_ctl_client_close(c);
	} else if (req.op_n == 3 && memcmp(req.op, "run", 3) == 0) {
		char cmd[VT_CTL_LINE];
		char cwd[512];
		char tail[48];
		const char *dir;
		PeakProc job;
		int n;

		if (!req.cmd) {
			vt_ctl_reply_err(c, req.id, req.id_n, "missing cmd");
			return;
		}
		n = vt_ctl_unescape(req.cmd, req.cmd_n, cmd, sizeof cmd);
		if (n < 0) {
			vt_ctl_reply_err(c, req.id, req.id_n, "bad json");
			return;
		}
		if (n == 0) {
			vt_ctl_reply_err(c, req.id, req.id_n, "empty cmd");
			return;
		}
		if (ctl_job.pid > 0) {
			vt_ctl_reply_err(c, req.id, req.id_n, "busy");
			return;
		}
		dir = NULL;
		if (sh.pid > 0 && peak_pid_cwd(sh.pid, cwd, sizeof cwd))
			dir = cwd;
		job = peak_job_run(cmd, dir);
		if (job.fd == PEAK_HANDLE_INVALID) {
			vt_ctl_reply_err(c, req.id, req.id_n, "fork failed");
			return;
		}
		ctl_job.pid = job.pid;
		ctl_job.fd = job.fd;
		ctl_job.client = (int)(c - ctl_clients);
		if (req.id && req.id_n > 0 && (size_t)req.id_n < sizeof ctl_job.id) {
			memcpy(ctl_job.id, req.id, (size_t)req.id_n);
			ctl_job.id_n = req.id_n;
		} else {
			ctl_job.id_n = 0;
		}
		ctl_job.out_n = 0;
		ctl_job.trunc = false;
		ctl_job.seq++;
		if (ctl_job.seq == 0)
			ctl_job.seq = 1;
		n = snprintf(tail, sizeof tail, ",\"job\":%u}\n", ctl_job.seq);
		if (n < 0 || (size_t)n >= sizeof tail
				|| vt_ctl_put_prefix(c->fd, req.id, req.id_n, 1) < 0
				|| vt_ctl_put(c->fd, tail, (size_t)n) < 0)
			vt_ctl_client_close(c);
	} else {
		vt_ctl_reply_err(c, req.id, req.id_n, "unknown op");
	}
}

static void
vt_ctl_client_read(VtCtlClient *c)
{
	for (;;) {
		int r;
		u32 i;

		r = peak_fd_read(c->fd, c->buf + c->n, sizeof c->buf - c->n);
		if (r > 0) {
			c->n += (u32)r;
			i = 0;
			while (i < c->n) {
				u32 j;

				for (j = i; j < c->n; j++) {
					if (c->buf[j] == '\n')
						break;
				}
				if (j == c->n)
					break;
				c->buf[j] = 0;
				if (j > i && c->buf[j - 1] == '\r')
					c->buf[j - 1] = 0;
				vt_ctl_handle_line(c, c->buf + i);
				if (c->fd == PEAK_HANDLE_INVALID)
					return;
				i = j + 1;
			}
			if (i) {
				c->n -= i;
				if (c->n)
					memmove(c->buf, c->buf + i, c->n);
			}
			if (c->n == sizeof c->buf) {
				vt_ctl_reply_err(c, NULL, 0, "line too long");
				vt_ctl_client_close(c);
				return;
			}
			continue;
		}
		if (r < 0)
			return;
		vt_ctl_client_close(c);
		return;
	}
}

static void
vt_ctl_accept(void)
{
	PEAK_HANDLE fd;
	int i, slot;

	if (ctl_listen == PEAK_HANDLE_INVALID)
		return;
	for (;;) {
		fd = peak_sock_accept(ctl_listen);
		if (fd == PEAK_HANDLE_INVALID)
			return;
		slot = -1;
		for (i = 0; i < VT_CTL_CLIENTS; i++) {
			if (ctl_clients[i].fd == PEAK_HANDLE_INVALID) {
				slot = i;
				break;
			}
		}
		if (slot < 0) {
			peak_fd_close(fd);
			return;
		}
		ctl_clients[slot].fd = fd;
		ctl_clients[slot].n = 0;
	}
}

static void
vt_ctl_pump(void)
{
	int i;

	vt_ctl_accept();
	for (i = 0; i < VT_CTL_CLIENTS; i++) {
		if (ctl_clients[i].fd != PEAK_HANDLE_INVALID)
			vt_ctl_client_read(&ctl_clients[i]);
	}
	vt_ctl_job_read();
	vt_ctl_job_reap();
}

static bool
vt_ctl_init(void)
{
	char dir[192];
	PEAK_HANDLE fd;
	int i;
	int n;

	ctl_listen = PEAK_HANDLE_INVALID;
	ctl_path[0] = 0;
	ctl_job.pid = 0;
	ctl_job.fd = PEAK_HANDLE_INVALID;
	ctl_job.client = -1;
	ctl_job.id_n = 0;
	ctl_job.out_n = 0;
	ctl_job.trunc = false;
	for (i = 0; i < VT_CTL_CLIENTS; i++) {
		ctl_clients[i].fd = PEAK_HANDLE_INVALID;
		ctl_clients[i].n = 0;
	}

	if (!peak_runtime_dir(dir, sizeof dir, "vt"))
		return false;
	n = snprintf(ctl_path, sizeof ctl_path, "%s/%d.sock", dir, (int)getpid());
	if (n < 0 || (size_t)n >= sizeof ctl_path) {
		ctl_path[0] = 0;
		return false;
	}
	fd = peak_sock_listen(ctl_path);
	if (fd == PEAK_HANDLE_INVALID) {
		VTERROR("ctl bind %s", ctl_path);
		ctl_path[0] = 0;
		return false;
	}
	ctl_listen = fd;
	return true;
}

static void
vt_ctl_destroy(void)
{
	int i;

	vt_ctl_job_kill();
	for (i = 0; i < VT_CTL_CLIENTS; i++)
		vt_ctl_client_close(&ctl_clients[i]);
	if (ctl_listen != PEAK_HANDLE_INVALID) {
		peak_fd_close(ctl_listen);
		ctl_listen = PEAK_HANDLE_INVALID;
	}
	if (ctl_path[0]) {
		remove(ctl_path);
		ctl_path[0] = 0;
	}
}

static void
vt_wait(bool ready)
{
	PEAK_HANDLE fds[2 + VT_CTL_CLIENTS + 1];
	uint32_t n;
	int i;
#ifndef VT_HEADLESS
	PeakWindow *w;
#endif

	n = 0;
	if (sh.fd != PEAK_HANDLE_INVALID)
		fds[n++] = sh.fd;
	if (ctl_listen != PEAK_HANDLE_INVALID)
		fds[n++] = ctl_listen;
	for (i = 0; i < VT_CTL_CLIENTS; i++) {
		if (ctl_clients[i].fd != PEAK_HANDLE_INVALID)
			fds[n++] = ctl_clients[i].fd;
	}
	if (ctl_job.fd != PEAK_HANDLE_INVALID)
		fds[n++] = ctl_job.fd;
#ifndef VT_HEADLESS
	w = vt_headless ? NULL : &win;
	peak_wait(w, fds, n, ready ? 0 : -1);
#else
	peak_wait(NULL, fds, n, ready ? 0 : -1);
#endif
}

#ifndef VT_HEADLESS
static void
vt_present(void)
{
	TermScreen *scr;
	u64 cells;

	scr = term_screen(&term);
	if (!scr || !scr->cell_buffer)
		return;
#ifdef DEBUG
	if (term.screen.cell_buffer != renderer_screen_cells() || term.alt.cell_buffer != renderer_alt_cells())
		VTERROR("term cell_buffer is not the Rend map");
#endif
	cells = renderer_live_address(&term);
	renderer_sync(cells, scr->cols, scr->rows, term.cursor.x, term.cursor.y, !(term.mode & TERM_MODE_HIDE));
}
#endif


static u32
vt_csi_end(const char *data, u32 n, u32 i)
{
	for (; i < n; i++) {
		unsigned char b = (unsigned char)data[i];

		if (b >= 0x40 && b <= 0x7E)
			return i + 1;
	}
	return 0;
}

static u32
vt_str_end(const char *data, u32 n, u32 i)
{
	for (; i < n; i++) {
		unsigned char b = (unsigned char)data[i];

		if (b == 0x07)
			return i + 1;
		if (b == 0x1B && i + 1 < n && data[i + 1] == '\\')
			return i + 2;
	}
	return 0;
}

static u32
vt_atom_len(const char *data, u32 n)
{
	unsigned char ch;

	if (!n)
		return 0;
	ch = (unsigned char)data[0];
	if (ch == 0x1B) {
		if (n == 1)
			return 0;
		ch = (unsigned char)data[1];
		if (ch == '[')
			return vt_csi_end(data, n, 2);
		if (ch == ']' || ch == 'P' || ch == '_' || ch == '^' || ch == 'k')
			return vt_str_end(data, n, 2);
		if (ch >= 0x20 && ch <= 0x2F)
			return n >= 3 ? 3 : 0;
		return 2;
	}
	if (ch < 0x20 || ch == 0x7F)
		return 1;
	if ((ch & 0xE0) == 0xC0)
		return 2 <= n ? 2 : 0;
	if ((ch & 0xF0) == 0xE0)
		return 3 <= n ? 3 : 0;
	if ((ch & 0xF8) == 0xF0)
		return 4 <= n ? 4 : 0;
	return 1;
}

u32
vt_prepass(const char *data, u32 n, u32 term_state, u8 utf8_rem,
		VtRun *out, u32 cap)
{
	__m128i twenty;
	__m128i del;
	__m128i nl;
	__m128i cr;
	u32 off;
	u32 nrun;

	if (!data || !out || !cap || !n)
		return 0;
	off = 0;
	nrun = 0;
	if (term_state || utf8_rem) {
		u32 m;

		if (utf8_rem)
			m = utf8_rem <= n ? utf8_rem : 0;
		else if (term_state & TERM_ESC_CSI)
			m = vt_csi_end(data, n, 0);
		else if (term_state & TERM_ESC_STR)
			m = vt_str_end(data, n, 0);
		else if (term_state & TERM_ESC_START)
			m = 1;
		else
			m = vt_atom_len(data, n);
		if (!m)
			return 0;
		out[nrun].off = 0;
		out[nrun].n = m;
		out[nrun].kind = VT_RUN_PARSE;
		nrun++;
		off = m;
	}
	twenty = _mm_set1_epi8(0x20);
	del = _mm_set1_epi8(0x7F);
	nl = _mm_set1_epi8('\n');
	cr = _mm_set1_epi8('\r');
	while (off < n && nrun < cap) {
		unsigned char ch;
		u32 remaining;
		u32 m;

		remaining = n - off;
		m = 0;
		while (remaining - m >= 16) {
			__m128i batch;
			__m128i bad;
			__m128i ok;
			int mask;

			batch = _mm_loadu_si128((const __m128i *)(data + off + m));
			bad = _mm_or_si128(_mm_cmplt_epi8(batch, twenty), _mm_cmpeq_epi8(batch, del));
			ok = _mm_or_si128(_mm_cmpeq_epi8(batch, nl), _mm_cmpeq_epi8(batch, cr));
			mask = _mm_movemask_epi8(_mm_andnot_si128(ok, bad));
			if (mask) {
				m += (u32)__tzcnt_u32((unsigned int)mask);
				break;
			}
			m += 16;
		}
		if (remaining - m < 16) {
			while (m < remaining) {
				ch = (unsigned char)data[off + m];
				if (!(ch == '\n' || ch == '\r' || (ch >= 0x20 && ch < 0x7F)))
					break;
				m++;
			}
		}
		if (m) {
			out[nrun].off = off;
			out[nrun].n = m;
			out[nrun].kind = VT_RUN_ASCII;
			nrun++;
			off += m;
			continue;
		}
		m = vt_atom_len(data + off, remaining);
		if (!m)
			break;
		out[nrun].off = off;
		out[nrun].n = m;
		out[nrun].kind = VT_RUN_PARSE;
		nrun++;
		off += m;
	}
	return nrun;
}

void
vt_feed_runs(Term *t, const char *base, const VtRun *runs, u32 n)
{
	u32 i;

	if (!t || !base || !runs)
		return;
	for (i = 0; i < n; i++) {
		if (!runs[i].n)
			continue;
		if (runs[i].kind == VT_RUN_ASCII) {
			term_feed_ascii(t, base + runs[i].off, runs[i].n);
		} else {
			term_feed(t, base + runs[i].off, runs[i].n);
			if (atlas.atlas && (unsigned char)base[runs[i].off] >= 0x80)
				vt_glyph_get(vt_utf8_decode(base + runs[i].off, runs[i].n));
		}
	}
}

static void
vt_ring_drain(void)
{
	VtRun runs[VT_RUN_MAX];
	const char *head;
	size_t unread;
	u32 n;
	u32 covered;

	for (;;) {
		unread = vt_ring_unread(&ring);
		if (!unread)
			return;
		head = vt_ring_head(&ring);
		n = vt_prepass(head, (u32)unread, 0, 0, runs, VT_RUN_MAX);
		if (!n)
			return;
		vt_feed_runs(&term, head, runs, n);
		covered = runs[n - 1].off + runs[n - 1].n;
		vt_ring_consume(&ring, covered);
	}
}

static void
vt_ingest(const char *data, size_t len)
{
	if (!data || !len)
		return;
	while (len) {
		size_t room;
		size_t chunk;

		room = vt_ring_room(&ring);
		if (!room) {
			size_t unread;

			vt_ring_drain();
			room = vt_ring_room(&ring);
			if (!room) {
				unread = vt_ring_unread(&ring);
				if (unread) {
					term_feed(&term, vt_ring_head(&ring), unread);
					vt_ring_consume(&ring, unread);
				}
				room = vt_ring_room(&ring);
			}
			if (!room)
				break;
		}
		chunk = len < room ? len : room;
		memcpy(vt_ring_tail(&ring), data, chunk);
		vt_ring_produce(&ring, chunk);
		data += chunk;
		len -= chunk;
		vt_ring_drain();
	}
	if (term.reply_n && sh.fd != PEAK_HANDLE_INVALID) {
		if (peak_fd_write(sh.fd, term.reply, term.reply_n) <= 0)
			VTFATAL("Failed to write to the shell");
		term.reply_n = 0;
	}
}

static codepoint_t
vt_utf8_decode(const char *s, u32 n)
{
	unsigned char c;

	if (!s || !n)
		return UTF_INVALID;
	c = (unsigned char)s[0];
	if (c < 0x80)
		return c;
	if ((c & 0xE0) == 0xC0 && n >= 2)
		return (codepoint_t)(((c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F));
	if ((c & 0xF0) == 0xE0 && n >= 3)
		return (codepoint_t)(((c & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) | ((unsigned char)s[2] & 0x3F));
	if ((c & 0xF8) == 0xF0 && n >= 4)
		return (codepoint_t)(((c & 0x07) << 18) | (((unsigned char)s[1] & 0x3F) << 12) | (((unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F));
	return UTF_INVALID;
}

static int
vt_utf8_encode(codepoint_t c, char out[4])
{
	if (c < 0x80) {
		out[0] = (char)c;
		return 1;
	}
	if (c < 0x800) {
		out[0] = (char)(0xC0 | (c >> 6));
		out[1] = (char)(0x80 | (c & 0x3F));
		return 2;
	}
	if (c < 0x10000) {
		out[0] = (char)(0xE0 | (c >> 12));
		out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
		out[2] = (char)(0x80 | (c & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (c >> 18));
	out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
	out[3] = (char)(0x80 | (c & 0x3F));
	return 4;
}

static void
vt_dump_screen(FILE *out)
{
	TermScreen *s;
	u32 y, x, last_row;

	s = term_screen(&term);
	last_row = 0;
	for (y = 0; y < s->rows; y++) {
		for (x = 0; x < s->cols; x++) {
			if (s->cell_buffer[y * s->cols + x].codepoint)
				last_row = y;
		}
	}

	for (y = 0; y <= last_row; y++) {
		u32 last_col = 0;
		for (x = 0; x < s->cols; x++) {
			if (s->cell_buffer[y * s->cols + x].codepoint)
				last_col = x + 1;
		}
		for (x = 0; x < last_col; x++) {
			codepoint_t c = s->cell_buffer[y * s->cols + x].codepoint;
			char buf[4];
			int n;

			if (c == 0) {
				fputc(' ', out);
				continue;
			}
			n = vt_utf8_encode(c, buf);
			fwrite(buf, 1, (size_t)n, out);
		}
		fputc('\n', out);
	}
}

static int
vt_headless_run(const char *path, const char *shot, u32 cols, u32 rows)
{
	FILE *in;
	FILE *dump;
	FILE *devnull;
	TermScreen *scr;
	char buf[4096];
	size_t n;
	int saved;
	int rc;

	rc = 0;
	vt_headless = true;
	if (!vt_init_term(cols, rows))
		return 1;

	in = path ? fopen(path, "rb") : stdin;
	if (!in) {
		fprintf(stderr, "vt: cannot open %s\n", path);
		return 1;
	}

	saved = dup(STDOUT_FILENO);
#if defined(_WIN32)
	devnull = fopen("NUL", "w");
#else
	devnull = fopen("/dev/null", "w");
#endif
	if (saved >= 0 && devnull) {
		dup2(fileno(devnull), STDOUT_FILENO);
		fclose(devnull);
	}

	while ((n = fread(buf, 1, sizeof buf, in)) > 0)
		vt_ingest(buf, n);
	if (path)
		fclose(in);

	dump = (saved >= 0) ? fdopen(saved, "w") : stdout;
	vt_dump_screen(dump ? dump : stdout);
	if (dump && dump != stdout)
		fclose(dump);

	if (shot) {
		if (!glyph_table_init(font_path, (float)font_size_px)) {
			fprintf(stderr, "vt: cannot load font %s\n", font_path);
			rc = 1;
		} else {
			scr = term_screen(&term);
			if (!renderer_screenshot_ppm(scr, term.cursor.x, term.cursor.y,
					(term.cursor.fg << 8) | term.cursor.attr, term.cursor.bg << 8, shot)) {
				fprintf(stderr, "vt: cannot write %s\n", shot);
				rc = 1;
			}
			glyph_table_destroy();
		}
	}

	vt_destroy();
	return rc;
}

static int
vt_headless_live_run(u32 cols, u32 rows)
{
	vt_headless = true;
	if (!glyph_table_init(font_path, (float)font_size_px)) {
		fprintf(stderr, "vt: cannot load font %s\n", font_path);
		return 1;
	}
	if (!vt_init(cols, rows)) {
		fprintf(stderr, "vt: headless live init failed\n");
		glyph_table_destroy();
		vt_destroy();
		return 1;
	}
	while (running) {
		vt_wait(false);
		vt_ctl_pump();
		vt_sh_read();
	}
	glyph_table_destroy();
	vt_destroy();
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc >= 2 && strcmp(argv[1], "--headless") == 0) {
		const char *path = NULL;
		const char *shot = NULL;
		char *end;
		u32 cols = 80;
		u32 rows = 24;
		int live = 0;
		int i;
		unsigned long v;

		for (i = 2; i < argc; i++) {
			if (strcmp(argv[i], "--screenshot") == 0) {
				if (i + 1 >= argc) {
					fprintf(stderr, "vt: --screenshot needs a path\n");
					return 1;
				}
				shot = argv[++i];
			} else if (strcmp(argv[i], "--live") == 0) {
				live = 1;
			} else if (strcmp(argv[i], "--cols") == 0
					|| strcmp(argv[i], "--rows") == 0) {
				if (i + 1 >= argc) {
					fprintf(stderr, "vt: %s needs a number\n", argv[i]);
					return 1;
				}
				v = strtoul(argv[i + 1], &end, 10);
				if (!argv[i + 1][0] || *end || v < 2 || v > 400) {
					fprintf(stderr, "vt: bad %s\n", argv[i]);
					return 1;
				}
				if (argv[i][2] == 'c')
					cols = (u32)v;
				else
					rows = (u32)v;
				i++;
			} else if (!path) {
				path = argv[i];
			} else {
				fprintf(stderr, "vt: extra arg %s\n", argv[i]);
				return 1;
			}
		}
		if (live)
			return vt_headless_live_run(cols, rows);
		return vt_headless_run(path, shot, cols, rows);
	}

#ifdef VT_HEADLESS
	fprintf(stderr, "vt: headless build; pass --headless\n");
	return 1;
#else
	if (!renderer_init()) {
		VTFATAL("Failed to initalize renderer!");
		renderer_destroy();
		return 1;
	}

    u32 cols, rows;
    bool dirty;

    vt_peak_pump(NULL);
    if (!running) {
        renderer_destroy();
        return 0;
    }

    renderer_get_grid(&renderer, &cols, &rows);
    cols = (cols == 0) ? 80 : cols;
    rows = (rows == 0) ? 24 : rows;

    if (!vt_init(cols, rows)) {
        VTFATAL("Failed to initalize terminal!");
        vt_destroy();
        renderer_destroy();
        return 1;
    }

    dirty = true;
    while (running) {
        vt_wait(dirty);
        vt_peak_pump(&dirty);
        vt_ctl_pump();
        if (vt_sh_read())
            dirty = true;
        if (!running)
            break;
        if (dirty) {
            vt_present();
            dirty = false;
        }
    }

    vt_destroy();
    renderer_destroy();
    VTINFO("Quit successfully!");
    return 0;
#endif
}
