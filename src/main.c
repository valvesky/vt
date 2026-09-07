#define _GNU_SOURCE

#ifdef DEBUG
#define P_LOG_DEBUG_ENABLED 1
#define P_LOG_TRACE_ENABLED 1
#define TERM_DEBUG
#endif

#define REND_VK_ARENA_GROW 1
#define REND_VK_SWAPCHAIN_EXTRA 1
#define REND_VK_COMPOSITE_PREFER_ALPHA
#include "rend.h"
#include "peak.c"
#include "rend.c"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"

#include <assert.h>
#include <immintrin.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "vt.h"
#include "config.h"
#include "shell.c"
#include "term.c"
#include "ringbuffer.c"

#include "glyph_cache.c"
#include "glyph_generator.c"

typedef struct {
    char s[5];
    u8 n;
} Seq;

static TermColors colors;
static Seq seq[] = {
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

#include "renderer_gpu.c"

static bool init_term(u32 cols, u32 rows);
static bool init(u32 cols, u32 rows);
static void destroy(void);
static void present(void);
static int main_windowed(int argc, char **argv);
static int arg_mode(const char *s);
static int main_headless(int argc, char **argv);
static int main_live(int argc, char **argv);
static int utf8_encode(codepoint_t c, char out[4]);
static int dump_row(TermScreen *s, u32 y, int (*put)(void *, const char *, size_t), void *ctx);
static u32 dump_row_utf8(TermScreen *s, u32 y, char *dst, u32 cap);
static int dump_walk(int (*put)(void *, const char *, size_t), void *ctx);
static int dump_walk_rows(int (*put)(void *, const char *, size_t), void *ctx, u32 y0, u32 n);
static int dump_file_put(void *ctx, const char *p, size_t n);
static void dump_screen(FILE *out);
static void dump_runs(FILE *out);
static void drain(void);
static int ingest(void);
static void events(bool *dirty);

static void wait_io(int timeout_ms);
static u64 frame_period_ns(void);
static int io_timeout_ms(void);
static int hex_digit(int c);
static int parse_hex6(const char *p, const char *end, uint32_t *out);

static int theme_parse(const char *buf, unsigned long n, TermColors *c);
static int theme_load(const char *str);
static void theme_apply(void);
static void theme_poll(void);

static size_t base64_decode(const char *s, size_t n, char *dst, size_t cap);

static void clip_paste(PeakClip which);
static void clip_take_write(void);
static void drop_write(void);
static size_t sel_utf8(char *dst, size_t cap);
static void sel_to_primary(void);
static void cell_at(float px, float py, u32 *x, u32 *y);
static void mouse_report(int btn, u32 x, u32 y, int release, PeakKeyMod mod);


static bool running = true;
static bool redraw = true;
static u64 feed_bytes;
static u64 feed_consume;
static u64 consume_ns;
static u64 feed_read_ns;
static u64 feed_parse_ns;
#define CLIP_MAX (1024u * 1024u)
static int sel_on;
static u32 sel_ax, sel_ay, sel_bx, sel_by;
static int sel_drag;
static int mouse_btn = -1;
static char clip_buf[CLIP_MAX + 1];
static void (*const run_feed[])(Term *, const char *, size_t) = {
	term_feed_printable,
	term_feed_escape,
	term_feed_utf8,
};
static const u8 b64[256] = {
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
static const char theme_nm[10][12] = {
	"black", "red", "green", "yellow", "blue",
	"magenta", "cyan", "white", "background", "foreground",
};
/* [c-'a'][klen] = idx+1. (first letter, length) is unique. */
static const u8 theme_id[26][11] = {
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


static void shell_gone(Pane *p);
static size_t pane_write(Pane *p, const char *const src, size_t len);
static int pane_ingest(Pane *p);
static void pane_drain(Pane *p);

static Renderer *renderer;
static Multiplexor multiplexor;

#define VT_PEAK_WIN (renderer ? &renderer->win : NULL)
#include "multiplexing.c"
#include "kitty.c"
#include "osc.c"

#include "ctl.c"

int
hex_digit(int c)
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
parse_hex6(const char *p, const char *end, uint32_t *out)
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
		d = hex_digit((unsigned char)p[i]);
		if (d < 0)
			return 0;
		rgb = (rgb << 4) | (uint32_t)d;
	}
	*out = rgb;
	return 1;
}

int
theme_parse(const char *buf, unsigned long n, TermColors *c)
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
				if (eq < e && *eq == '=' && parse_hex6(eq + 1, e, &rgb)) {
					idx = 16;
					if (klen <= 10) {
						unsigned char ch;
						u8 t;

						ch = (unsigned char)line[0];
						t = 0;
						if (ch >= 'a' && ch <= 'z')
							t = theme_id[ch - 'a'][klen];
						if (t && memcmp(line, theme_nm[t - 1], klen) == 0)
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
theme_load(const char *str)
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
	ok = theme_parse((const char *)buf, n, &colors);
	free(buf);
	return ok;
}

void
theme_apply(void)
{
	u32 i;

	multiplexor.mux_colors = colors;
	for (i = 0; i < PANE_MAX; i++) {
		if (multiplexor.panes[i].used)
			term_colors_set(&multiplexor.panes[i].term, &colors);
	}
}

void
theme_poll(void)
{
	if (!peak_usr1_ack())
		return;
	if (theme_load("/.config/omarchy/current/theme/alacritty.toml")
	 || theme_load("/.config/vt/config.toml"))
		theme_apply();
	redraw = true;
}

bool
init_term(u32 cols, u32 rows)
{
	memset(&colors, 0, sizeof colors);
	memcpy(colors.fg, ansi_fg, sizeof colors.fg);
	memcpy(colors.bg, ansi_bg, sizeof colors.bg);
	colors.fg_default = (uint32_t)fg_color;
	colors.bg_default = (uint32_t)bg_color;

	if (!theme_load("/.config/omarchy/current/theme/alacritty.toml"))
        (void) theme_load("/.config/vt/config.toml");

	mux_reset(&multiplexor);
	if (!mux_open(&multiplexor, 0, cols, rows, &colors, renderer)) {
		VTFATAL("ring_init");
		VTASSERT(0, "init_term");
		return false;
	}
	mux_bind(&multiplexor, 0);
	mux_layout(&multiplexor, cols, rows, renderer);
	return true;
}

bool
init(u32 cols, u32 rows)
{
	PeakProc proc;

	if (!init_term(cols, rows))
		return false;

	peak_env_set("TERM", "xterm-256color");
	peak_env_set("COLUMNS", NULL);
	peak_env_set("LINES", NULL);
	peak_env_set("KITTY_WINDOW_ID", NULL);
	peak_env_set("KITTY_PID", NULL);
	peak_env_set("KITTY_LISTEN_ON", NULL);

	ctl_init();
	peak_usr1_arm();

	{
		u32 cw;
		u32 ch;

		cw = renderer ? renderer->atlas.cell_width : 0;
		ch = renderer ? renderer->atlas.cell_height : 0;
		proc = vt_shell_spawn(cols, rows, cols * cw, rows * ch);
	}
	if (proc.fd == PEAK_HANDLE_INVALID) {
		VTFATAL("Could not open tty.");
		VTASSERT(0, "Could not open tty.");
		return false;
	}
	multiplexor.pane->sh = proc;
	return true;
}

void
destroy(void)
{
	if (feed_bytes) {
		u64 read_mib;
		u64 parse_mib;
		u64 total_mib;
		u64 total_ns;

		total_ns = feed_read_ns + feed_parse_ns;
		read_mib = feed_read_ns
			? (feed_bytes * 1000000000ull / feed_read_ns) / (1024ull * 1024ull)
			: 0;
		parse_mib = feed_parse_ns
			? (feed_bytes * 1000000000ull / feed_parse_ns) / (1024ull * 1024ull)
			: 0;
		total_mib = total_ns
			? (feed_bytes * 1000000000ull / total_ns) / (1024ull * 1024ull)
			: 0;
		fprintf(stderr,
			"ingest %llu bytes  read %llu MiB/s  parse %llu MiB/s  total %llu MiB/s  consume %llu\n",
			(unsigned long long)feed_bytes,
			(unsigned long long)read_mib,
			(unsigned long long)parse_mib,
			(unsigned long long)total_mib,
			(unsigned long long)feed_consume);
		if (feed_read_ns && feed_bytes > 1024ull * 1024ull && read_mib <= 200)
			VTWARN("pty read %llu MiB/s (slow: <= 200)", (unsigned long long)read_mib);
	}
	ctl_destroy();
	mux_destroy(&multiplexor);
}

void
shell_gone(Pane *p)
{
	reap_children();
	if (!p || !p->used)
		return;
	if (p->sh.pid > 0)
		mux_kill_pane(&multiplexor, (u32)(p - multiplexor.panes), renderer);
	else
		running = false;
}

int
pane_ingest(Pane *p)
{
	/* NOTE(vasco): Receive until EAGAIN/EOF. Feed keeps the tail. */
	int r;
	int any;
	u64 t0;

    /* pane may have diead */
	if (!p->rb) return 0;


	any = 0;
	for (;;) {
		t0 = peak_get_time();
		r = vt_shell_read(&p->sh, p->rb->base + (p->rb->write % p->rb->size), p->rb->size);
		if (r > 0) {
			feed_read_ns += peak_get_time() - t0;
			ringbuffer_produce(p->rb, (size_t)r);
			feed_bytes += (u64)r;
			any = 1;
			continue;
		}
		if (r == 0) {
			if (p->rb && p->rb->write != p->rb->read)
				pane_drain(p);
			shell_gone(p);
			return any;
		}
		return any;
	}

}

void
pane_drain(Pane *pane)
{
	RingBuffer *rb;
	u64 t0;
	u64 dt;
	int fed;

	if (!pane || !pane->rb)
		return;
	rb = pane->rb;
	fed = 0;
	t0 = peak_get_time();
	{
		u32 i;

		ringbuffer_consume(rb);
		feed_consume++;
		if (rb->run_n) {
			fed = 1;
			VTASSERT(rb->base && rb->size);
			for (i = 0; i < rb->run_n; i++) {
				const char *p;
				u32 n;
				RunType type;

				if (!rb->run[i].n)
					continue;

				p = rb->base + (rb->run[i].off % rb->size);
				n = (u32)rb->run[i].n;

				type = rb->run[i].type;

				if (type == RUN_KITTY) {
					vt_kitty(pane, p, n);
				} else if (!(type == RUN_ESCAPE && osc52(p, n))) {
					VTASSERT((u32)type < LEN(run_feed));
					run_feed[type](&pane->term, p, n);
				}
			}
			rb->read = rb->parsed;
			rb->run_n = 0;
			redraw = true;
			if (pane->term.reply_n) {
				if (pane->sh.fd == PEAK_HANDLE_INVALID)
					pane->term.reply_n = 0;
				else if (vt_shell_write(&pane->sh, pane->term.reply, pane->term.reply_n) <= 0)
					VTFATAL("Failed to write to the shell");
				else
					pane->term.reply_n = 0;
			}
		}
	}
	dt = peak_get_time() - t0;
	if (fed)
		feed_parse_ns += dt;
}

int
ingest(void)
{
	u32 i;
	int any;

	any = 0;
	for (i = 0; i < PANE_MAX; i++) {
		Pane *p;

		p = &multiplexor.panes[i];
		if (!p->used)
			continue;
		if (p->sh.fd == PEAK_HANDLE_INVALID)
			continue; // cleaned up later
		any |= pane_ingest(p);
		if (!p->used || !p->rb)
			continue;
		if (p->rb->write != p->rb->read)
			redraw = true;
	}

	if (running && multiplexor.panes[multiplexor.focus].used)
		mux_bind(&multiplexor, multiplexor.focus);
	else if (running)
		mux_bind(&multiplexor, mux_first(&multiplexor));
	return any;
}

void
drain(void)
{
	u32 i;

	for (i = 0; i < PANE_MAX; i++) {
		Pane *p;

		p = &multiplexor.panes[i];
		if (!p->used || !p->rb)
			continue;
		if (p->rb->write != p->rb->read)
			pane_drain(p);
	}
	if (running && multiplexor.panes[multiplexor.focus].used)
		mux_bind(&multiplexor, multiplexor.focus);
	else if (running)
		mux_bind(&multiplexor, mux_first(&multiplexor));
}

size_t
pane_write(Pane *p, const char *const src, size_t len)
{
	int r;

	if (!p || p->sh.fd == PEAK_HANDLE_INVALID)
		return 0;
	r = vt_shell_write(&p->sh, src, len);
	if (r <= 0) {
		if (r == 0)
			shell_gone(p);
		return 0;
	}
	return (size_t)r;
}

size_t
base64_decode(const char *s, size_t n, char *dst, size_t cap)
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
		v = b64[c];
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

void
clip_take_write(void)
{
	size_t n;
	size_t i;
	size_t o;

	n = 0;
	if (!peak_clip_take(&renderer->win, clip_buf, CLIP_MAX, &n) || !n)
		return;
	o = 0;
	for (i = 0; i < n; i++) {
		char c;

		c = clip_buf[i];
		if (c == '\n' && (o == 0 || clip_buf[o - 1] != '\r'))
			c = '\r';
		else if (c == '\n')
			continue;
		clip_buf[o++] = c;
	}
	if (!o)
		return;
	if (multiplexor.pane->term.mode & TERM_MODE_BRKTPASTE)
		pane_write(multiplexor.pane, "\033[200~", 6);
	pane_write(multiplexor.pane, clip_buf, o);
	if (multiplexor.pane->term.mode & TERM_MODE_BRKTPASTE)
		pane_write(multiplexor.pane, "\033[201~", 6);
}

void
clip_paste(PeakClip which)
{
	peak_clip_request(&renderer->win, which);
}

void
drop_write(void)
{
	size_t n;
	size_t i;
	size_t o;

	n = 0;
	if (!peak_drop_take(&renderer->win, clip_buf, CLIP_MAX, &n) || !n)
		return;
	while (n && (clip_buf[n - 1] == '\0' || clip_buf[n - 1] == '\r'
			|| clip_buf[n - 1] == '\n'))
		n--;
	o = 0;
	for (i = 0; i < n; i++) {
		int hi;
		int lo;

		hi = -1;
		lo = -1;
		if (clip_buf[i] == '%' && i + 2 < n) {
			char a;
			char b;

			a = clip_buf[i + 1];
			b = clip_buf[i + 2];
			hi = (a >= '0' && a <= '9') ? a - '0' :
				(a >= 'a' && a <= 'f') ? a - 'a' + 10 :
				(a >= 'A' && a <= 'F') ? a - 'A' + 10 : -1;
			lo = (b >= '0' && b <= '9') ? b - '0' :
				(b >= 'a' && b <= 'f') ? b - 'a' + 10 :
				(b >= 'A' && b <= 'F') ? b - 'A' + 10 : -1;
		}
		if (hi >= 0 && lo >= 0) {
			clip_buf[o++] = (char)((hi << 4) | lo);
			i += 2;
		} else
			clip_buf[o++] = clip_buf[i];
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
		while (i < n && clip_buf[i] != '\n' && clip_buf[i] != '\r')
			i++;
		e = i;
		if (e - s >= 7 && memcmp(clip_buf + s, "file://", 7) == 0) {
			s += 7;
			while (s < e && clip_buf[s] != '/')
				s++;
		}
		while (s < e)
			clip_buf[o++] = clip_buf[s++];
		while (i < n && (clip_buf[i] == '\n' || clip_buf[i] == '\r'))
			i++;
		if (i < n)
			clip_buf[o++] = '\n';
	}
	n = o;
	if (!n)
		return;
	if (n < CLIP_MAX)
		clip_buf[n] = 0;
	else
		clip_buf[CLIP_MAX] = 0;
	if (n >= 5 && memcmp(clip_buf + n - 5, ".sock", 5) == 0) {
		char dir[192];
		char path[256];
		int m;

		if (peak_runtime_dir(dir, sizeof dir, "vt")) {
			m = snprintf(path, sizeof path, "%s/%d.sock", dir, peak_pid());
			if (m > 0 && (size_t)m == n && memcmp(path, clip_buf, n) == 0) {
				mux_drop_self(&multiplexor, renderer);
				return;
			}
		}
		if (mux_offer_take(&multiplexor, renderer))
			return;
		if (mux_pull(&multiplexor, clip_buf, -1, renderer))
			return;
	}
	if (multiplexor.pane->term.mode & TERM_MODE_BRKTPASTE)
		pane_write(multiplexor.pane, "\033[200~", 6);
	pane_write(multiplexor.pane, "'", 1);
	i = 0;
	while (i < n) {
		size_t s;

		s = i;
		while (i < n && clip_buf[i] != '\'')
			i++;
		if (i > s)
			pane_write(multiplexor.pane, clip_buf + s, i - s);
		if (i < n && clip_buf[i] == '\'') {
			pane_write(multiplexor.pane, "'\\''", 4);
			i++;
		}
	}
	pane_write(multiplexor.pane, "'", 1);
	if (multiplexor.pane->term.mode & TERM_MODE_BRKTPASTE)
		pane_write(multiplexor.pane, "\033[201~", 6);
}

size_t
sel_utf8(char *dst, size_t cap)
{
	TermScreen *s;
	u32 a;
	u32 b;
	u32 i;
	u32 cols;
	size_t o;

	if (!sel_on || !dst || !cap)
		return 0;
	s = term_screen(&multiplexor.pane->term);
	if (!s || !s->line || !s->cols)
		return 0;
	cols = s->cols;
	a = sel_ay * cols + sel_ax;
	b = sel_by * cols + sel_bx;
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
		k = utf8_encode(cp, enc);
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
sel_to_primary(void)
{
	size_t n;

	n = sel_utf8(clip_buf, CLIP_MAX);
	if (!n)
		return;
	peak_clip_set(&renderer->win, PEAK_CLIP_PRIMARY, clip_buf, n);
}

void
cell_at(float px, float py, u32 *x, u32 *y)
{
	u32 cols;
	u32 rows;
	u32 cw;
	u32 ch;
	int cx;
	int cy;

	cols = multiplexor.cols;
	rows = multiplexor.rows;
	cw = renderer->atlas.cell_width;
	ch = renderer->atlas.cell_height;
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
mouse_report(int btn, u32 x, u32 y, int release, PeakKeyMod mod)
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
	if (multiplexor.pane->term.mode & TERM_MODE_MOUSESGR) {
		n = snprintf(buf, sizeof buf, "\033[<%d;%u;%u%c", b, x, y, release ? 'm' : 'M');
		if (n > 0)
			pane_write(multiplexor.pane, buf, (size_t)n);
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
	pane_write(multiplexor.pane, buf, 6);
}

void
wait_io(int timeout_ms)
{
	PEAK_HANDLE fds[PANE_MAX + CTL_CLIENTS + 5];
	u32 n;
	PEAK_HANDLE usr;

	n = 0;
	n += mux_fds(&multiplexor, fds);
	n += ctl_fds(fds + n);
	usr = peak_usr1_fd();
	if (usr != PEAK_HANDLE_INVALID)
		fds[n++] = usr;
	peak_wait(renderer ? &renderer->win : NULL, fds, n, timeout_ms);
	theme_poll();
}

u64
frame_period_ns(void)
{
	if (hz <= 0)
		return 0;
	return 1000000000ull / (u64)hz;
}

int
io_timeout_ms(void)
{
	u64 fns;
	u64 now;
	u64 dt;

	if (!redraw)
		return -1;
	if (vsync)
		return 0;
	fns = frame_period_ns();
	if (!fns)
		return 0;
	now = peak_get_time();
	if (now - consume_ns >= fns)
		return 0;
	dt = fns - (now - consume_ns);
	return (int)((dt + 999999ull) / 1000000ull);
}

void
dump_runs(FILE *out)
{
	RingBuffer *rb;
	u32 i;

	if (!multiplexor.pane || !multiplexor.pane->rb)
		return;
	rb = multiplexor.pane->rb;
	for (i = 0; i < rb->run_n; i++) {
		VTASSERT((u32)rb->run[i].type < LEN(ring_buffer_run_name));
		fprintf(out, "%s %u\n", ring_buffer_run_name[rb->run[i].type], (u32)rb->run[i].n);
	}
}

int
utf8_encode(codepoint_t c, char out[4])
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
dump_row(TermScreen *s, u32 y, int (*put)(void *, const char *, size_t), void *ctx)
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
		n = utf8_encode(c, buf);
		if (put(ctx, buf, (size_t)n) < 0)
			return -1;
	}
	return put(ctx, "\n", 1);
}

u32
dump_row_utf8(TermScreen *s, u32 y, char *dst, u32 cap)
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
		n = utf8_encode(c, buf);
		if (o + (u32)n > cap)
			return o;
		memcpy(dst + o, buf, (size_t)n);
		o += (u32)n;
	}
	return o;
}

