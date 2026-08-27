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
	int code;
	u32 seq;
	u32 out_n;
	bool trunc;
	bool dead;
	char id[96];
	char out[VT_CTL_JOB_OUT];
} VtCtlJob;

static bool vt_init_term(u32 cols, u32 rows);
static bool vt_init(u32 cols, u32 rows);
static void vt_destroy(void);
static int vt_utf8_encode(codepoint_t c, char out[4]);
static void vt_dump_screen(FILE *out);
static void vt_shell_gone(void);
static size_t vt_sh_write(const char *const src, size_t len);
static void vt_feed_stdin_to_ringbuffer(void);
static void vt_feed_ringbuffer_to_runs(void);
static void vt_ingest(void);
static u32 vt_utf8_len(const char *data, u32 n);
#ifndef VT_HEADLESS
static void vt_events(bool *dirty);
#endif
static void vt_wait(bool ready);
static int vt_ctl_skip_ws(const char *s, int i);
static int vt_ctl_parse_string(const char *s, int i, const char **out, int *n);
static int vt_ctl_unescape(const char *s, int n, char *dst, size_t cap);
static int vt_ctl_put(PEAK_HANDLE fd, const char *p, size_t n);
static int vt_ctl_put_escaped(PEAK_HANDLE fd, const char *p, size_t n);
static int vt_ctl_put_prefix(PEAK_HANDLE fd, const char *id, int id_n, int ok);
static void vt_ctl_client_close(VtCtlClient *c);
static void vt_ctl_reply_err(VtCtlClient *c, const char *id, int id_n, const char *err);
static void vt_ctl_job_finish(void);
#ifdef _WIN32
static void vt_ctl_job_reap(void);
#else
static void vt_sigchld_handler(int sig);
static int vt_status_code(int status);
static void vt_reap_children(void);
#endif
static void vt_ctl_pump(void);

static Term term;
static VtRing ring;
static VtRun runs[VT_RUN_MAX];
static u32 nruns;
static PeakProc sh = { PEAK_HANDLE_INVALID, 0 };
static PEAK_HANDLE ctl_listen = PEAK_HANDLE_INVALID;
static char ctl_path[256];
static VtCtlClient ctl_clients[VT_CTL_CLIENTS];
static VtCtlJob ctl_job;
#ifndef _WIN32
static PEAK_HANDLE vt_chld_r = PEAK_HANDLE_INVALID;
static int vt_chld_w = -1;
#endif
static bool running = true;
static bool redraw = true;
static bool vt_headless;
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

#ifndef _WIN32
static void
vt_sigchld_handler(int sig)
{
	int saved;
	char x;

	(void)sig;
	saved = errno;
	x = 0;
	if (vt_chld_w >= 0)
		(void)write(vt_chld_w, &x, 1);
	errno = saved;
}

static int
vt_status_code(int status)
{
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return 1;
}
#endif

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
		VTASSERT(0, "vt_ring_init");
		return false;
	}
	if (!term_init(&term, cols, rows, &colors)) {
		VTASSERT(0, "term_init");
		vt_ring_destroy(&ring);
		return false;
	}
#ifndef VT_HEADLESS
	if (!vt_headless) {
		if (!renderer_instance_make(&renderer.instance, cols, rows)) {
			VTASSERT(0, "renderer_instance_make");
			term_destroy(&term);
			vt_ring_destroy(&ring);
			return false;
		}
	}
#endif
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
#ifndef _WIN32
	{
		int p[2];
		struct sigaction sa;

		vt_chld_r = PEAK_HANDLE_INVALID;
		vt_chld_w = -1;
		if (pipe2(p, O_CLOEXEC | O_NONBLOCK) == 0) {
			memset(&sa, 0, sizeof sa);
			sa.sa_handler = vt_sigchld_handler;
			sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
			sigemptyset(&sa.sa_mask);
			if (sigaction(SIGCHLD, &sa, NULL) == 0) {
				vt_chld_r = p[0];
				vt_chld_w = p[1];
			} else {
				close(p[0]);
				close(p[1]);
			}
		}
	}
