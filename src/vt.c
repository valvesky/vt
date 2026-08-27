#define _GNU_SOURCE

#ifdef DEBUG
#define P_LOG_DEBUG_ENABLED 1
#define P_LOG_TRACE_ENABLED 1
#define TERM_DEBUG
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
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#endif
#include <assert.h>
#include <emmintrin.h>
#include <immintrin.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vt.h"
#include "config.h"
#include "vt_debug.h"
#include "vt_circ_buf.c"
#include "vt_lru.c"
#include "vt_renderer.c"

#ifndef VT_HEADLESS
#define VT_PEAK_WIN (&win)
#else
#define VT_PEAK_WIN NULL
#endif

static bool vt_init_term(u32 cols, u32 rows);
static bool vt_init(u32 cols, u32 rows);
static void vt_destroy(void);
static int vt_utf8_encode(codepoint_t c, char out[4]);
static int vt_dump_walk(int (*put)(void *, const char *, size_t), void *ctx);
static void vt_dump_screen(FILE *out);
static void vt_dump_runs(FILE *out);
static void vt_shell_gone(void);
static size_t vt_sh_write(const char *const src, size_t len);
static int vt_feed_stdin_to_ringbuffer(void);
static void vt_feed_ringbuffer_to_runs(void);
static void vt_feed_ring_drain(void);
static int vt_ingest(void);
static u32 vt_utf8_len(const char *data, u32 n);
#ifndef VT_HEADLESS
static void vt_events(bool *dirty);
static void vt_present(void);
#endif
static void vt_wait(int timeout_ms);
static size_t vt_base64_decode(const char *s, size_t n, char *dst, size_t cap);
static int vt_osc52(const char *p, u32 n);
static void vt_clip_paste(PeakClip which);
static void vt_clip_take_write(void);
static size_t vt_sel_utf8(char *dst, size_t cap);
static void vt_sel_to_primary(void);
#ifndef VT_HEADLESS
static void vt_cell_at(float px, float py, u32 *x, u32 *y);
static void vt_mouse_report(int btn, u32 x, u32 y, int release, PeakKeyMod mod);
#endif

static Term term;
static VtRing ring;
static VtRun runs[VT_RUN_MAX];
static u32 nruns;
static PeakProc sh = { PEAK_HANDLE_INVALID, 0 };
static bool running = true;
static bool redraw = true;
#define VT_CLIP_MAX (1024u * 1024u)
static int vt_sel_on;
static u32 vt_sel_ax, vt_sel_ay, vt_sel_bx, vt_sel_by;
static int vt_sel_drag;
static int vt_mouse_btn = -1;
static char vt_clip_buf[VT_CLIP_MAX + 1];
static void (*const vt_run_feed[])(Term *, const char *, size_t) = {
	term_feed_printable,
	term_feed_escape,
	term_feed_utf8,
};
static const char *const vt_run_name[] = {
	"PRINTABLE",
	"ESCAPE",
	"UTF8",
};
static const char vt_utf8_fffd[3] = { (char)0xEF, (char)0xBF, (char)0xBD };


#include "vt_ctl.c"

bool
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
		VTASSERT(0, "vt_ring_init");
		return false;
	}
	if (!term_init(&term, cols, rows, &colors)) {
		VTASSERT(0, "term_init");
		vt_ring_destroy(&ring);
		return false;
	}
#ifndef VT_HEADLESS
	if (!renderer_instance_make(&renderer.instance, cols, rows)) {
		VTASSERT(0, "renderer_instance_make");
		term_destroy(&term);
		vt_ring_destroy(&ring);
		return false;
	}
#endif
	return true;
}

bool
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
	vt_ctl_init();
	proc = peak_pty_spawn("bash", argv, cols, rows,
			cols * atlas.cell_width, rows * atlas.cell_height);
	if (proc.fd == PEAK_HANDLE_INVALID) {
		VTFATAL("Could not open tty.");
		VTASSERT(0, "Could not open tty.");
		return false;
	}
	sh = proc;
	return true;
}

