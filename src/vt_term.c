#pragma once
#include "vt_term.h"

#define TERM_BEL  0x07
#define TERM_BS   0x08
#define TERM_ESC  0x1B
#define TERM_CAN  0x18
#define TERM_SUB  0x1A
#define TERM_DEL  0x7F

#define TERM_IS_C0(c)  (TERM_BETWEEN((c), 0, 0x1f) || (c) == TERM_DEL)
#define TERM_IS_C1(c)  TERM_BETWEEN((c), 0x80, 0x9f)
#define TERM_STYLE_MAX 65536u

static void term_screen_init(TermScreen *s, uint32_t cols, uint32_t rows);
static void term_screen_free(TermScreen *s);
static int  term_screen_grow(TermScreen *s, uint32_t cols, uint32_t rows);
static TermScreen *term_live(Term *t);
static void term_next_line(Term *t);
static void term_index(Term *t);
static void term_reverse_index(Term *t);
static void term_lf(Term *t);
static void term_cursor_up(Term *t, uint32_t n);
static void term_cursor_down(Term *t, uint32_t n);
static void term_cursor_forward(Term *t);
static void term_decaln(Term *t);
static void term_reset(Term *t);
static void term_clear_screen(Term *t, TermScreen *s);
static uint32_t term_acs_map(uint32_t c);
static int  term_codepoint_width(uint32_t c);
static int  term_utf8_consume(Term *t, unsigned char ch, uint32_t *out);
static void term_putc(Term *t, uint32_t c);
static void term_handle_c0(Term *t, unsigned char code);
static void term_handle_c1(Term *t, unsigned char code);
static int  term_handle_esc(Term *t, unsigned char ascii);
static void term_char_feed(Term *t, unsigned char ch);
static void term_cursor_sgr(Term *t, const int32_t *attr, int32_t n);
static int  term_parse_csi(Term *t);
static void term_handle_csi(Term *t);
static void term_handle_str(Term *t);
static void term_insert_blank(Term *t, uint32_t n);
static void term_delete_chars(Term *t, uint32_t n);
static void term_erase_chars(Term *t, uint32_t n);
static void term_scroll(Term *t, uint32_t y0, uint32_t y1, int n);
static void term_alt(Term *t, int on, int save_cur);
static void term_reset_margins(Term *t);
static void term_set_mode(Term *t, int set);
static void term_reply_str(Term *t, const char *s);
static uint32_t term_color_256(Term *t, int n);
static void term_clear_region(Term *t, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
static int  term_init_common(Term *t, uint32_t cols, uint32_t rows, const TermColors *colors);
static void term_hist_resize(Term *t, uint32_t cols);
static void term_screen_adopt(TermScreen *s, TermCell *cells, uint32_t cols, uint32_t rows, uint32_t cap);
static void term_screen_copy_on(TermScreen *s, TermCell *dst, uint32_t cols, uint32_t rows, uint32_t cap);
static void term_move_to(Term *t, uint32_t x, uint32_t y);
static void term_move_abs(Term *t, uint32_t x, uint32_t y);
static void term_colors_default(TermColors *c);
static int  term_style_init(Term *t);
static uint16_t term_style_intern(Term *t);
static void term_cell_put(Term *t, TermCell *c, uint32_t cp);
static int  term_style_match(const TermStyle *a, const TermStyle *b);
static TermStyle term_style_from_cursor(const Term *t);
static uint32_t term_style_rgb(const Term *t, uint8_t kind, uint8_t idx, uint32_t rgb, int fg);
static TermStyle term_style_resolve(const Term *t, TermStyle s);

static const TermColors term_colors_stock = {
    .fg = {
        0x1d2021, 0xea6962, 0xa9b665, 0xd8a657,
        0x7daea3, 0xd3869b, 0x89b482, 0xd4be98,
        0x928374, 0xef938e, 0xbbc585, 0xe1bb7e,
        0x9dc2ba, 0xe1acbb, 0xa7c7a2, 0xe2d3ba,
    },
    .bg = {
        0x1D2021, 0x800000, 0x008000, 0x808000,
        0x000080, 0x800080, 0x008080, 0xC8C8C8,
    },
    .fg_default = 7,
    .bg_default = 0,
};

static void
term_colors_default(TermColors *c)
{
    *c = term_colors_stock;
}

static int
term_style_init(Term *t)
{
    t->styles = calloc(TERM_STYLE_MAX, sizeof *t->styles);
    if (!t->styles)
        return 0;
    t->style_cap = TERM_STYLE_MAX;
    t->style_n = 1;
    return 1;
}

static int
term_style_match(const TermStyle *a, const TermStyle *b)
{
    return a->fg == b->fg && a->bg == b->bg
        && a->fg_kind == b->fg_kind && a->bg_kind == b->bg_kind
        && a->fg_idx == b->fg_idx && a->bg_idx == b->bg_idx
        && a->attr == b->attr;
}

static TermStyle
term_style_from_cursor(const Term *t)
{
    TermStyle s;

    memset(&s, 0, sizeof s);
    s.fg = t->cursor.fg;
    s.bg = t->cursor.bg;
    s.fg_kind = t->cursor.fg_kind;
    s.bg_kind = t->cursor.bg_kind;
    s.fg_idx = t->cursor.fg_idx;
    s.bg_idx = t->cursor.bg_idx;
    s.attr = t->cursor.attr;
    return s;
}

static uint32_t
term_style_rgb(const Term *t, uint8_t kind, uint8_t idx, uint32_t rgb, int fg)
{
    if (kind == TERM_COLOR_RGB)
        return rgb & 0xffffffu;
    if (kind == TERM_COLOR_PAL) {
        if (fg) {
            if (idx > 15)
                idx = 15;
            return t->colors.fg[idx];
        }
        if (idx < 8)
            return t->colors.bg[idx];
        if (idx > 15)
            idx = 15;
        return t->colors.fg[idx];
    }
    if (fg)
        return t->colors.fg[t->colors.fg_default < 16 ? t->colors.fg_default : 7];
    return t->colors.bg[t->colors.bg_default < 8 ? t->colors.bg_default : 0];
}

static TermStyle
term_style_resolve(const Term *t, TermStyle s)
{
    uint32_t frgb;
    uint32_t brgb;

    frgb = term_style_rgb(t, s.fg_kind, s.fg_idx, s.fg, 1);
    brgb = term_style_rgb(t, s.bg_kind, s.bg_idx, s.bg, 0);
    s.fg = (frgb << 8) | s.attr;
    s.bg = brgb << 8;
    return s;
}

static uint16_t
term_style_intern(Term *t)
{
    TermStyle s;
    uint32_t i;
    uint16_t id;

    if (!t || !t->styles)
        return 0;
    s = term_style_from_cursor(t);
    id = t->cursor.style;
    if (id != 0 && (uint32_t)id < t->style_n && term_style_match(&t->styles[id], &s))
        return id;
    for (i = 1; i < t->style_n; i++) {
        if (term_style_match(&t->styles[i], &s)) {
            t->cursor.style = (uint16_t)i;
            return (uint16_t)i;
        }
    }
    if (t->style_n >= TERM_STYLE_MAX) {
        id = (uint16_t)(TERM_STYLE_MAX - 1);
        t->styles[id] = s;
        t->cursor.style = id;
        return id;
    }
    t->styles[t->style_n] = s;
    t->cursor.style = (uint16_t)t->style_n;
    return (uint16_t)t->style_n++;
}

static void
term_cell_put(Term *t, TermCell *c, uint32_t cp)
{
    c->codepoint = cp;
    c->style = term_style_intern(t);
    c->tag = TERM_CELL_CODE;
    c->glyph = 0;
}

TermCell *
term_row(TermScreen *s, uint32_t y)
{
    uint32_t py;

    if (!s || !s->cell_buffer || !s->rows || y >= s->rows)
        return NULL;
    py = s->origin + y;
    if (py >= s->rows)
        py -= s->rows;
    return s->cell_buffer + (size_t)py * s->cols;
}

TermCell *
term_cell_at(TermScreen *s, uint32_t x, uint32_t y)
{
    TermCell *row;

    row = term_row(s, y);
    if (!row || x >= s->cols)
        return NULL;
    return row + x;
}

static void
term_zero_logical_row(TermScreen *s, uint32_t y)
{
    TermCell *row;

    row = term_row(s, y);
    if (!row)
        return;
    memset(row, 0, (size_t)s->cols * sizeof *row);
}

static void
term_copy_logical_row(TermScreen *s, uint32_t dst_y, uint32_t src_y)
{
    TermCell *d;
    TermCell *src;

    d = term_row(s, dst_y);
    src = term_row(s, src_y);
    if (!d || !src)
        return;
    memcpy(d, src, (size_t)s->cols * sizeof *d);
}

static void
term_screen_init(TermScreen *s, uint32_t cols, uint32_t rows)
{
    memset(s, 0, sizeof *s);
    s->cell_buffer = calloc((size_t)rows * cols, sizeof *s->cell_buffer);
    s->cols = cols;
    s->rows = rows;
    s->capacity = cols * rows;
}

static void
term_screen_free(TermScreen *s)
{
    if (s->cell_buffer)
        free(s->cell_buffer);
    memset(s, 0, sizeof *s);
}

static int
term_screen_grow(TermScreen *s, uint32_t cols, uint32_t rows)
{
    TermCell *cells;
    uint32_t y;
    uint32_t copy_cols;
    uint32_t copy_rows;
    size_t need;

    if (!s || !cols || !rows)
        return 0;
    if (s->cell_buffer && s->cols == cols && s->rows == rows)
        return 1;

    need = (size_t)cols * (size_t)rows;
    cells = calloc(need, sizeof *cells);
    if (!cells)
        return 0;

    copy_cols = TERM_MIN(s->cols, cols);
    copy_rows = TERM_MIN(s->rows, rows);
    if (s->cell_buffer && copy_cols && copy_rows) {
        for (y = 0; y < copy_rows; y++)
            memcpy(cells + (size_t)y * cols,
                term_row(s, y),
                (size_t)copy_cols * sizeof *cells);
    }
    free(s->cell_buffer);
    s->cell_buffer = cells;
    s->cols = cols;
    s->rows = rows;
    s->capacity = (uint32_t)need;
    s->origin = 0;
    return 1;
}

static void
term_screen_adopt(TermScreen *s, TermCell *cells, uint32_t cols, uint32_t rows, uint32_t cap)
{
    s->cell_buffer = cells;
    s->cols = cols;
    s->rows = rows;
    s->capacity = cap;
    s->origin = 0;
}

static void
term_screen_copy_on(TermScreen *s, TermCell *dst, uint32_t cols, uint32_t rows, uint32_t cap)
{
    uint32_t y;
    uint32_t copy_cols;
    uint32_t copy_rows;
    TermCell *src;
    TermCell *tmp;

    src = s->cell_buffer;
    copy_cols = TERM_MIN(s->cols, cols);
    copy_rows = TERM_MIN(s->rows, rows);
    if (dst == src) {
        if (s->cols == cols && s->rows == rows) {
            s->capacity = cap;
            return;
        }
        tmp = NULL;
        if (src && copy_cols && copy_rows) {
            tmp = malloc((size_t)copy_rows * s->cols * sizeof *tmp);
            if (tmp) {
                for (y = 0; y < copy_rows; y++)
                    memcpy(tmp + (size_t)y * s->cols,
                        term_row(s, y),
                        (size_t)s->cols * sizeof *tmp);
            }
        }
        memset(dst, 0, (size_t)cap * sizeof *dst);
        if (tmp) {
            for (y = 0; y < copy_rows; y++)
                memcpy(dst + (size_t)y * cols,
                    tmp + (size_t)y * s->cols,
                    (size_t)copy_cols * sizeof *dst);
            free(tmp);
        }
        term_screen_adopt(s, dst, cols, rows, cap);
        return;
    }
    memset(dst, 0, (size_t)cap * sizeof *dst);
    if (src && copy_cols && copy_rows) {
        for (y = 0; y < copy_rows; y++)
            memcpy(dst + (size_t)y * cols,
                term_row(s, y),
                (size_t)copy_cols * sizeof *dst);
    }
    term_screen_adopt(s, dst, cols, rows, cap);
}

static int
term_init_common(Term *t, uint32_t cols, uint32_t rows, const TermColors *colors)
{
    memset(t, 0, sizeof *t);
    if (colors)
        t->colors = *colors;
    else
        term_colors_default(&t->colors);

    t->mode = TERM_MODE_UTF8 | TERM_MODE_WRAP;
    t->top = 0;
    t->bot = rows - 1;
    t->hist_cap = TERM_HIST_MAX;
    t->hist_cols = cols;
    t->hist = calloc((size_t)TERM_HIST_MAX * cols, sizeof *t->hist);
    if (!t->hist)
        t->hist_cap = 0;
    term_style_init(t);
    term_style_intern(t);
    t->saved = t->cursor;
    return 1;
}

static void
term_hist_resize(Term *t, uint32_t cols)
{
    TermCell *next;
    uint32_t i;
    uint32_t copy;

    if (!t->hist || t->hist_cols == cols)
        return;
    next = calloc((size_t)t->hist_cap * cols, sizeof *next);
    if (!next) {
        free(t->hist);
        t->hist = NULL;
        t->hist_cap = 0;
        t->hist_n = 0;
        t->hist_i = 0;
        t->hist_cols = 0;
        return;
    }
    copy = TERM_MIN(t->hist_cols, cols);
    for (i = 0; i < t->hist_n; i++) {
        uint32_t src;

        src = (t->hist_i + t->hist_cap - t->hist_n + i) % t->hist_cap;
        memcpy(next + (size_t)i * cols,
            t->hist + (size_t)src * t->hist_cols,
            (size_t)copy * sizeof *next);
    }
    free(t->hist);
    t->hist = next;
    t->hist_cols = cols;
    t->hist_i = t->hist_n % (t->hist_cap ? t->hist_cap : 1);
}

static TermScreen *
term_live(Term *t)
{
    return (t->mode & TERM_MODE_ALTSCREEN) ? &t->alt : &t->screen;
}

TermScreen *
term_screen(Term *t)
{
    TASSERT(t, "Invalid term.");
    if (!t)
        return NULL;
    return term_live(t);
}

uint32_t
term_hist_count(const Term *t)
{
    return t ? t->hist_n : 0;
}

const TermCell *
term_hist_line(const Term *t, uint32_t back)
{
    uint32_t i;

    if (!t || !t->hist || back >= t->hist_n || !t->hist_cols)
        return NULL;
    i = (t->hist_i + t->hist_cap - 1 - back) % t->hist_cap;
    return t->hist + (size_t)i * t->hist_cols;
}

TermStyle
term_cell_style(const Term *t, const TermCell *c)
{
    TermStyle z;

    memset(&z, 0, sizeof z);
    if (!t || !c || !t->styles || c->style == 0 || (uint32_t)c->style >= t->style_n)
        return z;
    return term_style_resolve(t, t->styles[c->style]);
}

TermStyle
term_cursor_style(const Term *t)
{
    TermStyle z;

    memset(&z, 0, sizeof z);
    if (!t)
        return z;
    return term_style_resolve(t, term_style_from_cursor(t));
}

void
term_colors_set(Term *t, const TermColors *colors)
{
    if (!t || !colors)
        return;
    t->colors = *colors;
}

int
term_init(Term *t, uint32_t cols, uint32_t rows, const TermColors *colors)
{
    TASSERT(t, "Invalid term.");
    if (!t || !cols || !rows)
        return 0;
    term_init_common(t, cols, rows, colors);
    term_screen_init(&t->screen, cols, rows);
    term_screen_init(&t->alt, cols, rows);
    t->cells_owned = 1;
    if (!t->screen.cell_buffer || !t->alt.cell_buffer) {
        term_destroy(t);
        return 0;
    }
    return 1;
}

int
term_init_on(Term *t, uint32_t cols, uint32_t rows, const TermColors *colors,
    TermCell *screen, TermCell *alt, uint32_t cap)
{
    TASSERT(t, "Invalid term.");
    if (!t || !cols || !rows || !screen || !alt || cap < cols * rows)
        return 0;
    term_init_common(t, cols, rows, colors);
    memset(screen, 0, (size_t)cap * sizeof *screen);
    memset(alt, 0, (size_t)cap * sizeof *alt);
    term_screen_adopt(&t->screen, screen, cols, rows, cap);
    term_screen_adopt(&t->alt, alt, cols, rows, cap);
    t->cells_owned = 0;
    return 1;
}

void
term_destroy(Term *t)
{
    if (!t)
        return;
    if (t->cells_owned) {
        term_screen_free(&t->screen);
        term_screen_free(&t->alt);
    } else {
        memset(&t->screen, 0, sizeof t->screen);
        memset(&t->alt, 0, sizeof t->alt);
    }
    free(t->hist);
    free(t->styles);
    free(t->style_hash);
    memset(t, 0, sizeof *t);
}

void
term_resize(Term *t, uint32_t cols, uint32_t rows)
{
    uint32_t old_rows;
    int full;

    TASSERT(t, "Invalid term.");
    if (!t || !cols || !rows || !t->cells_owned)
        return;
    old_rows = t->screen.rows;
    full = (t->top == 0 && old_rows && t->bot + 1 == old_rows);
    term_screen_grow(&t->screen, cols, rows);
    term_screen_grow(&t->alt, cols, rows);
    t->cursor.x = TERM_MIN(t->cursor.x, cols - 1);
    t->cursor.y = TERM_MIN(t->cursor.y, rows - 1);
    if (full || t->bot >= rows || t->top >= rows) {
        t->top = 0;
        t->bot = rows - 1;
    }
    term_hist_resize(t, cols);
}

void
term_resize_on(Term *t, uint32_t cols, uint32_t rows,
    TermCell *screen, TermCell *alt, uint32_t cap)
{
    uint32_t old_rows;
    int full;
    int owned;
    TermCell *old_scr;
    TermCell *old_alt;

    TASSERT(t, "Invalid term.");
    if (!t || !cols || !rows || !screen || !alt || cap < cols * rows)
        return;
    old_rows = t->screen.rows;
    full = (t->top == 0 && old_rows && t->bot + 1 == old_rows);
    owned = t->cells_owned;
    old_scr = t->screen.cell_buffer;
    old_alt = t->alt.cell_buffer;
    term_screen_copy_on(&t->screen, screen, cols, rows, cap);
    term_screen_copy_on(&t->alt, alt, cols, rows, cap);
    if (owned) {
        if (old_scr && old_scr != screen)
            free(old_scr);
        if (old_alt && old_alt != alt)
            free(old_alt);
    }
    t->cells_owned = 0;
    t->cursor.x = TERM_MIN(t->cursor.x, cols - 1);
    t->cursor.y = TERM_MIN(t->cursor.y, rows - 1);
    if (full || t->bot >= rows || t->top >= rows) {
        t->top = 0;
        t->bot = rows - 1;
    }
    term_hist_resize(t, cols);
}

static void
term_index(Term *t)
{
    TermScreen *s;

    s = term_live(t);
    t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
    if (t->cursor.y == t->bot) {
        term_scroll(t, t->top, t->bot, 1);
        return;
    }
    if (t->cursor.y + 1 < s->rows)
        t->cursor.y++;
}

static void
term_reverse_index(Term *t)
{
    t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
    if (t->cursor.y == t->top) {
        term_scroll(t, t->top, t->bot, -1);
        return;
    }
    if (t->cursor.y > 0)
        t->cursor.y--;
}

static void
term_next_line(Term *t)
{
    t->cursor.x = 0;
    term_index(t);
}

static void
term_lf(Term *t)
{
    term_index(t);
    if (t->mode & TERM_MODE_CRLF) {
        t->cursor.x = 0;
        t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
    }
}

static void
term_cursor_up(Term *t, uint32_t n)
{
    uint32_t y;
    uint32_t lim;

    if (!n)
        return;
    y = t->cursor.y;
    lim = (y >= t->top) ? t->top : 0;
    y = (y > n) ? y - n : 0;
    if (y < lim)
        y = lim;
    term_move_to(t, t->cursor.x, y);
}

static void
term_cursor_down(Term *t, uint32_t n)
{
    TermScreen *s;
    uint32_t y;
    uint32_t lim;

    s = term_live(t);
    if (!n)
        return;
    y = t->cursor.y;
    lim = s->rows ? s->rows - 1 : 0;
    if (y <= t->bot)
        lim = t->bot;
    if (n < lim - y)
        y += n;
    else
        y = lim;
    term_move_to(t, t->cursor.x, y);
}

static void
term_cursor_forward(Term *t)
{
    TermScreen *s;

    s = term_live(t);
    if (t->cursor.x + 1 < s->cols) {
        t->cursor.x++;
        t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
        return;
    }
    if (t->mode & TERM_MODE_WRAP)
        t->cursor.state |= TERM_WRAPNEXT;
}

static int
term_codepoint_width(uint32_t c)
{
    if (c == 0)
        return 0;
    if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F))
        return -1;
    if ((c >= 0x0300 && c <= 0x036F) ||
        (c >= 0x1AB0 && c <= 0x1AFF) ||
        (c >= 0x1DC0 && c <= 0x1DFF) ||
        (c >= 0x200B && c <= 0x200F) ||
        (c >= 0x202A && c <= 0x202E) ||
        (c >= 0x2060 && c <= 0x206F) ||
        (c >= 0x20D0 && c <= 0x20FF) ||
        (c >= 0x3099 && c <= 0x309A) ||
        (c >= 0xFE00 && c <= 0xFE0F) ||
        (c >= 0xFE20 && c <= 0xFE2F) ||
        c == 0xFEFF ||
        (c >= 0xE0100 && c <= 0xE01EF))
        return 0;
    if ((c >= 0x1100 && c <= 0x115F) ||
        (c >= 0x2329 && c <= 0x232A) ||
        (c >= 0x2E80 && c <= 0xA4CF && c != 0x303F) ||
        (c >= 0xAC00 && c <= 0xD7A3) ||
        (c >= 0xF900 && c <= 0xFAFF) ||
        (c >= 0xFE10 && c <= 0xFE19) ||
        (c >= 0xFE30 && c <= 0xFE6F) ||
        (c >= 0xFF00 && c <= 0xFF60) ||
        (c >= 0xFFE0 && c <= 0xFFE6) ||
        (c >= 0x1F300 && c <= 0x1FAFF))
        return 2;
    return 1;
}

