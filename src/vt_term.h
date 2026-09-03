/* ===========================================================================
 * TERM - Copyright @ Vasco Alves - See LICENSE at the end of file.
 *
 * Cell-grid terminal emulator.
 * Partial st-style state-machine / parser.
 * Feed bytes -> Read the grid -> Profit.
 *
 * SURVIVOR:
 * - No OS. No GPU. No PTY. No Peak. Never #ifdef a brand.
 * - If you need a clock, fd, or window, you are in the wrong library.
 *
 * PREFIX: TERM (macros)  Term (types)  term_ (functions)
 *
 * USAGE:
 *     #include "vt_term.h"
 *     #include "vt_term.c"
 *
 *     TermColors colors = {0};
 *     Term term;
 *     term_init(&term, 80, 24, &colors);
 *     term_feed_printable(&term, bytes, len);
 *     TermScreen *s = term_screen(&term);
 *     term_destroy(&term);
 *
 * =========================================================================== */

#ifndef TERM_H
#define TERM_H

#define TERM_MAJOR 0
#define TERM_MINOR 7
#define TERM_PATCH 5

/* CHANGE LOG
 * 0.1.0 - @vasco - extract from vt: feed, grid, live CSI
 * 0.2.0 - @vasco - term_feed_ascii: clean CR/LF/printable, no state machine
 * 0.3.0 - @vasco - alt screen, scroll region, IL/DL/DCH/ECH, SGR 256/RGB, DSR/DA
 * 0.3.1 - @vasco - resize copies rows; alt switch resets scroll region
 * 0.3.2 - @vasco - CSI 18 t, DECRQM, XTVERSION; larger reply
 * 0.3.3 - @vasco - primary scroll hist; term_hist_line / term_hist_count
 * 0.3.4 - @vasco - DECSET mouse 1000/1002/1003/1006
 * 0.3.5 - @vasco - is_dirty on scroll, ICH/DCH, alt leave, resize
 * 0.3.6 - @vasco - term_init_on / term_resize_on: external screen/alt storage
 * 0.3.7 - @vasco - dirty putc, LF=IND, DECSTBM 1-line, CUU/CUD margins,
 *                  DECALN, RIS, ACS G0, resize_on adopt; TERM_MAX
 * 0.4.0 - @vasco - typed feed: printable / utf8 / escape / any
 * 0.4.1 - @vasco - printable is 0x20-0x7E; utf8 is high bytes; CR/LF is escape
 * 0.4.2 - @vasco - unknown C0/UTF-8 leaves ground state; VS/ZW format width 0
 * 0.4.3 - @vasco - UTF-8 OSC/DCS payload is not 8-bit C1
 * 0.5.0 - @vasco - 8-byte TermCell; interned style id; drop is_dirty; DECSET 2004
 * 0.6.0 - @vasco - term_cell_style; drop term_cell_fg / term_cell_bg
 * 0.6.1 - @vasco - TermCursor.style; intern on style change
 * 0.7.0 - @vasco - drop term_feed; typed printable / utf8 / escape only
 * 0.7.1 - @vasco - intern scan; full table overwrites last id, not 0
 * 0.7.2 - @vasco - intern palette index; term_cell_style resolves; term_colors_set
 * 0.7.3 - @vasco - bulk printable row store
 * 0.7.4 - @vasco - combining run + glyph id on cell
 * 0.7.5 - @vasco - row-ring origin; full-screen scroll is origin++
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(TERM_DEBUG)
#define TASSERT_N(_1, _2, N, ...) N
#define TASSERT(...) TASSERT_N(__VA_ARGS__, TASSERT2, TASSERT1)(__VA_ARGS__)
#define TASSERT1(a) assert(a)
#define TASSERT2(a, s) assert((a) && (s))
#else
#define TASSERT(...) ((void)0)
#endif

#define TERM_TODO \
    do { \
        fprintf(stderr, "TERM TODO: %s() in %s:%d\n", __func__, __FILE__, __LINE__); \
        abort(); \
    } while (0)

#define TERM_MIN(a, b) ((a) < (b) ? (a) : (b))
#define TERM_MAX(a, b) ((a) > (b) ? (a) : (b))
#define TERM_BETWEEN(x, a, b) (((unsigned)((x) - (a))) <= (unsigned)((b) - (a)))
#define TERM_DEFAULT(a, b) ((a) = (a) ? (a) : (b))

#define TERM_UTF_INVALID 0xFFFDu

#define TERM_MODE_WRAP      (1u << 0)
#define TERM_MODE_INSERT    (1u << 1)
#define TERM_MODE_ALTSCREEN (1u << 2)
#define TERM_MODE_CRLF      (1u << 3)
#define TERM_MODE_ECHO      (1u << 4)
#define TERM_MODE_PRINT     (1u << 5)
#define TERM_MODE_UTF8      (1u << 6)
#define TERM_MODE_HIDE      (1u << 7)
#define TERM_MODE_MOUSEBTN  (1u << 8)
#define TERM_MODE_MOUSEMOT  (1u << 9)
#define TERM_MODE_MOUSEMANY (1u << 10)
#define TERM_MODE_MOUSESGR  (1u << 11)
#define TERM_MODE_BRKTPASTE (1u << 12)
#define TERM_MODE_MOUSE     (TERM_MODE_MOUSEBTN | TERM_MODE_MOUSEMOT | TERM_MODE_MOUSEMANY)
#define TERM_WRAPNEXT       1u

#define TERM_ESC_START      1u
#define TERM_ESC_CSI        2u
#define TERM_ESC_STR        4u
#define TERM_ESC_ALTCHARSET 8u
#define TERM_ESC_STR_END    16u
#define TERM_ESC_TEST       32u
#define TERM_ESC_UTF8       64u

#define TERM_ATTR_NONE      0u
#define TERM_ATTR_BOLD      (1u << 0)
#define TERM_ATTR_FAINT     (1u << 1)
#define TERM_ATTR_ITALIC    (1u << 2)
#define TERM_ATTR_UNDERLINE (1u << 3)
#define TERM_ATTR_BLINK     (1u << 4)
#define TERM_ATTR_REVERSE   (1u << 5)
#define TERM_ATTR_INVISIBLE (1u << 6)
#define TERM_ATTR_STRUCK    (1u << 7)

#define TERM_COLOR_DEF 0u
#define TERM_COLOR_PAL 1u
#define TERM_COLOR_RGB 2u

#define TERM_ESC_ARG_SIZ 16
#define TERM_CSI_BUF_SIZ 256
#define TERM_HIST_MAX    1024
#define TERM_CELL_CODE   0u
#define TERM_SEQ_MAX     8u

typedef struct TermColors {
    uint32_t fg[16];
    uint32_t bg[8];
    uint32_t fg_default;
    uint32_t bg_default;
} TermColors;

typedef struct TermCursor {
    uint32_t fg;
    uint32_t bg;
    uint32_t x;
    uint32_t y;
    uint8_t attr;
    uint8_t state;
    uint8_t fg_kind;
    uint8_t bg_kind;
    uint8_t fg_idx;
    uint8_t bg_idx;
    uint16_t style;
} TermCursor;

typedef struct TermStyle {
    uint32_t fg;
    uint32_t bg;
    uint8_t fg_kind;
    uint8_t bg_kind;
    uint8_t fg_idx;
    uint8_t bg_idx;
    uint8_t attr;
} TermStyle;

typedef struct TermCell {
    uint32_t codepoint;
    uint16_t style;
    uint16_t tag;
    uint32_t glyph;
} TermCell;

typedef struct TermScreen {
    TermCell *cell_buffer;
    uint32_t cols;
    uint32_t rows;
    uint32_t capacity;
    uint32_t origin; /* physical row of logical y=0 */
} TermScreen;