void
vt_destroy(void)
{
	vt_ctl_destroy();
	VTINFO("[Shell %-d] Exited successfully", sh.pid);
	peak_pty_close(&sh);
	term_destroy(&term);
	vt_ring_destroy(&ring);
}

void
vt_shell_gone(void)
{
#ifndef _WIN32
	vt_reap_children();
#else
	if (sh.pid > 0)
		peak_pty_reap(&sh);
#endif
	running = false;
}

int
vt_feed_stdin_to_ringbuffer(void)
{
	/* NOTE(vasco): Read into unused ring only. Never drop unread.
	 * Mirror map makes room bytes linear from the tail.
	 * 1 = ring full (parse this much, then present). 0 = EAGAIN/EOF.
	 */
	size_t nmax;
	int r;

	VTASSERT(sh.fd != PEAK_HANDLE_INVALID && ring.base && ring.size);

	for (;;) {
		nmax = vt_ring_room(&ring);
		if (!nmax)
			return 1;
		r = peak_fd_read(sh.fd, vt_ring_tail(&ring), nmax);
		if (r > 0) {
			vt_ring_produce(&ring, (size_t)r);
			continue;
		}
		if (r == 0)
			vt_shell_gone();
		return 0;
	}
}

void
vt_feed_ringbuffer_to_runs(void)
{
	/* NOTE(vasco):
	 *
	 * Input can contain escape sequences and/or UTF-8.
	 *
	 * Term accepts 4 types of input:
	 * 1. printable (> SPC & < DEL)
	 * 2. escape (includes printable) (< DEL)
	 * 3. utf8 (> SPC)
	 * 4. ALL
	 *
	 * We can avoid type 4, because all text fits
	 * into type 2. and 3. necessarily.
	 *
	 * The optimal preparser maximizes use of type 1.
	 *
	 * Let's say that we recieve:
	 * <90 bytes of ANSI SEQUENCE>
	 * <1 kilobyte plain text>
	 * <8 bytes utf-8>
	 * <1 kilobyte plain text>
	 *
	 * We want that reflected into exactly that:
	 * RUN 1: OFFSET=HEAD LEN=90 TYPE=ESCAPE
	 * RUN 2: OFFSET=HEAD+90 LEN=1024 TYPE=PRINTABLE
	 * RUN 3: OFFSET=HEAD+90+1024 LEN=8 TYPE=UTF8
	 * RUN 4: OFFSET=HEAD+90+1024+8 LEN=1024 TYPE=PRINTABLE
	 *
	 * This should give us optimal parsing speeds.
	 */

	__m128i space;
	__m128i del;
	const char *head;
	u32 n;
	u32 off;

	nruns = 0;
	VTASSERT(ring.base && ring.size);
	n = (u32)(ring.w - ring.r);
	head = ring.base + (ring.r % ring.size);
	off = 0;
	space = _mm_set1_epi8(0x20);
	del = _mm_set1_epi8(0x7F);

	while (off < n && nruns < VT_RUN_MAX) {
		unsigned char ch;
		u32 remaining;
		u32 m;
		int type;

		remaining = n - off;
		ch = (unsigned char)head[off];
		m = 0;
		if (ch >= 0x20 && ch < 0x7F) {
			type = VT_RUN_PRINTABLE;
			while (remaining - m >= 16) {
				__m128i batch;
				__m128i bad;
				int mask;

				batch = _mm_loadu_si128((const __m128i *)(head + off + m));
				bad = _mm_or_si128(_mm_cmplt_epi8(batch, space), _mm_cmpeq_epi8(batch, del));
				mask = _mm_movemask_epi8(bad);
				if (mask) {
					m += (u32)__tzcnt_u32((unsigned int)mask);
					break;
				}
				m += 16;
			}
			if (remaining - m < 16) {
				while (m < remaining) {
					ch = (unsigned char)head[off + m];
					if (ch < 0x20 || ch >= 0x7F)
						break;
					m++;
				}
			}
		} else if (ch >= 0x80) {
			type = VT_RUN_UTF8;
			while (m < remaining) {
				u32 k;

				ch = (unsigned char)head[off + m];
				if (ch < 0x80)
					break;
				k = vt_utf8_len(head + off + m, remaining - m);
				if (!k)
					break;
				m += k;
			}
		} else {
			type = VT_RUN_ESCAPE;
			while (m < remaining) {
				u32 k;
				u32 left;

				ch = (unsigned char)head[off + m];
				if ((ch >= 0x20 && ch < 0x7F) || ch >= 0x80)
					break;
				left = remaining - m;
				k = 1;
				if (ch == 0x1B) {
					unsigned char nch;
					u32 i;

					if (left == 1) {
						k = 0;
					} else {
						nch = (unsigned char)head[off + m + 1];
						if (nch == '[') {
							k = 0;
							for (i = 2; i < left; i++) {
								unsigned char b = (unsigned char)head[off + m + i];

								if (b >= 0x40 && b <= 0x7E) {
									k = i + 1;
									break;
								}
							}
						} else if (nch == ']' || nch == 'P' || nch == '_' || nch == '^' || nch == 'k') {
							k = 0;
							for (i = 2; i < left; i++) {
								unsigned char b = (unsigned char)head[off + m + i];

								if (b == 0x07) {
									k = i + 1;
									break;
								}
								if (b == 0x1B && i + 1 < left && head[off + m + i + 1] == '\\') {
									k = i + 2;
									break;
								}
							}
						} else if (nch >= 0x20 && nch <= 0x2F) {
							k = left >= 3 ? 3 : 0;
						} else {
							k = 2;
						}
					}
				}
				if (!k)
					break;
				m += k;
			}
		}
		if (!m)
			break;
		runs[nruns].off = off;
		runs[nruns].n = m;
		runs[nruns].type = type;
#ifdef DEBUG
		{
			u32 j;
			for (j = 0; j < m; j++) {
				unsigned char b = (unsigned char)head[off + j];
				if (type == VT_RUN_PRINTABLE)
					VTASSERT(b >= 0x20 && b < 0x7F);
				else if (type == VT_RUN_UTF8)
					VTASSERT(b >= 0x80);
				else if (!j)
					VTASSERT(b < 0x20 || b == 0x7F);
			}
		}
#endif
		nruns++;
		off += m;
	}
}