int
dump_walk(int (*put)(void *, const char *, size_t), void *ctx)
{
	TermScreen *s;
	u32 y, x, last_row;

	s = term_screen(&multiplexor.pane->term);
	if (!s || !s->line)
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
		if (dump_row(s, y, put, ctx) < 0)
			return -1;
	}
	return 0;
}

int
dump_walk_rows(int (*put)(void *, const char *, size_t), void *ctx, u32 y0, u32 n)
{
	TermScreen *s;
	u32 y;

	s = term_screen(&multiplexor.pane->term);
	if (!s || !s->line)
		return -1;
	if (y0 >= s->rows)
		return 0;
	if (y0 + n > s->rows)
		n = s->rows - y0;
	for (y = 0; y < n; y++) {
		if (dump_row(s, y0 + y, put, ctx) < 0)
			return -1;
	}
	return 0;
}

int
dump_file_put(void *ctx, const char *p, size_t n)
{
	return fwrite(p, 1, n, (FILE *)ctx) == n ? 0 : -1;
}

void
dump_screen(FILE *out)
{
	(void)dump_walk(dump_file_put, out);
}

void
events(bool *dirty)
{
    /* NOTE(vasco): this code is... DIRTY */
	PeakEvent event;
    u32 cols, rows;
    PeakKeyCode key;
    PeakKeyMod mod;
    uint32_t code;
    char ch;


	while (peak_window_epoll(&renderer->win, &event)) {
		switch (event.type) {
		case PEAK_EVENT_WINDOW_CLOSE:
			running = false;
			break;
		case PEAK_EVENT_WINDOW_RESIZE:
			renderer->current_width = event.resize.width;
			renderer->current_height = event.resize.height;
			VTDEBUG("Resize %ux%u", event.resize.width, event.resize.height);
			{
				u32 cw;
				u32 ch;

				cw = renderer->atlas.cell_width;
				ch = renderer->atlas.cell_height;
				cols = cw ? event.resize.width / cw : 0;
				rows = ch ? event.resize.height / ch : 0;
			}
			VTASSERT(cols && rows);
			mux_resize(&multiplexor, cols, rows, renderer);
			*dirty = true;
			break;
		case PEAK_EVENT_KEY_DOWN: {
			key = event.key.key;
			mod = event.key.mod;
			code = event.key.code;

			if (mux_key(&multiplexor, key, mod, code, renderer))
				break;

			if (mux_chord(clip_copy_key, clip_copy_mod, key, mod)) {
				size_t n;

				n = sel_utf8(clip_buf, CLIP_MAX);
				if (n)
					peak_clip_set(&renderer->win, PEAK_CLIP_CLIPBOARD, clip_buf, n);
				break;
			}
			if (mux_chord(clip_paste_key, clip_paste_mod, key, mod)) {
				clip_paste(PEAK_CLIP_CLIPBOARD);
				break;
			}
			if (key == PEAK_KEY_INSERT && (mod & PEAK_KEYMOD_SHIFT)) {
				clip_paste(PEAK_CLIP_CLIPBOARD);
				break;
			}
			if (key == PEAK_KEY_INSERT) {
				pane_write(multiplexor.pane, "\033[2~", 4);
				break;
			}
			if (key == PEAK_KEY_TAB && (mod & PEAK_KEYMOD_SHIFT)) {
				pane_write(multiplexor.pane, "\033[Z", 3);
				break;
			}
			if ((u32)key < LEN(seq) && seq[key].n) {
				pane_write(multiplexor.pane, seq[key].s, seq[key].n);
				break;
			}

			if ((mod & PEAK_KEYMOD_CTRL) && !(mod & PEAK_KEYMOD_SHIFT)) {
				if (key >= PEAK_KEY_A && key <= PEAK_KEY_Z) {
					ch = (char)(1 + (key - PEAK_KEY_A));
					pane_write(multiplexor.pane, &ch, 1);
					break;
				}
				if (code >= 1 && code < 32) {
					ch = (char)code;
					pane_write(multiplexor.pane, &ch, 1);
					break;
				}
			}

			if (code >= 32 && code < 127) {
				ch = (char)code;
				pane_write(multiplexor.pane, &ch, 1);
				break;
			}
			if (code >= 128)
				break;

			if (key >= PEAK_KEY_0 && key <= PEAK_KEY_9) {
				ch = (char)('0' + (key - PEAK_KEY_0));
				pane_write(multiplexor.pane, &ch, 1);
				break;
			}
			if (key >= PEAK_KEY_A && key <= PEAK_KEY_Z) {
				ch = (char)('a' + (key - PEAK_KEY_A));
				if (mod & (PEAK_KEYMOD_SHIFT | PEAK_KEYMOD_CAPS))
					ch = (char)(ch - 32);
				pane_write(multiplexor.pane, &ch, 1);
			}
			break;
		}
		case PEAK_EVENT_TEXT: {
			size_t n;

			n = 0;
			if (!peak_text_take(&renderer->win, clip_buf, CLIP_MAX, &n) || n < 2)
				break;
			pane_write(multiplexor.pane, clip_buf, n);
			break;
		}
		case PEAK_EVENT_CLIP:
			clip_take_write();
			break;
		case PEAK_EVENT_DROP:
			drop_write();
			break;
		case PEAK_EVENT_POINTER: {
			u32 cx;
			u32 cy;
			int btn;
			PeakKeyMod pmod;

			cell_at(event.pointer.x, event.pointer.y, &cx, &cy);
			pmod = event.pointer.mod;
			if (event.pointer.type == PEAK_POINTER_MIDDLE
					&& mux_pointer(&multiplexor, cx, cy, event.pointer.state, pmod, renderer)) {
				*dirty = true;
				if (multiplexor.click_paste) {
					multiplexor.click_paste = 0;
					clip_paste(PEAK_CLIP_PRIMARY);
				}
				break;
			}
			{
				u32 lx;
				u32 ly;
				int hit;
				int focus_changed;

				focus_changed = 0;
				hit = mux_pick_near(&multiplexor, cx, cy, &lx, &ly);
				if (hit >= 0) {
					if (event.pointer.state == PEAK_POINTER_PRESSED) {
						focus_changed = hit != (int)multiplexor.focus;
						mux_focus(&multiplexor, (u32)hit);
					}
					if (hit == (int)multiplexor.focus) {
						cx = lx;
						cy = ly;
					}
				}
				if (focus_changed) {
					sel_on = 0;
					sel_drag = 0;
					*dirty = true;
				}
				if (focus_changed && event.pointer.type == PEAK_POINTER_LEFT
						&& !((multiplexor.pane->term.mode & TERM_MODE_MOUSE) && !(pmod & PEAK_KEYMOD_SHIFT))) {
					break;
				}
			}
			if (event.pointer.type == PEAK_POINTER_WHEEL_UP
					|| event.pointer.type == PEAK_POINTER_WHEEL_DOWN) {
				if (event.pointer.state == PEAK_POINTER_PRESSED) {
					if (multiplexor.pane->term.mode & TERM_MODE_MOUSE)
						mouse_report(event.pointer.type == PEAK_POINTER_WHEEL_UP ? 64 : 65,
							cx, cy, 0, pmod);
					else if (multiplexor.pane->term.mode & TERM_MODE_ALTSCREEN)
						pane_write(multiplexor.pane, event.pointer.type == PEAK_POINTER_WHEEL_UP
								? "\033[A" : "\033[B", 3);
				}
				break;
			}
			btn = event.pointer.type == PEAK_POINTER_RIGHT ? 2 :
				event.pointer.type == PEAK_POINTER_MIDDLE ? 1 : 0;
			if ((multiplexor.pane->term.mode & TERM_MODE_MOUSE) && !(pmod & PEAK_KEYMOD_SHIFT)) {
				if (event.pointer.state == PEAK_POINTER_PRESSED) {
					mouse_btn = btn;
					mouse_report(btn, cx, cy, 0, pmod);
				} else if (event.pointer.state == PEAK_POINTER_RELEASED) {
					mouse_report(mouse_btn >= 0 ? mouse_btn : btn, cx, cy, 1, pmod);
					mouse_btn = -1;
				} else if (event.pointer.state == PEAK_POINTER_MOVED) {
					int motion;

					motion = 0;
					if (multiplexor.pane->term.mode & TERM_MODE_MOUSEMANY)
						motion = 1;
					else if ((multiplexor.pane->term.mode & TERM_MODE_MOUSEMOT) && mouse_btn >= 0)
						motion = 1;
					if (motion)
						mouse_report((mouse_btn >= 0 ? mouse_btn : 3) + 32,
							cx, cy, 0, pmod);
				}
				break;
			}
			if (event.pointer.type == PEAK_POINTER_MIDDLE
					&& event.pointer.state == PEAK_POINTER_PRESSED) {
				clip_paste(PEAK_CLIP_PRIMARY);
				break;
			}
			if (event.pointer.type == PEAK_POINTER_LEFT) {
				if (event.pointer.state == PEAK_POINTER_PRESSED) {
					sel_on = 1;
					sel_drag = 1;
					sel_ax = sel_bx = cx;
					sel_ay = sel_by = cy;
					*dirty = true;
				} else if (event.pointer.state == PEAK_POINTER_MOVED && sel_drag) {
					sel_bx = cx;
					sel_by = cy;
					*dirty = true;
				} else if (event.pointer.state == PEAK_POINTER_RELEASED && sel_drag) {
					sel_drag = 0;
					sel_bx = cx;
					sel_by = cy;
					sel_to_primary();
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

void
present(void)
{
	u32 i;
	u32 bg;

	if (!renderer)
		return;
	renderer->mux_cols = multiplexor.cols;
	renderer->sel_on = sel_on;
	if (sel_on) {
		u32 a;
		u32 b;

		a = sel_ay * multiplexor.cols + sel_ax;
		b = sel_by * multiplexor.cols + sel_bx;
		if (a > b) {
			u32 t;

			t = a;
			a = b;
			b = t;
		}
		renderer->sel0 = a;
		renderer->sel1 = b;
	} else {
		renderer->sel0 = 0;
		renderer->sel1 = 0;
	}
	bg = multiplexor.mux_colors.bg[multiplexor.mux_colors.bg_default < 8
		? multiplexor.mux_colors.bg_default : 0];
	if (multiplexor.drag >= 0 && multiplexor.hover >= 0) {
		renderer->drop_hover = multiplexor.hover;
		renderer->drop_dir = multiplexor.drop_dir;
		renderer->drop_first = multiplexor.drop_first;
	} else {
		renderer->drop_hover = -1;
		renderer->drop_dir = 0;
		renderer->drop_first = 0;
	}
	if (!renderer_instance_resize(renderer, multiplexor.cols, multiplexor.rows))
		return;
	{
		VtInstance *inst;
		u32 n;

		inst = rend_buffer_mapped(&renderer->instance);
		if (!inst)
			return;
		if (renderer->tile)
			memset(inst, 0, (size_t)renderer->instance_cap * sizeof *inst);
		n = 0;
		for (i = 0; i < PANE_MAX; i++) {
			Pane *p;

			if (!multiplexor.panes[i].used)
				continue;
			p = &multiplexor.panes[i];
			n = renderer_fill_term(renderer, &p->term, p->x, p->y, inst, n, renderer->instance_cap, i == multiplexor.focus);
		}
		n = mux_fill_walls(&multiplexor, renderer, inst, n, renderer->instance_cap);
		n = mux_fill_drop(&multiplexor, renderer, inst, n, renderer->instance_cap);
		if (!renderer_frame_begin(renderer, bg))
			return;
		renderer_flush(renderer, n);
		renderer_frame_end(renderer);
	}
}

int
main_windowed(int argc, char **argv)
{
	RendererParams params;
	u32 cols;
	u32 rows;
	int timeout;

	(void)argc;
	(void)argv;
	memset(&params, 0, sizeof params);
	params.atlas_cols = 32;
	params.atlas_rows = 32;
	renderer = renderer_create(params);
	if (!renderer) {
		VTFATAL("Failed to initalize renderer!");
		VTASSERT(0, "Failed to initalize renderer!");
		return 1;
	}
	events(&redraw);
	if (!running) {
		renderer_destroy(renderer);
		return 0;
	}
	cols = renderer->atlas.cell_width ? renderer->win.width / renderer->atlas.cell_width : 80;
	rows = renderer->atlas.cell_height ? renderer->win.height / renderer->atlas.cell_height : 24;
	if (cols < 2)
		cols = 2;
	if (rows < 2)
		rows = 2;
	if (!init(cols, rows)) {
		VTFATAL("Failed to initalize terminal!");
		VTASSERT(0, "Failed to initalize terminal!");
		destroy();
		renderer_destroy(renderer);
		return 1;
	}
	while (running) {
        /* NOTE(vasco):
         * im too fast for vsync
         */
		timeout = io_timeout_ms();
		wait_io(timeout);
		events(&redraw);
		ctl_pump();
		ingest();
		if (redraw) {
			u64 fns;
			u64 now;

			fns = frame_period_ns();
			now = peak_get_time();
			if (vsync || !fns || now - consume_ns >= fns) {
				drain();
				consume_ns = now;
				present();
				redraw = false;
			}
		}
	}
	destroy();
	renderer_destroy(renderer);
	VTINFO("Quit successfully!");
	return 0;
}


int
arg_mode(const char *s)
{
	return strcmp(s, "--headless") == 0 || strcmp(s, "--live") == 0;
}

int
main_headless(int argc, char **argv)
{
	const char *path;
	const char *shot;
	FILE *in;
	Pane *pane;
	char *end;
	u32 cols;
	u32 rows;
	int dump;
	int i;
	unsigned long v;

	path = NULL;
	shot = NULL;
	cols = 80;
	rows = 24;
	dump = 0;
	for (i = 1; i < argc; i++) {
		if (arg_mode(argv[i]))
			continue;
		if (strcmp(argv[i], "--screenshot") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "vt: --screenshot needs a path\n");
				return 1;
			}
			shot = argv[++i];
		} else if (strcmp(argv[i], "--dump-runs") == 0) {
			dump = 1;
		} else if (strcmp(argv[i], "--cols") == 0 || strcmp(argv[i], "--rows") == 0) {
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
		}
	}
	if (!init_term(cols, rows))
		return 1;
	in = path ? fopen(path, "rb") : stdin;
	if (!in) {
		fprintf(stderr, "vt: cannot open %s\n", path);
		destroy();
		return 1;
	}
	pane = multiplexor.pane;
	peak_stdout_silence();
	for (;;) {
		size_t n;

		n = fread(pane->rb->base + (pane->rb->write % pane->rb->size), 1, pane->rb->size, in);
		if (n == 0)
			break;
		ringbuffer_produce(pane->rb, n);
		feed_bytes += n;
	}
	if (path)
		fclose(in);
	peak_stdout_restore();
	if (dump) {
		ringbuffer_consume(pane->rb);
		ringbuffer_runs_from_last_n_lines(pane->rb, pane->rb->line_n);
		dump_runs(stdout);
	} else {
		pane_drain(pane);
		dump_screen(stdout);
	}
	if (shot) {
		RendererParams params;

		memset(&params, 0, sizeof params);
		params.atlas_cols = 32;
		params.atlas_rows = 32;
		params.offscreen = 1;
		renderer = renderer_create(params);
		if (!renderer) {
			fprintf(stderr, "vt: screenshot renderer failed\n");
			destroy();
			return 1;
		}
		renderer_screenshot_term_ppm(renderer, &pane->term, shot);
		renderer_destroy(renderer);
		renderer = 0;
	}
	destroy();
	return 0;
}

int
main_live(int argc, char **argv)
{
	char *end;
	u32 cols;
	u32 rows;
	int i;
	unsigned long v;

	cols = 80;
	rows = 24;
	for (i = 1; i < argc; i++) {
		if (arg_mode(argv[i]))
			continue;
		if (strcmp(argv[i], "--cols") == 0 || strcmp(argv[i], "--rows") == 0) {
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
		} else {
			fprintf(stderr, "vt: extra arg %s\n", argv[i]);
			return 1;
		}
	}
	if (!init(cols, rows)) {
		fprintf(stderr, "vt: init failed\n");
		destroy();
		return 1;
	}
	{
		RendererParams params;

		memset(&params, 0, sizeof params);
		params.atlas_cols = 32;
		params.atlas_rows = 32;
		params.offscreen = 1;
		renderer = renderer_create(params);
	}
	while (running) {
		wait_io(io_timeout_ms());
		ctl_pump();
		ingest();
		if (redraw) {
			u64 fns;
			u64 now;

			fns = frame_period_ns();
			now = peak_get_time();
			if (!fns || now - consume_ns >= fns) {
				drain();
				consume_ns = now;
				redraw = false;
			}
		}
	}
	destroy();
	if (renderer)
		renderer_destroy(renderer);
	return 0;
}

int
main(int argc, char **argv)
{
	int i;
	int live;
	int headless;

	live = 0;
	headless = 0;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--live") == 0)
			live = 1;
		else if (strcmp(argv[i], "--headless") == 0)
			headless = 1;
	}
	if (headless)
		return main_headless(argc, argv);
	if (live)
		return main_live(argc, argv);
	return main_windowed(argc, argv);
}