static int
term_utf8_consume(Term *t, unsigned char ch, uint32_t *out)
{
    if (t->utf8_rem) {
        if ((ch & 0xC0) == 0x80) {
            t->utf8_acc = (t->utf8_acc << 6) | (uint32_t)(ch & 0x3F);
            t->utf8_rem--;
            if (t->utf8_rem)
                return 0;
            if (t->utf8_acc > 0x10FFFF ||
                (t->utf8_acc >= 0xD800 && t->utf8_acc <= 0xDFFF) ||
                t->utf8_acc < t->utf8_min) {
                *out = TERM_UTF_INVALID;
                return 1;
            }
            *out = t->utf8_acc;
            return 1;
        }
        t->utf8_rem = 0;
        *out = TERM_UTF_INVALID;
        return 2;
    }

    if (ch < 0x80) {
        *out = ch;
        return 1;
    }
    if ((ch & 0xE0) == 0xC0) {
        t->utf8_acc = ch & 0x1F;
        t->utf8_rem = 1;
        t->utf8_min = 0x80;
        return 0;
    }
    if ((ch & 0xF0) == 0xE0) {
        t->utf8_acc = ch & 0x0F;
        t->utf8_rem = 2;
        t->utf8_min = 0x800;
        return 0;
    }
    if ((ch & 0xF8) == 0xF0) {
        t->utf8_acc = ch & 0x07;
        t->utf8_rem = 3;
        t->utf8_min = 0x10000;
        return 0;
    }
    *out = TERM_UTF_INVALID;
    return 1;
}