void
vt_feed_ring_drain(void)
{
#ifdef DEBUG
	u64 t0;
	u64 dt;
	int fed;

	fed = 0;
	t0 = peak_get_time();
#endif
	for (;;) {
		const char *head;
		u32 i;
		u32 covered;

		vt_feed_ringbuffer_to_runs();
		if (!nruns)
			break;
#ifdef DEBUG
		fed = 1;
#endif
		VTASSERT(ring.base && ring.size);
		head = ring.base + (ring.r % ring.size);
		for (i = 0; i < nruns; i++) {
			const char *p;
			u32 n;
			int type;

			if (!runs[i].n)
				continue;
			p = head + runs[i].off;
			n = runs[i].n;
			type = runs[i].type;
			VTASSERT((u32)type < LEN(vt_run_feed));
			if (type == VT_RUN_UTF8) {
				u32 ui;
				u32 span;

				ui = 0;
				span = 0;
				while (ui < n) {
					u32 k;
					u32 need;
					unsigned char lead;

					k = vt_utf8_len(p + ui, n - ui);
					lead = (unsigned char)p[ui];
					if ((lead & 0xE0) == 0xC0)
						need = 2;
					else if ((lead & 0xF0) == 0xE0)
						need = 3;
					else if ((lead & 0xF8) == 0xF0)
						need = 4;
					else
						need = 1;
					if (!k || k != need) {
						if (span) {
							term_feed_utf8(&term, p + (ui - span), span);
							span = 0;
						}
						term_feed_utf8(&term, vt_utf8_fffd, 3);
						if (atlas.atlas)
							vt_glyph_get(UTF_INVALID);
						ui++;
						continue;
					}
					if (atlas.atlas) {
						codepoint_t cp;
						unsigned char c;

						c = (unsigned char)p[ui];
						if (!k)
							cp = UTF_INVALID;
						else if (c < 0x80)
							cp = c;
						else if ((c & 0xE0) == 0xC0 && k >= 2)
							cp = (codepoint_t)(((c & 0x1F) << 6) | ((unsigned char)p[ui + 1] & 0x3F));
						else if ((c & 0xF0) == 0xE0 && k >= 3)
							cp = (codepoint_t)(((c & 0x0F) << 12) | (((unsigned char)p[ui + 1] & 0x3F) << 6) | ((unsigned char)p[ui + 2] & 0x3F));
						else if ((c & 0xF8) == 0xF0 && k >= 4)
							cp = (codepoint_t)(((c & 0x07) << 18) | (((unsigned char)p[ui + 1] & 0x3F) << 12) | (((unsigned char)p[ui + 2] & 0x3F) << 6) | ((unsigned char)p[ui + 3] & 0x3F));
						else
							cp = UTF_INVALID;
						vt_glyph_get(cp);
					}
					span += k;
					ui += k;
				}
				if (span)
					term_feed_utf8(&term, p + (n - span), span);
			} else if (!(type == VT_RUN_ESCAPE && vt_osc52(p, n))) {
				vt_run_feed[type](&term, p, n);
			}
		}
		if (nruns) {
			covered = runs[nruns - 1].off + runs[nruns - 1].n;
			vt_ring_consume(&ring, covered);
			nruns = 0;
			redraw = true;
		}
		if (term.reply_n) {
			VTASSERT(sh.fd != PEAK_HANDLE_INVALID);
			if (peak_fd_write(sh.fd, term.reply, term.reply_n) <= 0)
				VTFATAL("Failed to write to the shell");
			term.reply_n = 0;
		}
	}
#ifdef DEBUG
	dt = peak_get_time() - t0;
	if (fed) {
		VTDEBUG("parse %llu ns", (unsigned long long)dt);
		vt_stage_add(VT_STAGE_PARSE, dt);
	}
#endif
}