#endif
	proc = peak_pty_spawn("bash", argv, cols, rows,
			cols * atlas.cell_width, rows * atlas.cell_height);
	if (proc.fd == PEAK_HANDLE_INVALID) {
		VTFATAL("Could not open tty.");
		VTASSERT(0, "Could not open tty.");
		return false;
	}
	sh = proc;
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
		ctl_job.code = 0;
		ctl_job.out_n = 0;
		ctl_job.trunc = false;
		ctl_job.dead = false;
		for (i = 0; i < VT_CTL_CLIENTS; i++) {
			ctl_clients[i].fd = PEAK_HANDLE_INVALID;
			ctl_clients[i].n = 0;
		}
		if (!peak_runtime_dir(dir, sizeof dir, "vt")) {
			VTERROR("ctl socket disabled");
		} else {
			n = snprintf(ctl_path, sizeof ctl_path, "%s/%d.sock", dir, (int)getpid());
			if (n < 0 || (size_t)n >= sizeof ctl_path) {
				ctl_path[0] = 0;
				VTERROR("ctl socket disabled");
			} else {
				fd = peak_sock_listen(ctl_path);
				if (fd == PEAK_HANDLE_INVALID) {
					VTERROR("ctl bind %s", ctl_path);
					ctl_path[0] = 0;
					VTERROR("ctl socket disabled");
				} else {
					ctl_listen = fd;
				}
			}
		}
	}
	return true;
}

static void
vt_destroy(void)
{
	int i;
	PeakProc job;
#ifndef _WIN32
	struct sigaction sa;
#endif

	job.fd = ctl_job.fd;
	job.pid = ctl_job.pid;
	peak_job_kill(&job);
	ctl_job.fd = PEAK_HANDLE_INVALID;
	ctl_job.pid = 0;
	ctl_job.client = -1;
	ctl_job.out_n = 0;
	ctl_job.trunc = false;
	ctl_job.dead = false;
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
	VTINFO("[Shell %-d] Exited successfully", sh.pid);
	peak_pty_close(&sh);
#ifndef _WIN32
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGCHLD, &sa, NULL);
	if (vt_chld_r != PEAK_HANDLE_INVALID) {
		peak_fd_close(vt_chld_r);
		vt_chld_r = PEAK_HANDLE_INVALID;
	}
	if (vt_chld_w >= 0) {
		close(vt_chld_w);
		vt_chld_w = -1;
	}
#endif
	term_destroy(&term);
	vt_ring_destroy(&ring);
}

static void
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

static void
vt_feed_stdin_to_ringbuffer(void)
{
	/* NOTE(vasco): Read into unused ring only. Never drop unread.
	 * Mirror map makes room bytes linear from the tail.
	 */
	size_t nmax;
	int r;

	VTASSERT(sh.fd != PEAK_HANDLE_INVALID && ring.base && ring.size);

	for (;;) {
		nmax = vt_ring_room(&ring);
		if (!nmax)
			return;
		r = peak_fd_read(sh.fd, vt_ring_tail(&ring), nmax);
		if (r > 0) {
			vt_ring_produce(&ring, (size_t)r);
			continue;
		}
		if (r == 0)
			vt_shell_gone();
		return;
	}
}

static void
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

static void
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
			} else {
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

static void
vt_ingest(void)
{
	for (;;) {
		size_t left;

		vt_feed_stdin_to_ringbuffer();
		left = ring.w - ring.r;
		if (!left)
			break;
		vt_feed_ring_drain();
		if (ring.w - ring.r == left)
			break;
	}
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

			if (key == PEAK_KEY_TAB && mod == PEAK_KEYMOD_SHIFT) {
				vt_sh_write("\033[Z", 3);
				break;
			}
			if ((u32)key < LEN(seq) && seq[key].n) {
				vt_sh_write(seq[key].s, seq[key].n);
				break;
			}

			if (mod == PEAK_KEYMOD_CTRL) {
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
				if (mod == PEAK_KEYMOD_SHIFT || mod == PEAK_KEYMOD_CAPS)
					ch = (char)(ch - 32);
				vt_sh_write(&ch, 1);
			}
			break;
		}
		case PEAK_EVENT_POINTER:
			if (event.pointer.state == PEAK_POINTER_PRESSED
					&& (event.pointer.type == PEAK_POINTER_WHEEL_UP
							|| event.pointer.type == PEAK_POINTER_WHEEL_DOWN)) {
				if (term.mode & TERM_MODE_MOUSE) {
					u32 cols, rows;
					u32 cw, ch;
					int x, y;
					int btn;
					char buf[32];
					int n;

					renderer_get_grid(&renderer, &cols, &rows);
					cw = atlas.cell_width;
					ch = atlas.cell_height;
					VTASSERT(cw && ch && cols && rows);
					x = (int)event.pointer.x / (int)cw + 1;
					y = (int)event.pointer.y / (int)ch + 1;
					if (x < 1)
						x = 1;
					if (y < 1)
						y = 1;
					if (x > (int)cols)
						x = (int)cols;
					if (y > (int)rows)
						y = (int)rows;
					btn = event.pointer.type == PEAK_POINTER_WHEEL_UP ? 64 : 65;
					if (term.mode & TERM_MODE_MOUSESGR) {
						n = snprintf(buf, sizeof buf, "\033[<%d;%d;%dM", btn, x, y);
						if (n > 0)
							vt_sh_write(buf, (size_t)n);
					} else {
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
				} else if (term.mode & TERM_MODE_ALTSCREEN)
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
		if (vt_ctl_put(fd, "\"id\":", strlen("\"id\":")) < 0)
			return -1;
		if (vt_ctl_put(fd, id, (size_t)id_n) < 0)
			return -1;
		if (vt_ctl_put(fd, ",", 1) < 0)
			return -1;
	}
	if (vt_ctl_put(fd, "\"ok\":", strlen("\"ok\":")) < 0)
		return -1;
	return vt_ctl_put(fd, ok ? "true" : "false", strlen(ok ? "true" : "false"));
}

static void
vt_ctl_client_close(VtCtlClient *c)
{
	VTASSERT(c);
	if (c->fd == PEAK_HANDLE_INVALID)
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
			|| vt_ctl_put(c->fd, ",\"error\":\"", strlen(",\"error\":\"")) < 0
			|| vt_ctl_put(c->fd, err, strlen(err)) < 0
			|| vt_ctl_put(c->fd, "\"}\n", strlen("\"}\n")) < 0)
		vt_ctl_client_close(c);
}