static uint32_t
term_acs_map(uint32_t c)
{
    static const uint32_t map[32] = {
        0x00A0, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0,
        0x00B1, 0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C,
        0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534,
        0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7,
    };

    if (c < 0x5Fu || c > 0x7Eu)
        return c;
    return map[c - 0x5Fu];
}

static void
term_putc(Term *t, uint32_t c)
{
    TermScreen *s;
    int width;

    s = term_live(t);
    if ((t->cs_gl ? t->cs_g1 : t->cs_g0) && c >= 0x5Fu && c <= 0x7Eu)
        c = term_acs_map(c);
    width = term_codepoint_width(c);
    if (width == 0) {
        TermCell *cell;

        if (!t->seq_n || t->seq_n >= TERM_SEQ_MAX || !s->cols)
            return;
        t->seq[t->seq_n++] = c;
        cell = term_cell_at(s, t->seq_i % s->cols, t->seq_i / s->cols);
        if (cell)
            cell->glyph = vt_glyph_get_run(t->seq, t->seq_n);
        return;
    }
    if (width < 0)
        return;

    if (t->cursor.state & TERM_WRAPNEXT) {
        term_next_line(t);
        t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
        s = term_live(t);
    }

    if (t->cursor.x + (uint32_t)width > s->cols)
        term_next_line(t);

    if (t->mode & TERM_MODE_INSERT)
        term_insert_blank(t, (uint32_t)width);

    {
        TermCell *cell;

        cell = term_cell_at(s, t->cursor.x, t->cursor.y);
        TASSERT(cell);
        if (!cell)
            return;
        term_cell_put(t, cell, c);
        t->seq[0] = c;
        t->seq_n = 1;
        t->seq_i = t->cursor.y * s->cols + t->cursor.x;
        if (c >= 128)
            cell->glyph = vt_glyph_get(c);
        term_cursor_forward(t);

        if (width == 2 && t->cursor.x != 0) {
            cell = term_cell_at(s, t->cursor.x, t->cursor.y);
            if (cell)
                term_cell_put(t, cell, 0);
            term_cursor_forward(t);
        }
    }
}