int
vt_ingest(void)
{
	int full;

	full = vt_feed_stdin_to_ringbuffer();
	if (ring.w != ring.r)
		vt_feed_ring_drain();
	return full;
}

size_t
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

size_t
vt_base64_decode(const char *s, size_t n, char *dst, size_t cap)
{
	size_t i;
	size_t o;
	unsigned acc;
	int bits;

	o = 0;
	acc = 0;
	bits = 0;
	for (i = 0; i < n; i++) {
		unsigned char c;
		int v;

		c = (unsigned char)s[i];
		if (c == '=' || c == '\n' || c == '\r')
			break;
		if (c >= 'A' && c <= 'Z')
			v = c - 'A';
		else if (c >= 'a' && c <= 'z')
			v = c - 'a' + 26;
		else if (c >= '0' && c <= '9')
			v = c - '0' + 52;
		else if (c == '+')
			v = 62;
		else if (c == '/')
			v = 63;
		else
			continue;
		acc = (acc << 6) | (unsigned)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			if (o < cap)
				dst[o++] = (char)((acc >> bits) & 0xff);
		}
	}
	return o;
}

int
vt_osc52(const char *p, u32 n)
{
	u32 i;
	u32 pd;
	u32 pdn;
	size_t dn;

	if (n < 6 || (unsigned char)p[0] != 0x1b || p[1] != ']')
		return 0;
	i = 2;
	if (i + 3 > n || p[i] != '5' || p[i + 1] != '2' || p[i + 2] != ';')
		return 0;
	i += 3;
	while (i < n && p[i] != ';')
		i++;
	if (i >= n || p[i] != ';')
		return 0;
	i++;
	pd = i;
	while (i < n && (unsigned char)p[i] != 0x07
			&& !((unsigned char)p[i] == 0x1b && i + 1 < n && p[i + 1] == '\\'))
		i++;
	if (i >= n)
		return 0;
	pdn = i - pd;
	if (pdn == 0 || (pdn == 1 && p[pd] == '?'))
		return 1;
	dn = vt_base64_decode(p + pd, pdn, vt_clip_buf, VT_CLIP_MAX);
	peak_clip_set(VT_PEAK_WIN, PEAK_CLIP_CLIPBOARD, vt_clip_buf, dn);
	return 1;
}

