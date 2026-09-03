#define _GNU_SOURCE

#ifdef DEBUG
#define P_LOG_DEBUG_ENABLED 1
#define P_LOG_TRACE_ENABLED 1
#define TERM_DEBUG
#endif

#ifndef VT_HEADLESS
#define REND_VK_ARENA_GROW 1
#define REND_VK_SWAPCHAIN_EXTRA 1
#define REND_VK_COMPOSITE_PREFER_ALPHA
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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#include "lib/stb_image.h"
#pragma GCC diagnostic pop

#include <assert.h>
#include <immintrin.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vt.h"
#include "config.h"
#include "vt_debug.h"
#include "vt_term.c"
#include "vt_circ_buf.c"
#include "vt_lru.c"

typedef struct {
    char s[5];
    u8 n;
} VtSeq;

static TermColors vt_colors;
static VtSeq seq[] = {
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

static color_packed_t vt_pack_fg(unsigned i);
static color_packed_t vt_pack_bg(unsigned i);
static color_packed_t vt_pack_def_fg(void);
static color_packed_t vt_pack_def_bg(void);

color_packed_t
vt_pack_fg(unsigned i)
{
	if (i > 15)
		i = 15;
	return (color_packed_t)vt_colors.fg[i] << 8;
}

color_packed_t
vt_pack_bg(unsigned i)
{
	if (i > 7)
		i = 0;
	return (color_packed_t)vt_colors.bg[i] << 8;
}

color_packed_t
vt_pack_def_fg(void)
{
	return vt_pack_fg(vt_colors.fg_default < 16 ? vt_colors.fg_default : 7);
}

color_packed_t
vt_pack_def_bg(void)
{
	return vt_pack_bg(vt_colors.bg_default < 8 ? vt_colors.bg_default : 0);
}

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
static int vt_dump_row(TermScreen *s, u32 y, int (*put)(void *, const char *, size_t), void *ctx);
static u32 vt_dump_row_utf8(TermScreen *s, u32 y, char *dst, u32 cap);
static int vt_dump_walk(int (*put)(void *, const char *, size_t), void *ctx);
static int vt_dump_walk_rows(int (*put)(void *, const char *, size_t), void *ctx, u32 y0, u32 n);
static int vt_dump_file_put(void *ctx, const char *p, size_t n);
static void vt_dump_screen(FILE *out);
static void vt_dump_runs(FILE *out);
static void vt_shell_gone(void);
static size_t vt_sh_write(const char *const src, size_t len);
static int vt_feed_stdin_to_ringbuffer(void);
static void vt_feed_ringbuffer_to_runs(void);
static void vt_feed_ring_drain(void);
static int vt_ingest(void);
static u32 vt_utf8_atom(const char *data, u32 n, codepoint_t *cp);
#ifndef VT_HEADLESS
static void vt_events(bool *dirty);
static void vt_present(void);
#endif
static void vt_wait(int timeout_ms);
static int vt_hex_digit(int c);
static int vt_parse_hex6(const char *p, const char *end, uint32_t *out);
static int vt_theme_parse(const char *buf, unsigned long n, TermColors *c);
static int vt_theme_load(const char *str);
static void vt_theme_apply(void);
static void vt_theme_poll(void);
static size_t vt_base64_decode(const char *s, size_t n, char *dst, size_t cap);
static int vt_osc52(const char *p, u32 n);
#ifndef VT_HEADLESS
static void vt_clip_paste(PeakClip which);
static void vt_clip_take_write(void);
static void vt_drop_write(void);
static size_t vt_sel_utf8(char *dst, size_t cap);
static void vt_sel_to_primary(void);
static void vt_cell_at(float px, float py, u32 *x, u32 *y);
static void vt_mouse_report(int btn, u32 x, u32 y, int release, PeakKeyMod mod);
#endif

static VtRun runs[VT_RUN_MAX];
static u32 nruns;
static bool running = true;
static bool redraw = true;
static FILE *vt_in;
static u64 vt_feed_bytes;
static u64 vt_feed_read_ns;
static u64 vt_feed_parse_ns;
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
	"KITTY",
};
static const char vt_utf8_fffd[3] = { (char)0xEF, (char)0xBF, (char)0xBD };
static const u8 vt_b64[256] = {
	['+'] = 63, ['/'] = 64,
	['0'] = 53, ['1'] = 54, ['2'] = 55, ['3'] = 56, ['4'] = 57,
	['5'] = 58, ['6'] = 59, ['7'] = 60, ['8'] = 61, ['9'] = 62,
	['A'] = 1,  ['B'] = 2,  ['C'] = 3,  ['D'] = 4,  ['E'] = 5,
	['F'] = 6,  ['G'] = 7,  ['H'] = 8,  ['I'] = 9,  ['J'] = 10,
	['K'] = 11, ['L'] = 12, ['M'] = 13, ['N'] = 14, ['O'] = 15,
	['P'] = 16, ['Q'] = 17, ['R'] = 18, ['S'] = 19, ['T'] = 20,
	['U'] = 21, ['V'] = 22, ['W'] = 23, ['X'] = 24, ['Y'] = 25,
	['Z'] = 26,
	['a'] = 27, ['b'] = 28, ['c'] = 29, ['d'] = 30, ['e'] = 31,
	['f'] = 32, ['g'] = 33, ['h'] = 34, ['i'] = 35, ['j'] = 36,
	['k'] = 37, ['l'] = 38, ['m'] = 39, ['n'] = 40, ['o'] = 41,
	['p'] = 42, ['q'] = 43, ['r'] = 44, ['s'] = 45, ['t'] = 46,
	['u'] = 47, ['v'] = 48, ['w'] = 49, ['x'] = 50, ['y'] = 51,
	['z'] = 52,
};
static const char vt_theme_nm[10][12] = {
	"black", "red", "green", "yellow", "blue",
	"magenta", "cyan", "white", "background", "foreground",
};
/* [c-'a'][klen] = idx+1. (first letter, length) is unique. */
static const u8 vt_theme_id[26][11] = {
	['b' - 'a'][4] = 5,
	['b' - 'a'][5] = 1,
	['b' - 'a'][10] = 9,
	['c' - 'a'][4] = 7,
	['f' - 'a'][10] = 10,
	['g' - 'a'][5] = 3,
	['m' - 'a'][7] = 6,
	['r' - 'a'][3] = 2,
	['w' - 'a'][5] = 8,
	['y' - 'a'][6] = 4,
};


