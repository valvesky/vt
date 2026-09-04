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
#include "rend.h"
#endif
#include "peak.c"
#ifndef VT_HEADLESS
#include "rend.c"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "lib/stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#include "lib/stb_image.h"

#include <assert.h>
#include <immintrin.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vt_multiplexing.h"
#include "config.h"
#include "vt_shell.h"
#include "vt_shell.c"
#include "vt_debug.h"
#include "vt_term.c"
#include "vt_ring_buffer.c"

#include "vt_glyth_cache.h"
#include "vt_glyth_cache.c"

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
static void vt_drain(void);
static int vt_ingest(void);
static void vt_events(bool *dirty);
static void vt_present(void);

static void vt_wait(int timeout_ms);
static int vt_hex_digit(int c);
static int vt_parse_hex6(const char *p, const char *end, uint32_t *out);
static int vt_theme_parse(const char *buf, unsigned long n, TermColors *c);
static int vt_theme_load(const char *str);
static void vt_theme_apply(void);
static void vt_theme_poll(void);
static size_t vt_base64_decode(const char *s, size_t n, char *dst, size_t cap);
static int vt_osc52(const char *p, u32 n);

static void vt_clip_paste(PeakClip which);
static void vt_clip_take_write(void);
static void vt_drop_write(void);
static size_t vt_sel_utf8(char *dst, size_t cap);
static void vt_sel_to_primary(void);
static void vt_cell_at(float px, float py, u32 *x, u32 *y);
static void vt_mouse_report(int btn, u32 x, u32 y, int release, PeakKeyMod mod);


static bool running = true;
static bool redraw = true;
static u64 vt_feed_bytes;
static u64 vt_feed_consume;
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


static void vt_shell_gone(VtPane *p);
static size_t vt_pane_write(VtPane *p, const char *const src, size_t len);
static int vt_pane_ingest(VtPane *p);
static void vt_pane_drain(VtPane *p);
#include "vt_multiplexing.c"

static VtMultiplexor vt_multiplexor;

#include "vt_kitty.c"

#include "vt_ctl.h"
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

	vt_multiplexor.mux_colors = vt_colors;
	for (i = 0; i < VT_PANE_MAX; i++) {
		if (vt_multiplexor.panes[i].used)
			term_colors_set(&vt_multiplexor.panes[i].term, &vt_colors);
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

	vt_mux_reset(&vt_multiplexor);
	if (!vt_mux_open(&vt_multiplexor, 0, cols, rows, &vt_colors)) {
		VTFATAL("vt_ring_init");
		VTASSERT(0, "vt_init_term");
		return false;
	}
	vt_mux_bind(&vt_multiplexor, 0);
	vt_mux_layout(&vt_multiplexor, cols, rows);
#ifndef VT_HEADLESS
	if (!renderer_instance_make(&renderer.instance, cols, rows)) {
		VTASSERT(0, "renderer_instance_make");
		vt_mux_close(&vt_multiplexor, 0);
		return false;
	}
#endif
	return true;
}

bool
vt_init(u32 cols, u32 rows)
{
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

	proc = vt_shell_spawn(cols, rows, cols * atlas.cell_width, rows * atlas.cell_height);

	if (proc.fd == PEAK_HANDLE_INVALID) {
		VTFATAL("Could not open tty.");
		VTASSERT(0, "Could not open tty.");
		return false;
	}
	vt_multiplexor.vt_pane->sh = proc;
	vt_shell_setup_term(&vt_multiplexor.vt_pane->term);
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
			"ingest %llu bytes  read %llu MiB/s  parse %llu MiB/s  total %llu MiB/s  consume %llu\n",
			(unsigned long long)vt_feed_bytes,
			(unsigned long long)read_mib,
			(unsigned long long)parse_mib,
			(unsigned long long)total_mib,
			(unsigned long long)vt_feed_consume);
		if (vt_feed_read_ns && vt_feed_bytes > 1024ull * 1024ull && read_mib <= 200)
			VTWARN("pty read %llu MiB/s (slow: <= 200)", (unsigned long long)read_mib);
	}
	vt_ctl_destroy();
	vt_mux_destroy(&vt_multiplexor);
}

void
vt_shell_gone(VtPane *p)
{
	vt_reap_children();
	if (!p || !p->used)
		return;
	if (p->sh.pid > 0)
		vt_mux_kill(&vt_multiplexor, (u32)(p - vt_multiplexor.panes));
	else
		running = false;
}