static void
term_move_to(Term *t, uint32_t x, uint32_t y)
{
    TermScreen *s;

    s = term_live(t);
    t->cursor.x = TERM_MIN(x, s->cols ? s->cols - 1 : 0);
    t->cursor.y = TERM_MIN(y, s->rows ? s->rows - 1 : 0);
    t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
}

static void
term_move_abs(Term *t, uint32_t x, uint32_t y)
{
    term_move_to(t, x, y);
}

static void
term_insert_blank(Term *t, uint32_t n)
{
    TermScreen *s;
    uint32_t x;
    uint32_t rest;

    s = term_live(t);
    x = t->cursor.x;
    if (n == 0 || x >= s->cols)
        return;
    if (n > s->cols - x)
        n = s->cols - x;
    rest = s->cols - x - n;
    if (rest) {
        TermCell *row;

        row = term_row(s, t->cursor.y);
        if (row)
            memmove(row + x + n, row + x, rest * sizeof *row);
    }
    term_clear_region(t, x, t->cursor.y, x + n - 1, t->cursor.y);
}

static void
term_delete_chars(Term *t, uint32_t n)
{
    TermScreen *s;
    uint32_t x;
    uint32_t rest;

    s = term_live(t);
    x = t->cursor.x;
    if (n == 0 || x >= s->cols)
        return;
    if (n > s->cols - x)
        n = s->cols - x;
    rest = s->cols - x - n;
    if (rest) {
        TermCell *row;

        row = term_row(s, t->cursor.y);
        if (row)
            memmove(row + x, row + x + n, rest * sizeof *row);
    }
    term_clear_region(t, s->cols - n, t->cursor.y, s->cols - 1, t->cursor.y);
}

static void
term_erase_chars(Term *t, uint32_t n)
{
    TermScreen *s;
    uint32_t x1;

    s = term_live(t);
    if (n == 0 || t->cursor.x >= s->cols)
        return;
    x1 = t->cursor.x + n - 1;
    if (x1 >= s->cols)
        x1 = s->cols - 1;
    term_clear_region(t, t->cursor.x, t->cursor.y, x1, t->cursor.y);
}

static void
term_scroll(Term *t, uint32_t y0, uint32_t y1, int n)
{
    TermScreen *s;
    uint32_t rows;
    uint32_t cols;
    int span;

    s = term_live(t);
    if (!s->cols || !s->rows)
        return;
    if (y1 >= s->rows)
        y1 = s->rows - 1;
    if (y0 > y1)
        return;
    cols = s->cols;
    rows = y1 - y0 + 1;
    span = (int)rows;
    if (n > span)
        n = span;
    if (n < -span)
        n = -span;
    if (n == 0)
        return;
    if (n > 0) {
        if (!(t->mode & TERM_MODE_ALTSCREEN) && y0 == 0 && y1 + 1 == s->rows
            && t->hist && t->hist_cols == cols) {
            int k;

            for (k = 0; k < n; k++) {
                memcpy(t->hist + (size_t)t->hist_i * cols,
                    term_row(s, y0 + (uint32_t)k),
                    (size_t)cols * sizeof *t->hist);
                t->hist_i = (t->hist_i + 1) % t->hist_cap;
                if (t->hist_n < t->hist_cap)
                    t->hist_n++;
            }
        }
        if (y0 == 0 && y1 + 1 == s->rows) {
            uint32_t y;
            uint32_t yb;

            s->origin += (uint32_t)n;
            if (s->origin >= s->rows)
                s->origin -= s->rows;
            yb = s->rows - (uint32_t)n;
            for (y = yb; y < s->rows; y++)
                term_zero_logical_row(s, y);
            return;
        }
        if ((uint32_t)n < rows) {
            uint32_t y;

            for (y = y0; y <= y1 - (uint32_t)n; y++)
                term_copy_logical_row(s, y, y + (uint32_t)n);
        }
        term_clear_region(t, 0, y1 - (uint32_t)n + 1, cols - 1, y1);
    } else {
        n = -n;
        if (y0 == 0 && y1 + 1 == s->rows) {
            uint32_t y;
            uint32_t ye;

            if (s->origin >= (uint32_t)n)
                s->origin -= (uint32_t)n;
            else
                s->origin += s->rows - (uint32_t)n;
            ye = (uint32_t)n;
            for (y = 0; y < ye; y++)
                term_zero_logical_row(s, y);
            return;
        }
        if ((uint32_t)n < rows) {
            uint32_t y;

            y = y1;
            for (;;) {
                term_copy_logical_row(s, y, y - (uint32_t)n);
                if (y == y0 + (uint32_t)n)
                    break;
                y--;
            }
        }
        term_clear_region(t, 0, y0, cols - 1, y0 + (uint32_t)n - 1);
    }
}

static void
term_reset_margins(Term *t)
{
    TermScreen *s;

    s = term_live(t);
    t->top = 0;
    t->bot = s->rows ? s->rows - 1 : 0;
}

static void
term_clear_screen(Term *t, TermScreen *s)
{
    uint32_t i;
    uint32_t n;
    uint16_t sid;

    if (!s || !s->cell_buffer || !s->cols || !s->rows)
        return;
    sid = term_style_intern(t);
    s->origin = 0;
    n = s->cols * s->rows;
    for (i = 0; i < n; i++) {
        s->cell_buffer[i].codepoint = 0;
        s->cell_buffer[i].style = sid;
        s->cell_buffer[i].tag = TERM_CELL_CODE;
        s->cell_buffer[i].glyph = 0;
    }
}