#include "vt_mux.c"
#include "vt_kitty.c"
#include "vt_ctl.c"

int
vt_hex_digit(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int
vt_parse_hex6(const char *p, const char *end, uint32_t *out)
{
	uint32_t rgb;
	int i;
	int d;

	if (!p || !out)
		return 0;
	while (p < end && (*p == ' ' || *p == '\t' || *p == '"' || *p == '\''))
		p++;
	if (p < end && *p == '#')
		p++;
	else if (p + 1 < end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	if (end - p < 6)
		return 0;
	rgb = 0;
	for (i = 0; i < 6; i++) {
		d = vt_hex_digit((unsigned char)p[i]);
		if (d < 0)
			return 0;
		rgb = (rgb << 4) | (uint32_t)d;
	}
	*out = rgb;
	return 1;
}

int
vt_theme_parse(const char *buf, unsigned long n, TermColors *c)
{
	const char *p;
	const char *end;
	const char *nl;
	int sec;
	int hit;
	int have_pfg;
	int have_pbg;
	uint32_t primary_fg;
	uint32_t primary_bg;

	if (!buf || !c || !n)
		return 0;
	p = buf;
	end = buf + n;
	sec = 0;
	hit = 0;
	have_pfg = 0;
	have_pbg = 0;
	primary_fg = 0;
	primary_bg = 0;
	while (p < end) {
		const char *line;
		const char *e;
		size_t klen;
		uint32_t rgb;
		unsigned idx;

		nl = p;
		while (nl < end && *nl != '\n' && *nl != '\r')
			nl++;
		line = p;
		e = nl;
		while (line < e && (*line == ' ' || *line == '\t'))
			line++;
		if (line < e && *line == '[') {
			const char *rb;

			line++;
			rb = line;
			while (rb < e && *rb != ']')
				rb++;
			sec = 0;
			if (rb - line >= 14 && memcmp(line, "colors.primary", 14) == 0)
				sec = 1;
			else if (rb - line >= 13 && memcmp(line, "colors.normal", 13) == 0)
				sec = 2;
			else if (rb - line >= 13 && memcmp(line, "colors.bright", 13) == 0)
				sec = 3;
		} else if (sec && line < e && *line != '#' && *line != ';') {
			klen = 0;
			while (line + klen < e) {
				char ch;

				ch = line[klen];
				if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_')
					klen++;
				else
					break;
			}
			if (klen) {
				const char *eq;

				eq = line + klen;
				while (eq < e && (*eq == ' ' || *eq == '\t'))
					eq++;
				if (eq < e && *eq == '=' && vt_parse_hex6(eq + 1, e, &rgb)) {
					idx = 16;
					if (klen <= 10) {
						unsigned char ch;
						u8 t;

						ch = (unsigned char)line[0];
						t = 0;
						if (ch >= 'a' && ch <= 'z')
							t = vt_theme_id[ch - 'a'][klen];
						if (t && memcmp(line, vt_theme_nm[t - 1], klen) == 0)
							idx = t - 1;
					}
					if (sec == 1) {
						if (idx == 8) {
							primary_bg = rgb;
							have_pbg = 1;
							hit = 1;
						} else if (idx == 9) {
							primary_fg = rgb;
							have_pfg = 1;
							hit = 1;
						}
					} else if (idx < 8) {
						if (sec == 2) {
							c->fg[idx] = rgb;
							c->bg[idx] = rgb;
						} else
							c->fg[idx + 8] = rgb;
						hit = 1;
					}
				}
			}
		}
		p = nl;
		while (p < end && (*p == '\n' || *p == '\r'))
			p++;
	}
	if (have_pbg)
		c->bg[c->bg_default < 8 ? c->bg_default : 0] = primary_bg;
	if (have_pfg)
		c->fg[c->fg_default < 16 ? c->fg_default : 7] = primary_fg;
	return hit;
}

int
vt_theme_load(const char *str)
{
	char home[256];
	char path[320];
	void *buf;
	unsigned long n;
	int ok;

	if (!peak_env_get("HOME", home, sizeof home))
		return 0;
	if (snprintf(path, sizeof path, "%s%s", home, str) < 0)
		return 0;
	if (!peak_file_exists(path))
		return 0;
	buf = peak_file_alloc(path, &n);
	if (!buf)
		return 0;
	ok = vt_theme_parse((const char *)buf, n, &vt_colors);
	free(buf);
	return ok;
}

void
vt_theme_apply(void)
{
	u32 i;

	vt_mux_colors = vt_colors;
	for (i = 0; i < VT_PANE_MAX; i++) {
		if (vt_panes[i].used)
			term_colors_set(&vt_panes[i].term, &vt_colors);
	}
}

void
vt_theme_poll(void)
{
	if (!peak_usr1_ack())
		return;
	if (vt_theme_load("/.config/omarchy/current/theme/alacritty.toml")
	 || vt_theme_load("/.config/vt/config.toml"))
		vt_theme_apply();
	redraw = true;
}

bool
vt_init_term(u32 cols, u32 rows)
{
	memset(&vt_colors, 0, sizeof vt_colors);
	memcpy(vt_colors.fg, ansi_fg, sizeof vt_colors.fg);
	memcpy(vt_colors.bg, ansi_bg, sizeof vt_colors.bg);
	vt_colors.fg_default = (uint32_t)fg_color;
	vt_colors.bg_default = (uint32_t)bg_color;

	if (!vt_theme_load("/.config/omarchy/current/theme/alacritty.toml"))
        (void) vt_theme_load("/.config/vt/config.toml");

	vt_mux_reset();
	if (!vt_mux_open(0, cols, rows, &vt_colors)) {
		VTFATAL("vt_ring_init");
		VTASSERT(0, "vt_init_term");
		return false;
	}
	vt_mux_bind(0);
	vt_mux_layout(cols, rows);
#ifndef VT_HEADLESS
	if (!renderer_instance_make(&renderer.instance, cols, rows)) {
		VTASSERT(0, "renderer_instance_make");
		vt_mux_close(0);
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

	peak_env_set("TERM", "xterm-256color");
	peak_env_set("COLUMNS", NULL);
	peak_env_set("LINES", NULL);
	peak_env_set("KITTY_WINDOW_ID", NULL);
	peak_env_set("KITTY_PID", NULL);
	peak_env_set("KITTY_LISTEN_ON", NULL);
	vt_ctl_init();
	peak_usr1_arm();
	proc = peak_pty_spawn("bash", argv, cols, rows,
			cols * atlas.cell_width, rows * atlas.cell_height);
	if (proc.fd == PEAK_HANDLE_INVALID) {
		VTFATAL("Could not open tty.");
		VTASSERT(0, "Could not open tty.");
		return false;
	}
	vt_sh = proc;
	return true;
}

void
vt_destroy(void)
{
	if (vt_feed_bytes) {
		u64 read_mib;
		u64 parse_mib;
		u64 total_mib;
		u64 total_ns;

		total_ns = vt_feed_read_ns + vt_feed_parse_ns;
		read_mib = vt_feed_read_ns
			? (vt_feed_bytes * 1000000000ull / vt_feed_read_ns) / (1024ull * 1024ull)
			: 0;
		parse_mib = vt_feed_parse_ns
			? (vt_feed_bytes * 1000000000ull / vt_feed_parse_ns) / (1024ull * 1024ull)
			: 0;
		total_mib = total_ns
			? (vt_feed_bytes * 1000000000ull / total_ns) / (1024ull * 1024ull)
			: 0;
		fprintf(stderr,
			"ingest %llu bytes  read %llu MiB/s  parse %llu MiB/s  total %llu MiB/s\n",
			(unsigned long long)vt_feed_bytes,
			(unsigned long long)read_mib,
			(unsigned long long)parse_mib,
			(unsigned long long)total_mib);
	}
	vt_ctl_destroy();
	vt_mux_destroy();
}

void
vt_shell_gone(void)
{
	vt_reap_children();
	if (!vt_panes[vt_cur].used)
		return;
	if (vt_sh.pid > 0)
		vt_mux_kill(vt_cur);
	else
		running = false;
}

int
vt_feed_stdin_to_ringbuffer(void)
{
	/* NOTE(vasco): Receive until EAGAIN/EOF. Preparse is after this.
	 * Unused caps the write so the mirror map cannot clobber unread.
	 */
	size_t unused;
	int r;
	u64 t0;

	VTASSERT(vt_ring.base && vt_ring.size);
	if (!vt_in)
		VTASSERT(vt_sh.fd != PEAK_HANDLE_INVALID);

	for (;;) {
		unused = vt_ring.size - (vt_ring.w - vt_ring.r);
		if (!unused)
			return 1;
		if (vt_in) {
			size_t n;

			t0 = peak_get_time();
			n = fread(vt_ring_tail(&vt_ring), 1, unused, vt_in);
			vt_feed_read_ns += peak_get_time() - t0;
			if (n > 0) {
				vt_ring_produce(&vt_ring, n);
				vt_feed_bytes += n;
				continue;
			}
			return 0;
		}
		t0 = peak_get_time();
		r = peak_fd_read(vt_sh.fd, vt_ring_tail(&vt_ring), unused);
		vt_feed_read_ns += peak_get_time() - t0;
		if (r > 0) {
			vt_ring_produce(&vt_ring, (size_t)r);
			vt_feed_bytes += (u64)r;
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

	const char *head;
	u32 n;
	u32 off;
#ifdef __AVX2__
	__m256i space;
	__m256i del;
#endif

	nruns = 0;
	VTASSERT(vt_ring.base && vt_ring.size);
	n = (u32)(vt_ring.w - vt_ring.r);
	head = vt_ring.base + (vt_ring.r % vt_ring.size);
	off = 0;
#ifdef __AVX2__
	space = _mm256_set1_epi8(0x20);
	del = _mm256_set1_epi8(0x7F);
#endif

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
#ifdef __AVX2__
			while (remaining - m >= 32) {
				__m256i batch;
				__m256i bad;
				int mask;

				batch = _mm256_loadu_si256((const __m256i *)(head + off + m));
				bad = _mm256_or_si256(_mm256_cmpgt_epi8(space, batch), _mm256_cmpeq_epi8(batch, del));
				mask = _mm256_movemask_epi8(bad);
				if (mask) {
					m += (u32)__tzcnt_u32((unsigned int)mask);
					break;
				}
				m += 32;
			}
#endif
			while (m < remaining) {
				ch = (unsigned char)head[off + m];
				if (ch < 0x20 || ch >= 0x7F)
					break;
				m++;
			}
		} else if (ch >= 0x80) {
			type = VT_RUN_UTF8;
			while (m < remaining) {
				u32 k;

				ch = (unsigned char)head[off + m];
				if (ch < 0x80)
					break;
				k = vt_utf8_atom(head + off + m, remaining - m, NULL);
				if (!k)
					break;
				m += k;
			}
		} else {
			int kitty;

			kitty = (ch == 0x1B && remaining >= 3
				&& head[off + 1] == '_' && head[off + 2] == 'G');
			type = kitty ? VT_RUN_KITTY : VT_RUN_ESCAPE;
			while (m < remaining) {
				u32 k;
				u32 left;
				int atom_kitty;

				ch = (unsigned char)head[off + m];
				if ((ch >= 0x20 && ch < 0x7F) || ch >= 0x80)
					break;
				left = remaining - m;
				k = 1;
				atom_kitty = 0;
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
							if (k && nch == '_' && left >= 3
								&& head[off + m + 2] == 'G')
								atom_kitty = 1;
						} else if (nch >= 0x20 && nch <= 0x2F) {
							k = left >= 3 ? 3 : 0;
						} else {
							k = 2;
						}
					}
				}
				if (!k)
					break;
				if (atom_kitty != kitty)
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
				else if (type == VT_RUN_KITTY) {
					if (!j)
						VTASSERT(b == 0x1B);
				} else if (!j)
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
	u64 t0;
	u64 dt;
	int fed;

	fed = 0;
	t0 = peak_get_time();
	for (;;) {
		const char *head;
		u32 i;
		u32 covered;

		vt_feed_ringbuffer_to_runs();
		if (!nruns)
			break;
		fed = 1;
		VTASSERT(vt_ring.base && vt_ring.size);
		head = vt_ring.base + (vt_ring.r % vt_ring.size);
		for (i = 0; i < nruns; i++) {
			const char *p;
			u32 n;
			int type;

			if (!runs[i].n)
				continue;
			p = head + runs[i].off;
			n = runs[i].n;
			type = runs[i].type;
			if (type == VT_RUN_UTF8) {
				u32 ui;
				u32 span;

				ui = 0;
				span = 0;
				while (ui < n) {
					u32 k;
					codepoint_t cp;

					k = vt_utf8_atom(p + ui, n - ui, &cp);
					if (k < 2) {
						if (span) {
							term_feed_utf8(vt_term_p, p + (ui - span), span);
							span = 0;
						}
						term_feed_utf8(vt_term_p, vt_utf8_fffd, 3);
						if (atlas.atlas)
							vt_glyph_get(UTF_INVALID);
						ui++;
						continue;
					}
					if (atlas.atlas)
						vt_glyph_get(cp);
					span += k;
					ui += k;
				}
				if (span)
					term_feed_utf8(vt_term_p, p + (n - span), span);
			} else if (type == VT_RUN_KITTY) {
				vt_kitty(p, n);
			} else if (!(type == VT_RUN_ESCAPE && vt_osc52(p, n))) {
				VTASSERT((u32)type < LEN(vt_run_feed));
				vt_run_feed[type](vt_term_p, p, n);
			}
		}
		if (nruns) {
			covered = runs[nruns - 1].off + runs[nruns - 1].n;
			vt_ring_consume(&vt_ring, covered);
			nruns = 0;
			redraw = true;
		}
		if (vt_term.reply_n) {
			if (vt_in || vt_sh.fd == PEAK_HANDLE_INVALID)
				vt_term.reply_n = 0;
			else if (peak_fd_write(vt_sh.fd, vt_term.reply, vt_term.reply_n) <= 0)
				VTFATAL("Failed to write to the shell");
			else
				vt_term.reply_n = 0;
		}
	}
	dt = peak_get_time() - t0;
	if (fed) {
		vt_feed_parse_ns += dt;
#ifdef DEBUG
		vt_stage_add(VT_STAGE_PARSE, dt);
#endif
	}
}

int
vt_ingest(void)
{
	u32 i;
	int full;

	full = 0;
	for (i = 0; i < VT_PANE_MAX; i++) {
		if (!vt_panes[i].used)
			continue;
		if (!vt_in && vt_panes[i].sh.fd == PEAK_HANDLE_INVALID)
			continue;
		if (vt_in && i != 0)
			continue;
		vt_mux_bind(i);
		full |= vt_feed_stdin_to_ringbuffer();
		if (!vt_panes[i].used)
			continue;
		if (vt_ring.w != vt_ring.r)
			vt_feed_ring_drain();
	}
	if (running && vt_panes[vt_focus].used)
		vt_mux_bind(vt_focus);
	else if (running)
		vt_mux_bind(vt_mux_first());
	return full;
}

size_t
vt_sh_write(const char *const src, size_t len)
{
	int r;

	if (vt_sh.fd == PEAK_HANDLE_INVALID)
		return 0;
	r = peak_fd_write(vt_sh.fd, src, len);
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
		v = vt_b64[c];
		if (!v)
			continue;
		v--;
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

#ifndef VT_HEADLESS
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
	if (!o)
		return;
	if (vt_term.mode & TERM_MODE_BRKTPASTE)
		vt_sh_write("\033[200~", 6);
	vt_sh_write(vt_clip_buf, o);
	if (vt_term.mode & TERM_MODE_BRKTPASTE)
		vt_sh_write("\033[201~", 6);
}

void
vt_clip_paste(PeakClip which)
{
	peak_clip_request(&win, which);
}

void
vt_drop_write(void)
{
	size_t n;
	size_t i;
	size_t o;

	n = 0;
	if (!peak_drop_take(&win, vt_clip_buf, VT_CLIP_MAX, &n) || !n)
		return;
	while (n && (vt_clip_buf[n - 1] == '\0' || vt_clip_buf[n - 1] == '\r'
			|| vt_clip_buf[n - 1] == '\n'))
		n--;
	o = 0;
	for (i = 0; i < n; i++) {
		int hi;
		int lo;

		hi = -1;
		lo = -1;
		if (vt_clip_buf[i] == '%' && i + 2 < n) {
			char a;
			char b;

			a = vt_clip_buf[i + 1];
			b = vt_clip_buf[i + 2];
			hi = (a >= '0' && a <= '9') ? a - '0' :
				(a >= 'a' && a <= 'f') ? a - 'a' + 10 :
				(a >= 'A' && a <= 'F') ? a - 'A' + 10 : -1;
			lo = (b >= '0' && b <= '9') ? b - '0' :
				(b >= 'a' && b <= 'f') ? b - 'a' + 10 :
				(b >= 'A' && b <= 'F') ? b - 'A' + 10 : -1;
		}
		if (hi >= 0 && lo >= 0) {
			vt_clip_buf[o++] = (char)((hi << 4) | lo);
			i += 2;
		} else
			vt_clip_buf[o++] = vt_clip_buf[i];
	}
	n = o;
	if (!n)
		return;
	o = 0;
	i = 0;
	while (i < n) {
		size_t s;
		size_t e;

		s = i;
		while (i < n && vt_clip_buf[i] != '\n' && vt_clip_buf[i] != '\r')
			i++;
		e = i;
		if (e - s >= 7 && memcmp(vt_clip_buf + s, "file://", 7) == 0) {
			s += 7;
			while (s < e && vt_clip_buf[s] != '/')
				s++;
		}
		while (s < e)
			vt_clip_buf[o++] = vt_clip_buf[s++];
		while (i < n && (vt_clip_buf[i] == '\n' || vt_clip_buf[i] == '\r'))
			i++;
		if (i < n)
			vt_clip_buf[o++] = '\n';
	}
	n = o;
	if (!n)
		return;
	if (n < VT_CLIP_MAX)
		vt_clip_buf[n] = 0;
	else
		vt_clip_buf[VT_CLIP_MAX] = 0;
	if (n >= 5 && memcmp(vt_clip_buf + n - 5, ".sock", 5) == 0) {
		char dir[192];
		char path[256];
		int m;

		if (peak_runtime_dir(dir, sizeof dir, "vt")) {
			m = snprintf(path, sizeof path, "%s/%d.sock", dir, peak_pid());
			if (m > 0 && (size_t)m == n && memcmp(path, vt_clip_buf, n) == 0) {
				vt_mux_drop_self();
				return;
			}
		}
		if (vt_mux_offer_take())
			return;
		if (vt_mux_pull(vt_clip_buf, -1))
			return;
	}
	if (vt_term.mode & TERM_MODE_BRKTPASTE)
		vt_sh_write("\033[200~", 6);
	vt_sh_write("'", 1);
	i = 0;
	while (i < n) {
		size_t s;

		s = i;
		while (i < n && vt_clip_buf[i] != '\'')
			i++;
		if (i > s)
			vt_sh_write(vt_clip_buf + s, i - s);
		if (i < n && vt_clip_buf[i] == '\'') {
			vt_sh_write("'\\''", 4);
			i++;
		}
	}
	vt_sh_write("'", 1);
	if (vt_term.mode & TERM_MODE_BRKTPASTE)
		vt_sh_write("\033[201~", 6);
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
	s = term_screen(vt_term_p);
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
		{
			TermCell *cell;

			cell = term_cell_at(s, i % cols, i / cols);
			cp = cell ? cell->codepoint : 0;
		}
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
	if (vt_term.mode & TERM_MODE_MOUSESGR) {
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
    /* NOTE(vasco): this code is... DIRTY */
	PeakEvent event;
    u32 cols, rows;
    PeakKeyCode key;
    PeakKeyMod mod;
    uint32_t code;
    char ch;


	while (peak_window_epoll(&win, &event)) {
		switch (event.type) {
		case PEAK_EVENT_WINDOW_CLOSE:
			running = false;
			break;
		case PEAK_EVENT_WINDOW_RESIZE:
			renderer.current_width = event.resize.width;
			renderer.current_height = event.resize.height;
			VTDEBUG("Resize %ux%u", event.resize.width, event.resize.height);

            renderer_get_grid(&renderer, &cols, &rows);
            VTASSERT(cols && rows);
            if (cols * rows != renderer_ninst) {
                RendBuffer inst;
                if (inst_prev.handle)
                    renderer_instance_release_prev();
                if (renderer_instance_make(&inst, cols, rows)) {
                    inst_prev = renderer.instance;
                    renderer.instance = inst;
                }
            }
            vt_mux_resize(cols, rows);
			*dirty = true;
			break;
		case PEAK_EVENT_KEY_DOWN: {
			key = event.key.key;
			mod = event.key.mod;
			code = event.key.code;

			if (vt_mux_key(key, mod, code))
				break;

			if (vt_mux_chord(clip_copy_key, clip_copy_mod, key, mod)) {
				size_t n;

				n = vt_sel_utf8(vt_clip_buf, VT_CLIP_MAX);
				if (n)
					peak_clip_set(&win, PEAK_CLIP_CLIPBOARD, vt_clip_buf, n);
				break;
			}
			if (vt_mux_chord(clip_paste_key, clip_paste_mod, key, mod)) {
				vt_clip_paste(PEAK_CLIP_CLIPBOARD);
				break;
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
			if (code >= 128)
				break;

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
		case PEAK_EVENT_TEXT: {
			size_t n;

			n = 0;
			if (!peak_text_take(NULL, vt_clip_buf, VT_CLIP_MAX, &n) || n < 2)
				break;
			vt_sh_write(vt_clip_buf, n);
			break;
		}
		case PEAK_EVENT_CLIP:
			vt_clip_take_write();
			break;
		case PEAK_EVENT_DROP:
			vt_drop_write();
			break;
		case PEAK_EVENT_POINTER: {
			u32 cx;
			u32 cy;
			int btn;
			PeakKeyMod pmod;

			vt_cell_at(event.pointer.x, event.pointer.y, &cx, &cy);
			pmod = event.pointer.mod;
			if (event.pointer.type == PEAK_POINTER_MIDDLE
					&& vt_mux_pointer(cx, cy, event.pointer.state, pmod)) {
				*dirty = true;
				if (vt_mux_click_paste) {
					vt_mux_click_paste = 0;
					vt_clip_paste(PEAK_CLIP_PRIMARY);
				}
				break;
			}
			{
				u32 lx;
				u32 ly;
				int hit;

				hit = vt_mux_pick(cx, cy, &lx, &ly);
				if (hit >= 0) {
					if (event.pointer.state == PEAK_POINTER_PRESSED)
						vt_mux_focus((u32)hit);
					if (hit == (int)vt_focus) {
						cx = lx;
						cy = ly;
					}
				}
			}
			if (event.pointer.type == PEAK_POINTER_WHEEL_UP
					|| event.pointer.type == PEAK_POINTER_WHEEL_DOWN) {
				if (event.pointer.state == PEAK_POINTER_PRESSED) {
					if (vt_term.mode & TERM_MODE_MOUSE)
						vt_mouse_report(event.pointer.type == PEAK_POINTER_WHEEL_UP ? 64 : 65,
							cx, cy, 0, pmod);
					else if (vt_term.mode & TERM_MODE_ALTSCREEN)
						vt_sh_write(event.pointer.type == PEAK_POINTER_WHEEL_UP
								? "\033[A" : "\033[B", 3);
				}
				break;
			}
			btn = event.pointer.type == PEAK_POINTER_RIGHT ? 2 :
				event.pointer.type == PEAK_POINTER_MIDDLE ? 1 : 0;
			if ((vt_term.mode & TERM_MODE_MOUSE) && !(pmod & PEAK_KEYMOD_SHIFT)) {
				if (event.pointer.state == PEAK_POINTER_PRESSED) {
					vt_mouse_btn = btn;
					vt_mouse_report(btn, cx, cy, 0, pmod);
				} else if (event.pointer.state == PEAK_POINTER_RELEASED) {
					vt_mouse_report(vt_mouse_btn >= 0 ? vt_mouse_btn : btn, cx, cy, 1, pmod);
					vt_mouse_btn = -1;
				} else if (event.pointer.state == PEAK_POINTER_MOVED) {
					int motion;

					motion = 0;
					if (vt_term.mode & TERM_MODE_MOUSEMANY)
						motion = 1;
					else if ((vt_term.mode & TERM_MODE_MOUSEMOT) && vt_mouse_btn >= 0)
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
	PEAK_HANDLE fds[VT_PANE_MAX + VT_CTL_CLIENTS + 5];
	u32 n;
	PEAK_HANDLE usr;

	n = 0;
	n += vt_mux_fds(fds);
	n += vt_ctl_fds(fds + n);
	usr = peak_usr1_fd();
	if (usr != PEAK_HANDLE_INVALID)
		fds[n++] = usr;
	peak_wait(VT_PEAK_WIN, fds, n, timeout_ms);
	vt_theme_poll();
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
	if (vt_mux_single()) {
		scr = term_screen(vt_term_p);
		VTASSERT(scr && scr->cell_buffer);
		{
			u32 sel0;
			u32 sel1;
			u32 a;
			u32 b;
			TermStyle cs;

			a = vt_sel_ay * scr->cols + vt_sel_ax;
			b = vt_sel_by * scr->cols + vt_sel_bx;
			sel0 = a < b ? a : b;
			sel1 = a < b ? b : a;
			cs = term_cursor_style(vt_term_p);
			renderer_sync(vt_term_p, scr, vt_term.cursor.x, vt_term.cursor.y, !(vt_term.mode & TERM_MODE_HIDE),
				cs.fg, cs.bg,
				vt_sel_on, sel0, sel1);
		}
	} else {
		vt_mux_present();
	}
#ifdef DEBUG
	dt = peak_get_time() - t0;
	VTDEBUG("present %llu ns", (unsigned long long)dt);
	vt_stage_add(VT_STAGE_PRESENT, dt);
#endif
}
#endif


u32
vt_utf8_atom(const char *data, u32 n, codepoint_t *cp)
{
	unsigned char ch;
	u32 need;
	u32 i;
	codepoint_t u;

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
	else {
		if (cp)
			*cp = UTF_INVALID;
		return 1;
	}
	if (n < need) {
		for (i = 1; i < n; i++) {
			if (((unsigned char)data[i] & 0xC0) != 0x80) {
				if (cp)
					*cp = UTF_INVALID;
				return 1;
			}
		}
		return 0;
	}
	for (i = 1; i < need; i++) {
		if (((unsigned char)data[i] & 0xC0) != 0x80) {
			if (cp)
				*cp = UTF_INVALID;
			return 1;
		}
	}
	if (need == 2)
		u = (codepoint_t)(((ch & 0x1F) << 6) | ((unsigned char)data[1] & 0x3F));
	else if (need == 3)
		u = (codepoint_t)(((ch & 0x0F) << 12) | (((unsigned char)data[1] & 0x3F) << 6) | ((unsigned char)data[2] & 0x3F));
	else
		u = (codepoint_t)(((ch & 0x07) << 18) | (((unsigned char)data[1] & 0x3F) << 12) | (((unsigned char)data[2] & 0x3F) << 6) | ((unsigned char)data[3] & 0x3F));
	if (cp)
		*cp = u;
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
vt_dump_row(TermScreen *s, u32 y, int (*put)(void *, const char *, size_t), void *ctx)
{
	TermCell *row;
	u32 x, last_col;

	row = term_row(s, y);
	if (!row)
		return put(ctx, "\n", 1);
	last_col = 0;
	for (x = 0; x < s->cols; x++) {
		if (row[x].codepoint)
			last_col = x + 1;
	}
	for (x = 0; x < last_col; x++) {
		codepoint_t c = row[x].codepoint;
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
	return put(ctx, "\n", 1);
}

u32
vt_dump_row_utf8(TermScreen *s, u32 y, char *dst, u32 cap)
{
	TermCell *row;
	u32 x, last_col, o;

	row = term_row(s, y);
	if (!row)
		return 0;
	last_col = 0;
	for (x = 0; x < s->cols; x++) {
		if (row[x].codepoint)
			last_col = x + 1;
	}
	o = 0;
	for (x = 0; x < last_col; x++) {
		codepoint_t c = row[x].codepoint;
		char buf[4];
		int n;

		if (!c) {
			if (o >= cap)
				return o;
			dst[o++] = ' ';
			continue;
		}
		n = vt_utf8_encode(c, buf);
		if (o + (u32)n > cap)
			return o;
		memcpy(dst + o, buf, (size_t)n);
		o += (u32)n;
	}
	return o;
}

int
vt_dump_walk(int (*put)(void *, const char *, size_t), void *ctx)
{
	TermScreen *s;
	u32 y, x, last_row;

	s = term_screen(vt_term_p);
	if (!s || !s->cell_buffer)
		return -1;
	last_row = 0;
	for (y = 0; y < s->rows; y++) {
		TermCell *row;

		row = term_row(s, y);
		if (!row)
			continue;
		for (x = 0; x < s->cols; x++) {
			if (row[x].codepoint)
				last_row = y;
		}
	}
	for (y = 0; y <= last_row; y++) {
		if (vt_dump_row(s, y, put, ctx) < 0)
			return -1;
	}
	return 0;
}

int
vt_dump_walk_rows(int (*put)(void *, const char *, size_t), void *ctx, u32 y0, u32 n)
{
	TermScreen *s;
	u32 y;

	s = term_screen(vt_term_p);
	if (!s || !s->cell_buffer)
		return -1;
	if (y0 >= s->rows)
		return 0;
	if (y0 + n > s->rows)
		n = s->rows - y0;
	for (y = 0; y < n; y++) {
		if (vt_dump_row(s, y0 + y, put, ctx) < 0)
			return -1;
	}
	return 0;
}

int
vt_dump_file_put(void *ctx, const char *p, size_t n)
{
	return fwrite(p, 1, n, (FILE *)ctx) == n ? 0 : -1;
}

void
vt_dump_screen(FILE *out)
{
	(void)vt_dump_walk(vt_dump_file_put, out);
}