int
vt_pane_ingest(VtPane *p)
{
	/* NOTE(vasco): Receive until EAGAIN/EOF. Feed keeps the tail. */
	int r;
	int any;
	u64 t0;
	u64 t_data;

    /* pane may have diead */
	if (!p->rb) return 0;


	any = 0;
	t_data = 0;
	for (;;) {
		t0 = peak_get_time();
		r = vt_shell_read(&p->sh, p->rb->base + (p->rb->write % p->rb->size), p->rb->size);
		if (r > 0) {
			vt_feed_read_ns += peak_get_time() - t0;
			vt_ringbuffer_produce(p->rb, (size_t)r);
			vt_feed_bytes += (u64)r;
			any = 1;
			t_data = peak_get_time();
			continue;
		}
		if (r == 0) {
			vt_shell_gone(p);
			return any;
		}
		if (!any)
			return 0;
		if (peak_get_time() - t_data >= 16ull * 1000000ull)
			return any;
	}

}

void
vt_pane_drain(VtPane *pane)
{
	VtRingBuffer *rb;
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

		vt_ringbuffer_consume(rb);
		vt_feed_consume++;
		if (rb->run_n) {
			fed = 1;
			VTASSERT(rb->base && rb->size);
			for (i = 0; i < rb->run_n; i++) {
				const char *p;
				u32 n;
				VtRunType type;

				if (!rb->run[i].n)
					continue;

				p = rb->base + (rb->run[i].off % rb->size);
				n = (u32)rb->run[i].n;

				type = rb->run[i].type;

				if (type == VT_RUN_KITTY) {
					vt_kitty(pane, p, n);
				} else if (!(type == VT_RUN_ESCAPE && vt_osc52(p, n))) {
					VTASSERT((u32)type < LEN(vt_run_feed));
					vt_run_feed[type](&pane->term, p, n);
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
	int any;

	any = 0;
	for (i = 0; i < VT_PANE_MAX; i++) {
		VtPane *p;

		p = &vt_multiplexor.panes[i];
		if (!p->used)
			continue;
		if (p->sh.fd == PEAK_HANDLE_INVALID)
			continue; // cleaned up later
		any |= vt_pane_ingest(p);
		if (!p->used || !p->rb)
			continue;
		if (p->rb->write != p->rb->read)
			redraw = true;
	}

	if (running && vt_multiplexor.panes[vt_multiplexor.focus].used)
		vt_mux_bind(&vt_multiplexor, vt_multiplexor.focus);
	else if (running)
		vt_mux_bind(&vt_multiplexor, vt_mux_first(&vt_multiplexor));
	return any;
}

void
vt_drain(void)
{
	u32 i;

	for (i = 0; i < VT_PANE_MAX; i++) {
		VtPane *p;

		p = &vt_multiplexor.panes[i];
		if (!p->used || !p->rb)
			continue;
		if (p->rb->write != p->rb->read)
			vt_pane_drain(p);
	}
	if (running && vt_multiplexor.panes[vt_multiplexor.focus].used)
		vt_mux_bind(&vt_multiplexor, vt_multiplexor.focus);
	else if (running)
		vt_mux_bind(&vt_multiplexor, vt_mux_first(&vt_multiplexor));
}

size_t
vt_pane_write(VtPane *p, const char *const src, size_t len)
{
	int r;

	if (!p || p->sh.fd == PEAK_HANDLE_INVALID)
		return 0;
	r = vt_shell_write(&p->sh, src, len);
	if (r <= 0) {
		if (r == 0)
			vt_shell_gone(p);
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
	if (vt_multiplexor.vt_pane->term.mode & TERM_MODE_BRKTPASTE)
		vt_pane_write(vt_multiplexor.vt_pane, "\033[200~", 6);
	vt_pane_write(vt_multiplexor.vt_pane, vt_clip_buf, o);
	if (vt_multiplexor.vt_pane->term.mode & TERM_MODE_BRKTPASTE)
		vt_pane_write(vt_multiplexor.vt_pane, "\033[201~", 6);
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
				vt_mux_drop_self(&vt_multiplexor);
				return;
			}
		}
		if (vt_mux_offer_take(&vt_multiplexor))
			return;
		if (vt_mux_pull(&vt_multiplexor, vt_clip_buf, -1))
			return;
	}
	if (vt_multiplexor.vt_pane->term.mode & TERM_MODE_BRKTPASTE)
		vt_pane_write(vt_multiplexor.vt_pane, "\033[200~", 6);
	vt_pane_write(vt_multiplexor.vt_pane, "'", 1);
	i = 0;
	while (i < n) {
		size_t s;

		s = i;
		while (i < n && vt_clip_buf[i] != '\'')
			i++;
		if (i > s)
			vt_pane_write(vt_multiplexor.vt_pane, vt_clip_buf + s, i - s);
		if (i < n && vt_clip_buf[i] == '\'') {
			vt_pane_write(vt_multiplexor.vt_pane, "'\\''", 4);
			i++;
		}
	}
	vt_pane_write(vt_multiplexor.vt_pane, "'", 1);
	if (vt_multiplexor.vt_pane->term.mode & TERM_MODE_BRKTPASTE)
		vt_pane_write(vt_multiplexor.vt_pane, "\033[201~", 6);
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
	s = term_screen(&vt_multiplexor.vt_pane->term);
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
	if (vt_multiplexor.vt_pane->term.mode & TERM_MODE_MOUSESGR) {
		n = snprintf(buf, sizeof buf, "\033[<%d;%u;%u%c", b, x, y, release ? 'm' : 'M');
		if (n > 0)
			vt_pane_write(vt_multiplexor.vt_pane, buf, (size_t)n);
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
	vt_pane_write(vt_multiplexor.vt_pane, buf, 6);
}
#endif

void
vt_wait(int timeout_ms)
{
	PEAK_HANDLE fds[VT_PANE_MAX + VT_CTL_CLIENTS + 5];
	u32 n;
	PEAK_HANDLE usr;

	n = 0;
	n += vt_mux_fds(&vt_multiplexor, fds);
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
#ifdef DEBUG
	u64 t0;
	u64 dt;

	t0 = peak_get_time();
#endif
	vt_mux_present(&vt_multiplexor);
#ifdef DEBUG
	dt = peak_get_time() - t0;
	VTDEBUG("present %llu ns", (unsigned long long)dt);
	vt_stage_add(VT_STAGE_PRESENT, dt);
#endif
}
#endif

void
vt_dump_runs(FILE *out)
{
	VtRingBuffer *rb;
	u32 i;

	if (!vt_multiplexor.vt_pane || !vt_multiplexor.vt_pane->rb)
		return;
	rb = vt_multiplexor.vt_pane->rb;
	for (i = 0; i < rb->run_n; i++) {
		VTASSERT((u32)rb->run[i].type < LEN(vt_ring_buffer_run_name));
		fprintf(out, "%s %u\n", vt_ring_buffer_run_name[rb->run[i].type], (u32)rb->run[i].n);
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

	s = term_screen(&vt_multiplexor.vt_pane->term);
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

	s = term_screen(&vt_multiplexor.vt_pane->term);
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
            vt_mux_resize(&vt_multiplexor, cols, rows);
			*dirty = true;
			break;
		case PEAK_EVENT_KEY_DOWN: {
			key = event.key.key;
			mod = event.key.mod;
			code = event.key.code;

			if (vt_mux_key(&vt_multiplexor, key, mod, code))
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
				vt_pane_write(vt_multiplexor.vt_pane, "\033[2~", 4);
				break;
			}
			if (key == PEAK_KEY_TAB && (mod & PEAK_KEYMOD_SHIFT)) {
				vt_pane_write(vt_multiplexor.vt_pane, "\033[Z", 3);
				break;
			}
			if ((u32)key < LEN(seq) && seq[key].n) {
				vt_pane_write(vt_multiplexor.vt_pane, seq[key].s, seq[key].n);
				break;
			}

			if ((mod & PEAK_KEYMOD_CTRL) && !(mod & PEAK_KEYMOD_SHIFT)) {
				if (key >= PEAK_KEY_A && key <= PEAK_KEY_Z) {
					ch = (char)(1 + (key - PEAK_KEY_A));
					vt_pane_write(vt_multiplexor.vt_pane, &ch, 1);
					break;
				}
				if (code >= 1 && code < 32) {
					ch = (char)code;
					vt_pane_write(vt_multiplexor.vt_pane, &ch, 1);
					break;
				}
			}

			if (code >= 32 && code < 127) {
				ch = (char)code;
				vt_pane_write(vt_multiplexor.vt_pane, &ch, 1);
				break;
			}
			if (code >= 128)
				break;

			if (key >= PEAK_KEY_0 && key <= PEAK_KEY_9) {
				ch = (char)('0' + (key - PEAK_KEY_0));
				vt_pane_write(vt_multiplexor.vt_pane, &ch, 1);
				break;
			}
			if (key >= PEAK_KEY_A && key <= PEAK_KEY_Z) {
				ch = (char)('a' + (key - PEAK_KEY_A));
				if (mod & (PEAK_KEYMOD_SHIFT | PEAK_KEYMOD_CAPS))
					ch = (char)(ch - 32);
				vt_pane_write(vt_multiplexor.vt_pane, &ch, 1);
			}
			break;
		}
		case PEAK_EVENT_TEXT: {
			size_t n;

			n = 0;
			if (!peak_text_take(NULL, vt_clip_buf, VT_CLIP_MAX, &n) || n < 2)
				break;
			vt_pane_write(vt_multiplexor.vt_pane, vt_clip_buf, n);
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
					&& vt_mux_pointer(&vt_multiplexor, cx, cy, event.pointer.state, pmod)) {
				*dirty = true;
				if (vt_multiplexor.click_paste) {
					vt_multiplexor.click_paste = 0;
					vt_clip_paste(PEAK_CLIP_PRIMARY);
				}
				break;
			}
			{
				u32 lx;
				u32 ly;
				int hit;

				hit = vt_mux_pick(&vt_multiplexor, cx, cy, &lx, &ly);
				if (hit >= 0) {
					if (event.pointer.state == PEAK_POINTER_PRESSED)
						vt_mux_focus(&vt_multiplexor, (u32)hit);
					if (hit == (int)vt_multiplexor.focus) {
						cx = lx;
						cy = ly;
					}
				}
			}
			if (event.pointer.type == PEAK_POINTER_WHEEL_UP
					|| event.pointer.type == PEAK_POINTER_WHEEL_DOWN) {
				if (event.pointer.state == PEAK_POINTER_PRESSED) {
					if (vt_multiplexor.vt_pane->term.mode & TERM_MODE_MOUSE)
						vt_mouse_report(event.pointer.type == PEAK_POINTER_WHEEL_UP ? 64 : 65,
							cx, cy, 0, pmod);
					else if (vt_multiplexor.vt_pane->term.mode & TERM_MODE_ALTSCREEN)
						vt_pane_write(vt_multiplexor.vt_pane, event.pointer.type == PEAK_POINTER_WHEEL_UP
								? "\033[A" : "\033[B", 3);
				}
				break;
			}
			btn = event.pointer.type == PEAK_POINTER_RIGHT ? 2 :
				event.pointer.type == PEAK_POINTER_MIDDLE ? 1 : 0;
			if ((vt_multiplexor.vt_pane->term.mode & TERM_MODE_MOUSE) && !(pmod & PEAK_KEYMOD_SHIFT)) {
				if (event.pointer.state == PEAK_POINTER_PRESSED) {
					vt_mouse_btn = btn;
					vt_mouse_report(btn, cx, cy, 0, pmod);
				} else if (event.pointer.state == PEAK_POINTER_RELEASED) {
					vt_mouse_report(vt_mouse_btn >= 0 ? vt_mouse_btn : btn, cx, cy, 1, pmod);
					vt_mouse_btn = -1;
				} else if (event.pointer.state == PEAK_POINTER_MOVED) {
					int motion;

					motion = 0;
					if (vt_multiplexor.vt_pane->term.mode & TERM_MODE_MOUSEMANY)
						motion = 1;
					else if ((vt_multiplexor.vt_pane->term.mode & TERM_MODE_MOUSEMOT) && vt_mouse_btn >= 0)
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

int
main(int argc, char **argv)
{
	u32 cols, rows;
	int timeout;

	(void)argc;
	(void)argv;

	if (!renderer_init()) {
		VTFATAL("Failed to initalize renderer!");
		VTASSERT(0, "Failed to initalize renderer!");
		renderer_destroy();
		return 1;
	}

	vt_events(&redraw);
	if (!running) {
		renderer_destroy();
		return 0;
	}
	renderer_get_grid(&renderer, &cols, &rows);
	if (!vt_init(cols, rows)) {
		VTFATAL("Failed to initalize terminal!");
		VTASSERT(0, "Failed to initalize terminal!");
		vt_destroy();
		renderer_destroy();
		return 1;
	}

	while (running) {
        /* NOTE(vasco):
         * im too fast for vsync
         */
		timeout = redraw ? 0 : -1;
		vt_wait(timeout);
		vt_events(&redraw);
		vt_ctl_pump();
		while (vt_ingest())
			redraw = true;
		if (redraw) {
			vt_drain();
			vt_present();
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
}

#else
#ifndef VT_LIVE

int
main(int argc, char **argv)
{
	const char *path;
	const char *shot;
	FILE *in;
	TermScreen *scr;
	VtPane *pane;
	char *end;
	u32 cols;
	u32 rows;
	int dump_runs;
	int rc;
	int i;
	unsigned long v;

	path = NULL;
	shot = NULL;
	cols = 80;
	rows = 24;
	dump_runs = 0;
	rc = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--screenshot") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "vt-headless: --screenshot needs a path\n");
				return 1;
			}
			shot = argv[++i];
		} else if (strcmp(argv[i], "--dump-runs") == 0) {
			dump_runs = 1;
		} else if (strcmp(argv[i], "--cols") == 0
				|| strcmp(argv[i], "--rows") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "vt-headless: %s needs a number\n", argv[i]);
				return 1;
			}
			v = strtoul(argv[i + 1], &end, 10);
			if (!argv[i + 1][0] || *end || v < 2 || v > 400) {
				fprintf(stderr, "vt-headless: bad %s\n", argv[i]);
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
			fprintf(stderr, "vt-headless: extra arg %s\n", argv[i]);
			return 1;
		}
	}

	if (!vt_init_term(cols, rows))
		return 1;

	in = path ? fopen(path, "rb") : stdin;
	if (!in) {
		fprintf(stderr, "vt-headless: cannot open %s\n", path);
		vt_destroy();
		return 1;
	}

	pane = vt_multiplexor.vt_pane;
	peak_stdout_silence();
	for (;;) {
		size_t n;

		n = fread(pane->rb->base + (pane->rb->write % pane->rb->size), 1, pane->rb->size, in);
		if (n == 0)
			break;
		vt_ringbuffer_produce(pane->rb, n);
		vt_feed_bytes += n;
	}
	if (path)
		fclose(in);
	peak_stdout_restore();
	if (dump_runs) {
		vt_ringbuffer_runs_from_last_n_lines(pane->rb, pane->rb->line_n);
		vt_dump_runs(stdout);
	} else {
		vt_pane_drain(pane);
		vt_dump_screen(stdout);
	}

	if (shot) {
		if (!glyph_table_init(font_path, (float)font_size_px)) {
			fprintf(stderr, "vt-headless: cannot load font %s\n", font_path);
			rc = 1;
		} else {
			TermStyle cs;

			scr = term_screen(&vt_multiplexor.vt_pane->term);
			cs = term_cursor_style(&vt_multiplexor.vt_pane->term);
			if (!renderer_screenshot_ppm(&vt_multiplexor.vt_pane->term, scr, vt_multiplexor.vt_pane->term.cursor.x, vt_multiplexor.vt_pane->term.cursor.y,
					cs.fg, cs.bg, shot)) {
				fprintf(stderr, "vt-headless: cannot write %s\n", shot);
				rc = 1;
			}
			glyph_table_destroy();
		}
	}

	vt_destroy();
	return rc;
}

#else

int
main(int argc, char **argv)
{
	char *end;
	u32 cols;
	u32 rows;
	int i;
	unsigned long v;

	cols = 80;
	rows = 24;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--cols") == 0
				|| strcmp(argv[i], "--rows") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "vt-live: %s needs a number\n", argv[i]);
				return 1;
			}
			v = strtoul(argv[i + 1], &end, 10);
			if (!argv[i + 1][0] || *end || v < 2 || v > 400) {
				fprintf(stderr, "vt-live: bad %s\n", argv[i]);
				return 1;
			}
			if (argv[i][2] == 'c')
				cols = (u32)v;
			else
				rows = (u32)v;
			i++;
		} else {
			fprintf(stderr, "vt-live: extra arg %s\n", argv[i]);
			return 1;
		}
	}

	if (!glyph_table_init(font_path, (float)font_size_px)) {
		fprintf(stderr, "vt-live: cannot load font %s\n", font_path);
		return 1;
	}
	if (!vt_init(cols, rows)) {
		fprintf(stderr, "vt-live: init failed\n");
		glyph_table_destroy();
		vt_destroy();
		return 1;
	}
	while (running) {
		vt_wait(-1);
		vt_ctl_pump();
		while (vt_ingest())
			;
		vt_drain();
	}
	glyph_table_destroy();
	vt_destroy();
	return 0;
}
#endif
#endif