static void
term_decaln(Term *t)
{
    TermScreen *s;
    uint32_t i;
    uint32_t n;

    s = term_live(t);
    term_reset_margins(t);
    t->cursor.x = 0;
    t->cursor.y = 0;
    t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
    if (!s->cell_buffer || !s->cols || !s->rows)
        return;
    s->origin = 0;
    n = s->cols * s->rows;
    for (i = 0; i < n; i++)
        term_cell_put(t, &s->cell_buffer[i], 'E');
}

static void
term_reset(Term *t)
{
    t->mode = TERM_MODE_UTF8 | TERM_MODE_WRAP;
    t->state = 0;
    t->utf8_acc = 0;
    t->utf8_min = 0;
    t->utf8_rem = 0;
    t->last_ch = 0;
    t->seq_n = 0;
    t->seq_i = 0;
    t->cs_g0 = 0;
    t->cs_g1 = 0;
    t->cs_gl = 0;
    t->cs_sel = 0;
    t->cursor.attr = 0;
    t->cursor.state = 0;
    t->cursor.x = 0;
    t->cursor.y = 0;
    t->cursor.fg = 0;
    t->cursor.bg = 0;
    t->cursor.fg_kind = TERM_COLOR_DEF;
    t->cursor.bg_kind = TERM_COLOR_DEF;
    t->cursor.fg_idx = 0;
    t->cursor.bg_idx = 0;
    term_style_intern(t);
    t->saved = t->cursor;
    memset(&t->csi, 0, sizeof t->csi);
    memset(&t->str, 0, sizeof t->str);
    term_reset_margins(t);
    term_clear_screen(t, &t->screen);
    term_clear_screen(t, &t->alt);
}

static void
term_alt(Term *t, int on, int save_cur)
{
    TermScreen *s;

    if (on) {
        if (t->mode & TERM_MODE_ALTSCREEN)
            return;
        if (save_cur)
            t->saved = t->cursor;
        t->mode |= TERM_MODE_ALTSCREEN;
        s = term_live(t);
        if (s->cols && s->rows)
            term_clear_region(t, 0, 0, s->cols - 1, s->rows - 1);
        t->cursor.x = 0;
        t->cursor.y = 0;
        t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
        term_reset_margins(t);
    } else {
        if (!(t->mode & TERM_MODE_ALTSCREEN))
            return;
        t->mode &= ~TERM_MODE_ALTSCREEN;
        term_reset_margins(t);
        if (save_cur) {
            t->cursor = t->saved;
            term_move_to(t, t->cursor.x, t->cursor.y);
        }
    }
}

static void
term_reply_str(Term *t, const char *s)
{
    size_t n;
    size_t room;

    if (!s)
        return;
    n = strlen(s);
    room = sizeof t->reply - t->reply_n;
    if (n > room)
        n = room;
    if (!n)
        return;
    memcpy(t->reply + t->reply_n, s, n);
    t->reply_n += (uint32_t)n;
}

static uint32_t
term_color_256(Term *t, int n)
{
    static const int cube[6] = { 0, 95, 135, 175, 215, 255 };
    int idx;
    int g;

    if (n < 0)
        n = 0;
    if (n > 255)
        n = 255;
    if (n < 16)
        return t->colors.fg[n];
    if (n < 232) {
        idx = n - 16;
        return ((uint32_t)cube[idx / 36] << 16) |
            ((uint32_t)cube[(idx / 6) % 6] << 8) |
            (uint32_t)cube[idx % 6];
    }
    g = 8 + (n - 232) * 10;
    return ((uint32_t)g << 16) | ((uint32_t)g << 8) | (uint32_t)g;
}

static void
term_set_mode(Term *t, int set)
{
    int32_t i;

    for (i = 0; i < t->csi.narg; i++) {
        int32_t a = t->csi.arg[i];

        if (t->csi.priv == '?') {
            switch (a) {
            case 7:
                if (set)
                    t->mode |= TERM_MODE_WRAP;
                else
                    t->mode &= ~TERM_MODE_WRAP;
                break;
            case 25:
                if (set)
                    t->mode &= ~TERM_MODE_HIDE;
                else
                    t->mode |= TERM_MODE_HIDE;
                break;
            case 47:
            case 1047:
                term_alt(t, set, 0);
                break;
            case 1048:
                if (set)
                    t->saved = t->cursor;
                else {
                    t->cursor = t->saved;
                    term_move_to(t, t->cursor.x, t->cursor.y);
                }
                break;
            case 1049:
                term_alt(t, set, 1);
                break;
            case 1000:
                if (set)
                    t->mode |= TERM_MODE_MOUSEBTN;
                else
                    t->mode &= ~TERM_MODE_MOUSEBTN;
                break;
            case 1002:
                if (set)
                    t->mode |= TERM_MODE_MOUSEMOT;
                else
                    t->mode &= ~TERM_MODE_MOUSEMOT;
                break;
            case 1003:
                if (set)
                    t->mode |= TERM_MODE_MOUSEMANY;
                else
                    t->mode &= ~TERM_MODE_MOUSEMANY;
                break;
            case 1006:
                if (set)
                    t->mode |= TERM_MODE_MOUSESGR;
                else
                    t->mode &= ~TERM_MODE_MOUSESGR;
                break;
            case 2004:
                if (set)
                    t->mode |= TERM_MODE_BRKTPASTE;
                else
                    t->mode &= ~TERM_MODE_BRKTPASTE;
                break;
            default:
                break;
            }
        } else if (!t->csi.priv) {
            if (a == 4) {
                if (set)
                    t->mode |= TERM_MODE_INSERT;
                else
                    t->mode &= ~TERM_MODE_INSERT;
            } else if (a == 20) {
                if (set)
                    t->mode |= TERM_MODE_CRLF;
                else
                    t->mode &= ~TERM_MODE_CRLF;
            }
        }
    }
}

static void
term_clear_region(Term *t, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    TermScreen *s;
    uint32_t y;
    uint32_t x;
    uint16_t sid;

    s = term_live(t);
    if (s->cols == 0 || s->rows == 0)
        return;
    if (x0 > x1) { uint32_t tmp = x0; x0 = x1; x1 = tmp; }
    if (y0 > y1) { uint32_t tmp = y0; y0 = y1; y1 = tmp; }
    if (x0 >= s->cols || y0 >= s->rows)
        return;
    x1 = TERM_MIN(x1, s->cols - 1);
    y1 = TERM_MIN(y1, s->rows - 1);

    sid = term_style_intern(t);
    for (y = y0; y <= y1; y++) {
        TermCell *row;

        row = term_row(s, y);
        if (!row)
            continue;
        for (x = x0; x <= x1; x++) {
            TermCell *c = &row[x];
            c->codepoint = 0;
            c->style = sid;
            c->tag = TERM_CELL_CODE;
            c->glyph = 0;
        }
    }
}

static void
term_handle_c0(Term *t, unsigned char code)
{
    if (code != TERM_ESC && code != TERM_BEL)
        t->state &= ~(TERM_ESC_START | TERM_ESC_CSI | TERM_ESC_ALTCHARSET |
            TERM_ESC_TEST | TERM_ESC_UTF8);
    switch (code) {
    case '\t': {
        TermScreen *s = term_live(t);
        uint32_t nx = (t->cursor.x + 8u) & ~7u;
        if (nx >= s->cols && s->cols)
            nx = s->cols - 1;
        term_move_to(t, nx, t->cursor.y);
        return;
    }
    case '\b':
        t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
        if (t->cursor.x > 0)
            t->cursor.x--;
        return;
    case '\r':
        term_move_to(t, 0, t->cursor.y);
        return;
    case '\f': /* FALLTHROUGH */
    case '\v': /* FALLTHROUGH */
    case '\n':
        term_lf(t);
        return;
    case TERM_BEL:
        return;
    case TERM_ESC:
        t->state &= ~(TERM_ESC_CSI | TERM_ESC_ALTCHARSET | TERM_ESC_TEST | TERM_ESC_STR);
        t->state |= TERM_ESC_START;
        memset(&t->csi, 0, sizeof t->csi);
        return;
    case '\016':
        t->cs_gl = 1;
        return;
    case '\017':
        t->cs_gl = 0;
        return;
    case TERM_SUB:
    case TERM_CAN:
        break;
    default:
        break;
    }
    t->state &= ~TERM_ESC_STR;
}