void
vt_clip_take_write(void)
{
	size_t n;
	size_t i;
	size_t o;

	n = 0;
	if (!peak_clip_take(NULL, vt_clip_buf, VT_CLIP_MAX, &n) || !n)
		return;
	o = 0;
	for (i = 0; i < n; i++) {
		char c;

		c = vt_clip_buf[i];
		if (c == '\n' && (o == 0 || vt_clip_buf[o - 1] != '\r'))
			c = '\r';
		else if (c == '\n')
			continue;
		vt_clip_buf[o++] = c;
	}
	if (o)
		vt_sh_write(vt_clip_buf, o);
}

void
vt_clip_paste(PeakClip which)
{
#ifndef VT_HEADLESS
	peak_clip_request(&win, which);
#else
	if (peak_clip_request(NULL, which))
		vt_clip_take_write();
#endif
}

size_t
vt_sel_utf8(char *dst, size_t cap)
{
	TermScreen *s;
	u32 a;
	u32 b;
	u32 i;
	u32 cols;
	size_t o;

	if (!vt_sel_on || !dst || !cap)
		return 0;
	s = term_screen(&term);
	if (!s || !s->cell_buffer || !s->cols)
		return 0;
	cols = s->cols;
	a = vt_sel_ay * cols + vt_sel_ax;
	b = vt_sel_by * cols + vt_sel_bx;
	if (a > b) {
		u32 t;

		t = a;
		a = b;
		b = t;
	}
	if (b >= cols * s->rows)
		b = cols * s->rows - 1;
	o = 0;
	for (i = a; i <= b; i++) {
		codepoint_t cp;
		char enc[4];
		int k;

		if (i > a && i % cols == 0) {
			if (o < cap)
				dst[o++] = '\n';
		}
		cp = s->cell_buffer[i].codepoint;
		if (!cp)
			cp = (codepoint_t)' ';
		k = vt_utf8_encode(cp, enc);
		if (o + (size_t)k > cap)
			break;
		memcpy(dst + o, enc, (size_t)k);
		o += (size_t)k;
	}
	while (o && dst[o - 1] == ' ')
		o--;
	return o;
}

void
vt_sel_to_primary(void)
{
	size_t n;

	n = vt_sel_utf8(vt_clip_buf, VT_CLIP_MAX);
	if (!n)
		return;
	peak_clip_set(VT_PEAK_WIN, PEAK_CLIP_PRIMARY, vt_clip_buf, n);
}

#ifndef VT_HEADLESS
void
vt_cell_at(float px, float py, u32 *x, u32 *y)
{
	u32 cols;
	u32 rows;
	u32 cw;
	u32 ch;
	int cx;
	int cy;

	renderer_get_grid(&renderer, &cols, &rows);
	cw = atlas.cell_width;
	ch = atlas.cell_height;
	cx = cw ? (int)(px / (float)cw) : 0;
	cy = ch ? (int)(py / (float)ch) : 0;
	if (cx < 0)
		cx = 0;
	if (cy < 0)
		cy = 0;
	if (cols && cx >= (int)cols)
		cx = (int)cols - 1;
	if (rows && cy >= (int)rows)
		cy = (int)rows - 1;
	*x = (u32)cx;
	*y = (u32)cy;
}