static void
vt_ctl_job_finish(void)
{
	VtCtlClient *c;
	int n;
	char head[80];

	if (!ctl_job.dead || ctl_job.fd != PEAK_HANDLE_INVALID)
		return;
	if (ctl_job.client >= 0 && ctl_job.client < VT_CTL_CLIENTS) {
		c = &ctl_clients[ctl_job.client];
		if (c->fd != PEAK_HANDLE_INVALID) {
			n = snprintf(head, sizeof head, "{\"ev\":\"exit\",\"job\":%u,", ctl_job.seq);
			if (n < 0 || (size_t)n >= sizeof head
					|| vt_ctl_put(c->fd, head, (size_t)n) < 0)
				goto drop;
			if (ctl_job.id_n > 0) {
				if (vt_ctl_put(c->fd, "\"id\":", strlen("\"id\":")) < 0
						|| vt_ctl_put(c->fd, ctl_job.id, (size_t)ctl_job.id_n) < 0
						|| vt_ctl_put(c->fd, ",", 1) < 0)
					goto drop;
			}
			n = snprintf(head, sizeof head, "\"code\":%d,\"out\":\"", ctl_job.code);
			if (n < 0 || (size_t)n >= sizeof head
					|| vt_ctl_put(c->fd, head, (size_t)n) < 0
					|| vt_ctl_put_escaped(c->fd, ctl_job.out, ctl_job.out_n) < 0)
				goto drop;
			if (ctl_job.trunc && vt_ctl_put(c->fd, "\",\"trunc\":true}\n", strlen("\",\"trunc\":true}\n")) < 0)
				goto drop;
			if (!ctl_job.trunc && vt_ctl_put(c->fd, "\"}\n", strlen("\"}\n")) < 0)
				goto drop;
		}
	}
	ctl_job.dead = false;
	ctl_job.out_n = 0;
	ctl_job.trunc = false;
	ctl_job.client = -1;
	ctl_job.id_n = 0;
	return;
drop:
	vt_ctl_client_close(c);
	ctl_job.dead = false;
	ctl_job.out_n = 0;
	ctl_job.trunc = false;
	ctl_job.client = -1;
	ctl_job.id_n = 0;
}

#ifndef _WIN32
static void
vt_reap_children(void)
{
	int pid, status;

	for (;;) {
		pid = waitpid(-1, &status, WNOHANG);
		if (pid <= 0)
			break;
		if (sh.pid > 0 && pid == sh.pid) {
			sh.pid = 0;
			running = false;
		} else if (ctl_job.pid > 0 && pid == ctl_job.pid) {
			ctl_job.pid = 0;
			ctl_job.dead = true;
			ctl_job.code = vt_status_code(status);
			vt_ctl_job_finish();
		}
	}
}
#else
static void
vt_ctl_job_reap(void)
{
	PeakProc job;
	int code;

	if (ctl_job.pid <= 0)
		return;
	job.fd = ctl_job.fd;
	job.pid = ctl_job.pid;
	if (!peak_job_reap(&job, &code))
		return;
	ctl_job.pid = 0;
	ctl_job.dead = true;
	ctl_job.code = code;
	if (ctl_job.fd != PEAK_HANDLE_INVALID) {
		peak_fd_close(ctl_job.fd);
		ctl_job.fd = PEAK_HANDLE_INVALID;
	}
	vt_ctl_job_finish();
}
#endif