static void
term_handle_c1(Term *t, unsigned char code)
{
    (void)code;
    t->state &= ~TERM_ESC_STR;
}

static int
term_handle_esc(Term *t, unsigned char ascii)
{
    switch (ascii) {
    case '#':
        t->state |= TERM_ESC_TEST;
        return 0;
    case '%':
        t->state |= TERM_ESC_UTF8;
        return 0;
    case 'n':
    case 'o':
        break;
    case '(':
        t->state |= TERM_ESC_ALTCHARSET;
        t->cs_sel = 0;
        return 0;
    case ')':
        t->state |= TERM_ESC_ALTCHARSET;
        t->cs_sel = 1;
        return 0;
    case '*':
        t->state |= TERM_ESC_ALTCHARSET;
        t->cs_sel = 2;
        return 0;
    case '+':
        t->state |= TERM_ESC_ALTCHARSET;
        t->cs_sel = 3;
        return 0;
    case 'D':
        term_index(t);
        break;
    case 'E':
        term_next_line(t);
        break;
    case 'H':
        break;
    case 'M':
        term_reverse_index(t);
        break;
    case 'Z':
        term_reply_str(t, "\033[?1;2c");
        break;
    case 'c':
        term_reset(t);
        break;
    case '=':
        break;
    case '>':
        break;
    case '7':
        t->saved = t->cursor;
        break;
    case '8':
        t->cursor = t->saved;
        term_move_to(t, t->cursor.x, t->cursor.y);
        break;
    case '\\':
        break;
    default:
        break;
    }
    return 1;
}

static void
term_handle_str(Term *t)
{
    char buf[64];
    uint32_t rgb;
    const char *p;

    if (t->str.type != ']')
        return;
    p = t->str_buf;
    if (p[0] == '1' && (p[1] == '0' || p[1] == '1' || p[1] == '2') && p[2] == ';' && p[3] == '?') {
        rgb = (p[1] == '1') ? t->colors.bg[t->colors.bg_default < 8 ? t->colors.bg_default : 0]
            : t->colors.fg[t->colors.fg_default < 16 ? t->colors.fg_default : 7];
        snprintf(buf, sizeof buf, "\033]%c%c;rgb:%02x%02x/%02x%02x/%02x%02x\007",
            p[0], p[1],
            (rgb >> 16) & 0xff, (rgb >> 16) & 0xff,
            (rgb >> 8) & 0xff, (rgb >> 8) & 0xff,
            rgb & 0xff, rgb & 0xff);
        term_reply_str(t, buf);
    }
}

static void
term_cursor_sgr(Term *t, const int32_t *attr, int32_t n)
{
    int32_t i;

    for (i = 0; i < n; i++) {
        switch (attr[i]) {
        case 0:
            t->cursor.attr = TERM_ATTR_NONE;
            t->cursor.fg = 0;
            t->cursor.bg = 0;
            t->cursor.fg_kind = TERM_COLOR_DEF;
            t->cursor.bg_kind = TERM_COLOR_DEF;
            t->cursor.fg_idx = 0;
            t->cursor.bg_idx = 0;
            break;
        case 1:
            t->cursor.attr |= TERM_ATTR_BOLD;
            break;
        case 2:
            t->cursor.attr |= TERM_ATTR_FAINT;
            break;
        case 3:
            t->cursor.attr |= TERM_ATTR_ITALIC;
            break;
        case 4:
            t->cursor.attr |= TERM_ATTR_UNDERLINE;
            break;
        case 5:
        case 6:
            t->cursor.attr |= TERM_ATTR_BLINK;
            break;
        case 7:
            t->cursor.attr |= TERM_ATTR_REVERSE;
            break;
        case 8:
            t->cursor.attr |= TERM_ATTR_INVISIBLE;
            break;
        case 9:
            t->cursor.attr |= TERM_ATTR_STRUCK;
            break;
        case 22:
            t->cursor.attr &= (uint8_t)~(TERM_ATTR_BOLD | TERM_ATTR_FAINT);
            break;
        case 23:
            t->cursor.attr &= (uint8_t)~TERM_ATTR_ITALIC;
            break;
        case 24:
            t->cursor.attr &= (uint8_t)~TERM_ATTR_UNDERLINE;
            break;
        case 25:
            t->cursor.attr &= (uint8_t)~TERM_ATTR_BLINK;
            break;
        case 27:
            t->cursor.attr &= (uint8_t)~TERM_ATTR_REVERSE;
            break;
        case 28:
            t->cursor.attr &= (uint8_t)~TERM_ATTR_INVISIBLE;
            break;
        case 29:
            t->cursor.attr &= (uint8_t)~TERM_ATTR_STRUCK;
            break;
        case 38:
            if (i + 1 < n && attr[i + 1] == 5 && i + 2 < n) {
                if (attr[i + 2] >= 0 && attr[i + 2] < 16) {
                    t->cursor.fg_kind = TERM_COLOR_PAL;
                    t->cursor.fg_idx = (uint8_t)attr[i + 2];
                    t->cursor.fg = 0;
                } else {
                    t->cursor.fg_kind = TERM_COLOR_RGB;
                    t->cursor.fg = term_color_256(t, (int)attr[i + 2]);
                }
                i += 2;
            } else if (i + 1 < n && attr[i + 1] == 2 && i + 4 < n) {
                t->cursor.fg_kind = TERM_COLOR_RGB;
                t->cursor.fg =
                    (((uint32_t)attr[i + 2] & 255u) << 16) |
                    (((uint32_t)attr[i + 3] & 255u) << 8) |
                    ((uint32_t)attr[i + 4] & 255u);
                i += 4;
            }
            break;
        case 39:
            t->cursor.fg = 0;
            t->cursor.fg_kind = TERM_COLOR_DEF;
            t->cursor.fg_idx = 0;
            break;
        case 48:
            if (i + 1 < n && attr[i + 1] == 5 && i + 2 < n) {
                if (attr[i + 2] >= 0 && attr[i + 2] < 16) {
                    t->cursor.bg_kind = TERM_COLOR_PAL;
                    t->cursor.bg_idx = (uint8_t)attr[i + 2];
                    t->cursor.bg = 0;
                } else {
                    t->cursor.bg_kind = TERM_COLOR_RGB;
                    t->cursor.bg = term_color_256(t, (int)attr[i + 2]);
                }
                i += 2;
            } else if (i + 1 < n && attr[i + 1] == 2 && i + 4 < n) {
                t->cursor.bg_kind = TERM_COLOR_RGB;
                t->cursor.bg =
                    (((uint32_t)attr[i + 2] & 255u) << 16) |
                    (((uint32_t)attr[i + 3] & 255u) << 8) |
                    ((uint32_t)attr[i + 4] & 255u);
                i += 4;
            }
            break;
        case 49:
            t->cursor.bg = 0;
            t->cursor.bg_kind = TERM_COLOR_DEF;
            t->cursor.bg_idx = 0;
            break;
        default:
            if (TERM_BETWEEN(attr[i], 30, 37)) {
                t->cursor.fg_kind = TERM_COLOR_PAL;
                t->cursor.fg_idx = (uint8_t)(attr[i] - 30);
                t->cursor.fg = 0;
            } else if (TERM_BETWEEN(attr[i], 40, 47)) {
                t->cursor.bg_kind = TERM_COLOR_PAL;
                t->cursor.bg_idx = (uint8_t)(attr[i] - 40);
                t->cursor.bg = 0;
            } else if (TERM_BETWEEN(attr[i], 90, 97)) {
                t->cursor.fg_kind = TERM_COLOR_PAL;
                t->cursor.fg_idx = (uint8_t)(attr[i] - 90 + 8);
                t->cursor.fg = 0;
            } else if (TERM_BETWEEN(attr[i], 100, 107)) {
                t->cursor.bg_kind = TERM_COLOR_PAL;
                t->cursor.bg_idx = (uint8_t)(attr[i] - 100);
                t->cursor.bg = 0;
            }
            break;
        }
    }
    term_style_intern(t);
}