void
vt_mouse_report(int btn, u32 x, u32 y, int release, PeakKeyMod mod)
{
	char buf[64];
	int n;
	int b;

	b = btn;
	if (mod & PEAK_KEYMOD_SHIFT)
		b += 4;
	if (mod & PEAK_KEYMOD_ALT)
		b += 8;
	if (mod & PEAK_KEYMOD_CTRL)
		b += 16;
	x++;
	y++;
	if (term.mode & TERM_MODE_MOUSESGR) {
		n = snprintf(buf, sizeof buf, "\033[<%d;%u;%u%c", b, x, y, release ? 'm' : 'M');
		if (n > 0)
			vt_sh_write(buf, (size_t)n);
		return;
	}
	if (release)
		b = 3;
	if (x > 223)
		x = 223;
	if (y > 223)
		y = 223;
	buf[0] = '\033';
	buf[1] = '[';
	buf[2] = 'M';
	buf[3] = (char)(32 + b);
	buf[4] = (char)(32 + x);
	buf[5] = (char)(32 + y);
	vt_sh_write(buf, 6);
}
#endif

#ifndef VT_HEADLESS
void
vt_events(bool *dirty)
{
	PeakEvent event;

	while (peak_window_epoll(&win, &event)) {
		switch (event.type) {
		case PEAK_EVENT_WINDOW_CLOSE:
			running = false;
			break;
		case PEAK_EVENT_WINDOW_RESIZE:
			renderer.current_width = event.resize.width;
			renderer.current_height = event.resize.height;
			VTDEBUG("Resize %ux%u", event.resize.width, event.resize.height);
			if (sh.fd != PEAK_HANDLE_INVALID) {
				u32 cols, rows;
				TermScreen *s;

				renderer_get_grid(&renderer, &cols, &rows);
				VTASSERT(cols && rows);
				s = term_screen(&term);
				VTASSERT(s);
				if (!(s->cols == cols && s->rows == rows)) {
					RendBuffer inst;
					int resized = 0;

					if (inst_prev.handle)
						renderer_instance_release_prev();
					if (renderer_instance_make(&inst, cols, rows)) {
						inst_prev = renderer.instance;
						renderer.instance = inst;
						resized = 1;
					}
					if (resized) {
						term_resize(&term, cols, rows);
						peak_pty_resize(&sh, cols, rows, cols * atlas.cell_width, rows * atlas.cell_height);
					}
				}
			}
			*dirty = true;
			break;
		case PEAK_EVENT_KEY_DOWN: {
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

			key = event.key.key;
			mod = event.key.mod;
			code = event.key.code;

			if ((mod & PEAK_KEYMOD_CTRL) && (mod & PEAK_KEYMOD_SHIFT)) {
				if (key == PEAK_KEY_C) {
					size_t n;

					n = vt_sel_utf8(vt_clip_buf, VT_CLIP_MAX);
					if (n)
						peak_clip_set(&win, PEAK_CLIP_CLIPBOARD, vt_clip_buf, n);
					break;
				}
				if (key == PEAK_KEY_V) {
					vt_clip_paste(PEAK_CLIP_CLIPBOARD);
					break;
				}
			}
			if (key == PEAK_KEY_INSERT && (mod & PEAK_KEYMOD_SHIFT)) {
				vt_clip_paste(PEAK_CLIP_CLIPBOARD);
				break;
			}
			if (key == PEAK_KEY_INSERT) {
				vt_sh_write("\033[2~", 4);
				break;
			}
			if (key == PEAK_KEY_TAB && (mod & PEAK_KEYMOD_SHIFT)) {
				vt_sh_write("\033[Z", 3);
				break;
			}
			if ((u32)key < LEN(seq) && seq[key].n) {
				vt_sh_write(seq[key].s, seq[key].n);
				break;
			}

			if ((mod & PEAK_KEYMOD_CTRL) && !(mod & PEAK_KEYMOD_SHIFT)) {
				if (key >= PEAK_KEY_A && key <= PEAK_KEY_Z) {
					ch = (char)(1 + (key - PEAK_KEY_A));
					vt_sh_write(&ch, 1);
					break;
				}
				if (code >= 1 && code < 32) {
					ch = (char)code;
					vt_sh_write(&ch, 1);
					break;
				}
			}

			if (code >= 32 && code < 127) {
				ch = (char)code;
				vt_sh_write(&ch, 1);
				break;
			}

			if (key >= PEAK_KEY_0 && key <= PEAK_KEY_9) {
				ch = (char)('0' + (key - PEAK_KEY_0));
				vt_sh_write(&ch, 1);
				break;
			}
			if (key >= PEAK_KEY_A && key <= PEAK_KEY_Z) {
				ch = (char)('a' + (key - PEAK_KEY_A));
				if (mod & (PEAK_KEYMOD_SHIFT | PEAK_KEYMOD_CAPS))
					ch = (char)(ch - 32);
				vt_sh_write(&ch, 1);
			}
			break;
		}
		case PEAK_EVENT_CLIP:
			vt_clip_take_write();
			break;
		case PEAK_EVENT_POINTER: {
			u32 cx;
			u32 cy;
			int btn;
			PeakKeyMod pmod;

			vt_cell_at(event.pointer.x, event.pointer.y, &cx, &cy);
			pmod = event.pointer.mod;
			if (event.pointer.type == PEAK_POINTER_WHEEL_UP
					|| event.pointer.type == PEAK_POINTER_WHEEL_DOWN) {
				if (event.pointer.state == PEAK_POINTER_PRESSED) {
					if (term.mode & TERM_MODE_MOUSE)
						vt_mouse_report(event.pointer.type == PEAK_POINTER_WHEEL_UP ? 64 : 65,
							cx, cy, 0, pmod);
					else if (term.mode & TERM_MODE_ALTSCREEN)
						vt_sh_write(event.pointer.type == PEAK_POINTER_WHEEL_UP
								? "\033[A" : "\033[B", 3);
				}
				break;
			}
			btn = event.pointer.type == PEAK_POINTER_RIGHT ? 2 :
				event.pointer.type == PEAK_POINTER_MIDDLE ? 1 : 0;
			if ((term.mode & TERM_MODE_MOUSE) && !(pmod & PEAK_KEYMOD_SHIFT)) {
				if (event.pointer.state == PEAK_POINTER_PRESSED) {
					vt_mouse_btn = btn;
					vt_mouse_report(btn, cx, cy, 0, pmod);
				} else if (event.pointer.state == PEAK_POINTER_RELEASED) {
					vt_mouse_report(vt_mouse_btn >= 0 ? vt_mouse_btn : btn, cx, cy, 1, pmod);
					vt_mouse_btn = -1;
				} else if (event.pointer.state == PEAK_POINTER_MOVED) {
					int motion;

					motion = 0;
					if (term.mode & TERM_MODE_MOUSEMANY)
						motion = 1;
					else if ((term.mode & TERM_MODE_MOUSEMOT) && vt_mouse_btn >= 0)
						motion = 1;
					if (motion)
						vt_mouse_report((vt_mouse_btn >= 0 ? vt_mouse_btn : 3) + 32,
							cx, cy, 0, pmod);
				}
				break;
			}
			if (event.pointer.type == PEAK_POINTER_MIDDLE
					&& event.pointer.state == PEAK_POINTER_PRESSED) {
				vt_clip_paste(PEAK_CLIP_PRIMARY);
				break;
			}
			if (event.pointer.type == PEAK_POINTER_LEFT) {
				if (event.pointer.state == PEAK_POINTER_PRESSED) {
					vt_sel_on = 1;
					vt_sel_drag = 1;
					vt_sel_ax = vt_sel_bx = cx;
					vt_sel_ay = vt_sel_by = cy;
					*dirty = true;
				} else if (event.pointer.state == PEAK_POINTER_MOVED && vt_sel_drag) {
					vt_sel_bx = cx;
					vt_sel_by = cy;
					*dirty = true;
				} else if (event.pointer.state == PEAK_POINTER_RELEASED && vt_sel_drag) {
					vt_sel_drag = 0;
					vt_sel_bx = cx;
					vt_sel_by = cy;
					vt_sel_to_primary();
					*dirty = true;
				}
			}
			break;
		}
		default:
			break;
		}
	}
}
#endif