static void
vt_ctl_job_read(void)
{
	if (ctl_job.fd == PEAK_HANDLE_INVALID)
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
#ifdef _WIN32
		vt_ctl_job_reap();
#else
		vt_ctl_job_finish();
#endif
		return;
	}
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
				|| vt_ctl_put(c->fd, "\"}\n", strlen("\"}\n")) < 0)
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
				|| vt_ctl_put(c->fd, "}\n", strlen("}\n")) < 0)
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
		if (ctl_job.pid > 0 || ctl_job.dead || ctl_job.fd != PEAK_HANDLE_INVALID) {
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
		ctl_job.dead = false;
		ctl_job.code = 0;
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
#ifndef _WIN32
	if (vt_chld_r != PEAK_HANDLE_INVALID) {
		char buf[64];

		while (peak_fd_read(vt_chld_r, buf, sizeof buf) > 0)
			;
	}
	vt_reap_children();
#else
	vt_ctl_job_reap();
#endif
}

static void
vt_wait(bool ready)
{
	PEAK_HANDLE fds[2 + VT_CTL_CLIENTS + 2];
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
#ifndef _WIN32
	if (vt_chld_r != PEAK_HANDLE_INVALID)
		fds[n++] = vt_chld_r;
#endif
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

	scr = term_screen(&term);
	VTASSERT(scr && scr->cell_buffer);
	renderer_sync(scr, term.cursor.x, term.cursor.y, !(term.mode & TERM_MODE_HIDE),
		(term.cursor.fg << 8) | term.cursor.attr, term.cursor.bg << 8);
}
#endif


static u32
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

static void
vt_dump_runs(FILE *out)
{
	u32 i;

	for (i = 0; i < nruns; i++) {
		VTASSERT((u32)runs[i].type < LEN(vt_run_name));
		fprintf(out, "%s %u\n", vt_run_name[runs[i].type], runs[i].n);
	}
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
vt_headless_run(const char *path, const char *shot, u32 cols, u32 rows, int dump_runs)
{
	FILE *in;
	FILE *dump;
	FILE *devnull;
	TermScreen *scr;
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

	sh.fd = fileno(in);
	if (dump_runs)
		vt_feed_stdin_to_ringbuffer();
	else
		vt_ingest();
	sh.fd = PEAK_HANDLE_INVALID;
	if (path)
		fclose(in);
	dump = (saved >= 0) ? fdopen(saved, "w") : stdout;
	if (dump_runs) {
		vt_feed_ringbuffer_to_runs();
		vt_dump_runs(dump ? dump : stdout);
	} else {
		vt_feed_ring_drain();
		vt_dump_screen(dump ? dump : stdout);
	}
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
		vt_ingest();
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
		int dump_runs = 0;
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
			} else if (strcmp(argv[i], "--dump-runs") == 0) {
				dump_runs = 1;
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
		return vt_headless_run(path, shot, cols, rows, dump_runs);
	}

#ifdef VT_HEADLESS
	fprintf(stderr, "vt: headless build; pass --headless\n");
	return 1;
#else
	if (!renderer_init()) {
		VTFATAL("Failed to initalize renderer!");
		VTASSERT(0, "Failed to initalize renderer!");
		renderer_destroy();
		return 1;
	}

    u32 cols, rows;
    bool dirty;

    vt_events(&dirty);
    if (!running) {
        renderer_destroy();
        return 0;
    }

    renderer_get_grid(&renderer, &cols, &rows);
    cols = (cols == 0) ? 80 : cols;
    rows = (rows == 0) ? 24 : rows;

    if (!vt_init(cols, rows)) {
        VTFATAL("Failed to initalize terminal!");
        VTASSERT(0, "Failed to initalize terminal!");
        vt_destroy();
        renderer_destroy();
        return 1;
    }

    while (running) {
        vt_wait(redraw);
        vt_events(&redraw);
        vt_ctl_pump();

        vt_ingest();

        if (redraw) {
#ifdef DEBUG
            u64 t0;
            u64 dt;

            t0 = peak_get_time();
            vt_present();
            dt = peak_get_time() - t0;
            VTDEBUG("present %llu ns", (unsigned long long)dt);
            vt_stage_add(VT_STAGE_PRESENT, dt);
#else
            vt_present();
#endif
            redraw = false;
        }
    }

    vt_destroy();
    renderer_destroy();
#ifdef DEBUG
    vt_stage_report();
#endif
    VTINFO("Quit successfully!");
    return 0;
#endif
}