static int
term_parse_csi(Term *t)
{
    char *ptr;
    char *end;
    char *np;
    int32_t value;

    if (t->csi.len == 0)
        return 0;
    ptr = t->csi.buf;
    end = ptr + t->csi.len;
    t->csi.narg = 0;

    if (*ptr == '?' || *ptr == '>') {
        t->csi.priv = *ptr;
        ptr++;
    }

    for (; ptr < end; ptr++) {
        np = NULL;
        value = (int32_t)strtol(ptr, &np, 10);
        if (np == ptr)
            value = 0;
        t->csi.arg[t->csi.narg++] = value;
        ptr = np;
        if (*ptr != ';' || t->csi.narg == TERM_ESC_ARG_SIZ)
            break;
    }

    t->csi.mode[1] = 0;
    while (ptr < end && *ptr >= 0x20 && *ptr <= 0x2F) {
        t->csi.mode[1] = *ptr;
        ptr++;
    }
    if (ptr >= end || *ptr < 0x40 || *ptr > 0x7E) {
        memset(&t->csi, 0, sizeof t->csi);
        return 0;
    }

    t->csi.mode[0] = *ptr;
    return 1;
}

static void
term_handle_csi(Term *t)
{
    TermScreen *s;
    char buf[32];

    s = term_live(t);
    switch (t->csi.mode[0]) {
    default:
        memset(&t->csi, 0, sizeof t->csi);
        break;
    case '@':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_insert_blank(t, (uint32_t)t->csi.arg[0]);
        break;
    case 'A':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_cursor_up(t, (uint32_t)t->csi.arg[0]);
        break;
    case 'B':
    case 'e':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_cursor_down(t, (uint32_t)t->csi.arg[0]);
        break;
    case 'c':
        if (t->csi.priv == '>')
            term_reply_str(t, "\033[>0;276;0c");
        else
            term_reply_str(t, "\033[?1;2c");
        break;
    case 'b':
        TERM_DEFAULT(t->csi.arg[0], 1);
        if (t->last_ch > 0) {
            while (t->csi.arg[0]-- > 0)
                term_putc(t, t->last_ch);
        }
        break;
    case 'C':
    case 'a':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_move_to(t, t->cursor.x + (uint32_t)t->csi.arg[0], t->cursor.y);
        break;
    case 'D':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_move_to(t,
            t->cursor.x > (uint32_t)t->csi.arg[0] ? t->cursor.x - (uint32_t)t->csi.arg[0] : 0,
            t->cursor.y);
        break;
    case 'E':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_cursor_down(t, (uint32_t)t->csi.arg[0]);
        term_move_to(t, 0, t->cursor.y);
        break;
    case 'F':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_cursor_up(t, (uint32_t)t->csi.arg[0]);
        term_move_to(t, 0, t->cursor.y);
        break;
    case 'g':
        break;
    case 'G':
    case '`':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_move_to(t, (uint32_t)t->csi.arg[0] - 1, t->cursor.y);
        break;
    case 'H':
    case 'f':
        TERM_DEFAULT(t->csi.arg[0], 1);
        TERM_DEFAULT(t->csi.arg[1], 1);
        term_move_abs(t, (uint32_t)t->csi.arg[1] - 1, (uint32_t)t->csi.arg[0] - 1);
        break;
    case 'I': {
        uint32_t n;
        uint32_t i;
        TERM_DEFAULT(t->csi.arg[0], 1);
        n = (uint32_t)t->csi.arg[0];
        for (i = 0; i < n; i++)
            term_handle_c0(t, '\t');
        break;
    }
    case 'J':
        switch (t->csi.arg[0]) {
        case 0:
            term_clear_region(t, t->cursor.x, t->cursor.y, s->cols - 1, t->cursor.y);
            if (t->cursor.y + 1 < s->rows)
                term_clear_region(t, 0, t->cursor.y + 1, s->cols - 1, s->rows - 1);
            break;
        case 1:
            if (t->cursor.y > 0)
                term_clear_region(t, 0, 0, s->cols - 1, t->cursor.y - 1);
            term_clear_region(t, 0, t->cursor.y, t->cursor.x, t->cursor.y);
            break;
        default:
            term_clear_region(t, 0, 0, s->cols - 1, s->rows - 1);
            break;
        }
        break;
    case 'K':
        switch (t->csi.arg[0]) {
        case 1:
            term_clear_region(t, 0, t->cursor.y, t->cursor.x, t->cursor.y);
            break;
        case 2:
            term_clear_region(t, 0, t->cursor.y, s->cols - 1, t->cursor.y);
            break;
        default:
            term_clear_region(t, t->cursor.x, t->cursor.y, s->cols - 1, t->cursor.y);
            break;
        }
        break;
    case 'S':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_scroll(t, t->top, t->bot, (int)t->csi.arg[0]);
        break;
    case 'T':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_scroll(t, t->top, t->bot, -(int)t->csi.arg[0]);
        break;
    case 'L':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_scroll(t, t->cursor.y, t->bot, -(int)t->csi.arg[0]);
        break;
    case 'l':
        term_set_mode(t, 0);
        break;
    case 'M':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_scroll(t, t->cursor.y, t->bot, (int)t->csi.arg[0]);
        break;
    case 'X':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_erase_chars(t, (uint32_t)t->csi.arg[0]);
        break;
    case 'P':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_delete_chars(t, (uint32_t)t->csi.arg[0]);
        break;
    case 'Z': {
        uint32_t n;
        uint32_t x;
        TERM_DEFAULT(t->csi.arg[0], 1);
        n = (uint32_t)t->csi.arg[0];
        x = t->cursor.x;
        while (n--) {
            if (x == 0)
                break;
            x = (x - 1) & ~7u;
        }
        term_move_to(t, x, t->cursor.y);
        break;
    }
    case 'd':
        TERM_DEFAULT(t->csi.arg[0], 1);
        term_move_abs(t, t->cursor.x, (uint32_t)t->csi.arg[0] - 1);
        break;
    case 'h':
        term_set_mode(t, 1);
        break;
    case 'm':
        if (!t->csi.priv)
            term_cursor_sgr(t, t->csi.arg, t->csi.narg);
        break;
    case 'n':
        if (t->csi.arg[0] == 6) {
            snprintf(buf, sizeof buf, "\033[%u;%uR",
                t->cursor.y + 1, t->cursor.x + 1);
            term_reply_str(t, buf);
        } else if (t->csi.arg[0] == 5) {
            term_reply_str(t, "\033[0n");
        }
        break;
    case 'r':
        if (!t->csi.priv) {
            uint32_t top = t->csi.arg[0] ? (uint32_t)t->csi.arg[0] : 1;
            uint32_t bot = t->csi.arg[1] ? (uint32_t)t->csi.arg[1] : s->rows;
            if (top >= 1 && bot <= s->rows && top <= bot) {
                t->top = top - 1;
                t->bot = bot - 1;
                term_move_abs(t, 0, 0);
            } else if (!t->csi.arg[0] && !t->csi.arg[1]) {
                t->top = 0;
                t->bot = s->rows ? s->rows - 1 : 0;
                term_move_abs(t, 0, 0);
            }
        }
        break;
    case 's':
        if (!t->csi.priv)
            t->saved = t->cursor;
        break;
    case 'u':
        if (!t->csi.priv) {
            t->cursor = t->saved;
            term_move_to(t, t->cursor.x, t->cursor.y);
        }
        break;
    case 't':
        if (!t->csi.priv && t->csi.arg[0] == 18) {
            snprintf(buf, sizeof buf, "\033[8;%u;%ut", s->rows, s->cols);
            term_reply_str(t, buf);
        }
        break;
    case 'p':
        if (t->csi.priv == '?' && t->csi.mode[1] == '$') {
            snprintf(buf, sizeof buf, "\033[?%d;0$y", t->csi.arg[0]);
            term_reply_str(t, buf);
        }
        break;
    case 'q':
        if (t->csi.priv == '>')
            term_reply_str(t, "\033P>|vt\033\\");
        break;
    case ' ':
        break;
    }
}