void
vt_wait(int timeout_ms)
{
	PEAK_HANDLE fds[2 + VT_CTL_CLIENTS + 2];
	u32 n;

	n = 0;
	/* timeout > 0 is the hz hold: do not wake on PTY or we parse
	 * the rest of the input before the due present.
	 */
	if (timeout_ms <= 0 && sh.fd != PEAK_HANDLE_INVALID)
		fds[n++] = sh.fd;
	n += vt_ctl_fds(fds + n);
	peak_wait(VT_PEAK_WIN, fds, n, timeout_ms);
}

#ifndef VT_HEADLESS
void
vt_present(void)
{
	TermScreen *scr;
#ifdef DEBUG
	u64 t0;
	u64 dt;

	t0 = peak_get_time();
#endif
	scr = term_screen(&term);
	VTASSERT(scr && scr->cell_buffer);
	{
		u32 sel0;
		u32 sel1;
		u32 a;
		u32 b;

		a = vt_sel_ay * scr->cols + vt_sel_ax;
		b = vt_sel_by * scr->cols + vt_sel_bx;
		sel0 = a < b ? a : b;
		sel1 = a < b ? b : a;
		renderer_sync(scr, term.cursor.x, term.cursor.y, !(term.mode & TERM_MODE_HIDE),
			(term.cursor.fg << 8) | term.cursor.attr, term.cursor.bg << 8,
			vt_sel_on, sel0, sel1);
	}
#ifdef DEBUG
	dt = peak_get_time() - t0;
	VTDEBUG("present %llu ns", (unsigned long long)dt);
	vt_stage_add(VT_STAGE_PRESENT, dt);
#endif
}
#endif