typedef struct TermCsi {
    char buf[TERM_CSI_BUF_SIZ];
    int32_t arg[TERM_ESC_ARG_SIZ];
    uint32_t len;
    int32_t narg;
    char priv;
    char mode[2];
} TermCsi;

typedef struct TermStr {
    uint32_t len;
    char type;
} TermStr;

typedef struct Term {
    TermScreen screen;
    TermScreen alt;
    TermCursor cursor;
    TermCursor saved;
    TermColors colors;
    TermCsi csi;
    TermStr str;
    uint32_t top;
    uint32_t bot;
    uint32_t mode;
    uint32_t state;
    uint32_t utf8_acc;
    uint32_t utf8_min;
    uint32_t last_ch;
    uint8_t utf8_rem;
    char reply[256];
    uint32_t reply_n;
    char str_buf[128];
    TermCell *hist;
    uint32_t hist_cap;
    uint32_t hist_n;
    uint32_t hist_i;
    uint32_t hist_cols;
    TermStyle *styles;
    uint16_t *style_hash;
    uint32_t style_n;
    uint32_t style_cap;
    uint32_t style_hash_n;
    int cells_owned;
    uint32_t seq[TERM_SEQ_MAX];
    uint32_t seq_n;
    uint32_t seq_i;
    uint8_t cs_g0;
    uint8_t cs_g1;
    uint8_t cs_gl;
    uint8_t cs_sel;
} Term;

int  term_init(Term *t, uint32_t cols, uint32_t rows, const TermColors *colors);
int  term_init_on(Term *t, uint32_t cols, uint32_t rows, const TermColors *colors, TermCell *screen, TermCell *alt, uint32_t cap);
void term_destroy(Term *t);
void term_resize(Term *t, uint32_t cols, uint32_t rows);
void term_resize_on(Term *t, uint32_t cols, uint32_t rows, TermCell *screen, TermCell *alt, uint32_t cap);
/* Term only accepts specific types of input. */
void term_feed_printable(Term *t, const char *bytes, size_t len); /* 0x20-0x7E */
void term_feed_utf8(Term *t, const char *bytes, size_t len); /* complete UTF-8, high bytes */
void term_feed_escape(Term *t, const char *bytes, size_t len); /* C0 / ESC / CSI / OSC; 7-bit */
TermScreen *term_screen(Term *t);
TermCell *term_row(TermScreen *s, uint32_t y); // logical row, contiguous cols
TermCell *term_cell_at(TermScreen *s, uint32_t x, uint32_t y);
uint32_t term_hist_count(const Term *t);
const TermCell *term_hist_line(const Term *t, uint32_t back);
TermStyle term_cell_style(const Term *t, const TermCell *c);
TermStyle term_cursor_style(const Term *t);
void term_colors_set(Term *t, const TermColors *colors);

#endif /* TERM_H */

/*
------------------------------------------------------------------------------
MIT License
Copyright (c) 2026 Vasco Alves
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
*/