static void
term_char_feed(Term *t, unsigned char ch)
{
    /* Escape path is 7-bit sequences. OSC payload is raw bytes, not UTF-8 decode. */
    if (t->state & TERM_ESC_STR) {
        if (ch == TERM_BEL || ch == TERM_CAN || ch == TERM_SUB || ch == TERM_ESC ||
            (!(t->mode & TERM_MODE_UTF8) && TERM_IS_C1(ch))) {
            if (ch == TERM_BEL || ch == TERM_ESC)
                term_handle_str(t);
            t->state = 0;
            t->utf8_rem = 0;
            if (ch == TERM_ESC) {
                t->state = TERM_ESC_START;
                memset(&t->csi, 0, sizeof t->csi);
            }
            return;
        }
        if (t->str.len + 1 < sizeof t->str_buf) {
            t->str_buf[t->str.len] = (char)ch;
            t->str_buf[t->str.len + 1] = 0;
        }
        t->str.len++;
        return;
    }

    if (TERM_IS_C0(ch) || TERM_IS_C1(ch)) {
        if (TERM_IS_C1(ch))
            term_handle_c1(t, ch);
        else
            term_handle_c0(t, ch);
        return;
    }

    if (t->state & TERM_ESC_START) {
        if (ch > 0x7E) {
            t->state = 0;
            t->last_ch = ch;
            term_putc(t, ch);
            return;
        }
        if (t->state & TERM_ESC_CSI) {
            if (t->csi.len < TERM_CSI_BUF_SIZ - 1)
                t->csi.buf[t->csi.len++] = (char)ch;
            if (TERM_BETWEEN(ch, 0x40, 0x7E) || t->csi.len > TERM_DEL * 10) {
                if (term_parse_csi(t))
                    term_handle_csi(t);
                t->state = 0;
                memset(&t->csi, 0, sizeof t->csi);
            }
            return;
        }
        if (t->state & TERM_ESC_TEST) {
            if (ch == '8')
                term_decaln(t);
            t->state = 0;
            return;
        }
        if (t->state & TERM_ESC_UTF8) {
            if (ch == '@')
                t->mode &= ~TERM_MODE_UTF8;
            else
                t->mode |= TERM_MODE_UTF8;
            t->state = 0;
            return;
        }
        if (t->state & TERM_ESC_ALTCHARSET) {
            if (t->cs_sel == 0)
                t->cs_g0 = (ch == '0');
            else if (t->cs_sel == 1)
                t->cs_g1 = (ch == '0');
            t->state = 0;
            return;
        }
        switch (ch) {
        case '[':
            t->state |= TERM_ESC_CSI;
            memset(&t->csi, 0, sizeof t->csi);
            return;
        case 'P':
        case '_':
        case '^':
        case ']':
        case 'k':
            t->state |= TERM_ESC_STR;
            memset(&t->str, 0, sizeof t->str);
            memset(t->str_buf, 0, sizeof t->str_buf);
            t->str.type = (char)ch;
            return;
        }
        if (!term_handle_esc(t, ch))
            return;
        t->state = 0;
        return;
    }

    t->last_ch = ch;
    term_putc(t, ch);
}

void
term_feed_printable(Term *t, const char *bytes, size_t len)
{
    /* NOTE(vasco): 0x20-0x7E only. Ground state.
     * Same-style ASCII is a row store, not per-byte putc. */
    size_t i;
    uint16_t sid;
    int acs;
    int insert;

    TASSERT(t && bytes, "Invalid term.");
    TASSERT(!(t->state & TERM_ESC_START) && !t->utf8_rem);
    if (!len)
        return;

    acs = (t->cs_gl ? t->cs_g1 : t->cs_g0) != 0;
    insert = (t->mode & TERM_MODE_INSERT) != 0;
    sid = 0;
    if (!acs && !insert)
        sid = term_style_intern(t);
    i = 0;
    while (i < len) {
        unsigned char ch;
        TermScreen *s;
        TermCell *cell;
        uint32_t n;
        uint32_t k;
        uint32_t room;

        ch = (unsigned char)bytes[i];
        TASSERT(ch >= 0x20 && ch < 0x7F);
        if (acs || insert) {
            t->last_ch = ch;
            term_putc(t, ch);
            i++;
            continue;
        }
        s = term_live(t);
        if (!s || !s->cell_buffer || !s->cols) {
            t->last_ch = ch;
            term_putc(t, ch);
            i++;
            continue;
        }
        if (t->cursor.state & TERM_WRAPNEXT) {
            term_next_line(t);
            t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
            s = term_live(t);
        }
        room = s->cols - t->cursor.x;
        n = (uint32_t)(len - i);
        if (n > room)
            n = room;
        if (!n) {
            t->last_ch = ch;
            term_putc(t, ch);
            i++;
            continue;
        }
        cell = term_row(s, t->cursor.y);
        if (!cell) {
            t->last_ch = ch;
            term_putc(t, ch);
            i++;
            continue;
        }
        cell += t->cursor.x;
        for (k = 0; k < n; k++) {
            cell[k].codepoint = (unsigned char)bytes[i + k];
            cell[k].style = sid;
            cell[k].tag = TERM_CELL_CODE;
            cell[k].glyph = 0;
        }
        t->last_ch = (unsigned char)bytes[i + n - 1];
        t->seq[0] = t->last_ch;
        t->seq_n = 1;
        t->cursor.x += n;
        if (t->cursor.x >= s->cols) {
            t->cursor.x = s->cols - 1;
            if (t->mode & TERM_MODE_WRAP)
                t->cursor.state |= TERM_WRAPNEXT;
            else
                t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
            t->seq_i = t->cursor.y * s->cols + t->cursor.x;
        } else {
            t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
            t->seq_i = t->cursor.y * s->cols + t->cursor.x - 1;
        }
        i += n;
    }
}

void
term_feed_utf8(Term *t, const char *bytes, size_t len)
{
    /* NOTE(vasco): complete UTF-8, high bytes only. Ground state. */
    size_t i;

    TASSERT(t && bytes, "Invalid term.");
    TASSERT(!(t->state & TERM_ESC_START) && !t->utf8_rem);
    if (!len)
        return;

    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)bytes[i];
        uint32_t cp;
        int st;

        TASSERT(ch >= 0x80);
        st = term_utf8_consume(t, ch, &cp);
        if (st == 0)
            continue;
        if (st == 2) {
            t->last_ch = 0;
            term_putc(t, TERM_UTF_INVALID);
            i--;
            continue;
        }
        t->last_ch = cp;
        term_putc(t, cp);
    }
    if (t->utf8_rem) {
        t->utf8_rem = 0;
        t->last_ch = 0;
        term_putc(t, TERM_UTF_INVALID);
    }
}

void
term_feed_escape(Term *t, const char *bytes, size_t len)
{
    /* NOTE(vasco): C0 / ESC / CSI / OSC. 7-bit. Complete sequences. Ground state. */
    size_t i;

    TASSERT(t && bytes, "Invalid term.");
    TASSERT(!(t->state & TERM_ESC_START) && !t->utf8_rem);
    if (!len)
        return;
    TASSERT((unsigned char)bytes[0] < 0x20 || (unsigned char)bytes[0] == 0x7F);
    for (i = 0; i < len; i++)
        term_char_feed(t, (unsigned char)bytes[i]);
    t->state = 0;
    t->utf8_rem = 0;
}