u32
vt_utf8_len(const char *data, u32 n)
{
	unsigned char ch;
	u32 need;
	u32 i;

	if (!n)
		return 0;
	ch = (unsigned char)data[0];
	if (ch < 0x80)
		return 0;
	if ((ch & 0xE0) == 0xC0)
		need = 2;
	else if ((ch & 0xF0) == 0xE0)
		need = 3;
	else if ((ch & 0xF8) == 0xF0)
		need = 4;
	else
		need = 1;
	if (need == 1)
		return 1;
	if (n < need) {
		for (i = 1; i < n; i++) {
			if (((unsigned char)data[i] & 0xC0) != 0x80)
				return 1;
		}
		return 0;
	}
	for (i = 1; i < need; i++) {
		if (((unsigned char)data[i] & 0xC0) != 0x80)
			return 1;
	}
	return need;
}

void
vt_dump_runs(FILE *out)
{
	u32 i;

	for (i = 0; i < nruns; i++) {
		VTASSERT((u32)runs[i].type < LEN(vt_run_name));
		fprintf(out, "%s %u\n", vt_run_name[runs[i].type], runs[i].n);
	}
}

int
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

int
vt_dump_walk(int (*put)(void *, const char *, size_t), void *ctx)
{
	TermScreen *s;
	u32 y, x, last_row;

	s = term_screen(&term);
	if (!s || !s->cell_buffer)
		return -1;
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

			if (!c) {
				if (put(ctx, " ", 1) < 0)
					return -1;
				continue;
			}
			n = vt_utf8_encode(c, buf);
			if (put(ctx, buf, (size_t)n) < 0)
				return -1;
		}
		if (put(ctx, "\n", 1) < 0)
			return -1;
	}
	return 0;
}

static int
vt_dump_file_put(void *ctx, const char *p, size_t n)
{
	return fwrite(p, 1, n, (FILE *)ctx) == n ? 0 : -1;
}

void
vt_dump_screen(FILE *out)
{
	(void)vt_dump_walk(vt_dump_file_put, out);
}

