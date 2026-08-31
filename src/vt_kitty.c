#pragma once

/*
 * Kitty graphics is APC (ESC _ G … ST). Classify emits VT_RUN_KITTY; drain
 * never feeds Term. Session lives on the bound pane so mux icat does not
 * clobber. Term str_buf is 128 bytes. Payload is base64 PNG (f=100).
 * Stamp color glyphs when the atlas exists. a=q replies on the PTY before DA.
 */

enum {
	VT_KITTY_MAX = 8u * 1024u * 1024u,
	VT_KITTY_PUA = 0xE000u,
	VT_KITTY_PUA_N = 0x1900u
};

static u32 vt_kitty_tile;

static void vt_kitty_reset(void);
static int vt_kitty_append(const char *p, u32 n);
static void vt_kitty_slot_copy(u32 slot, const u8 *src, int src_w, int src_h,
		u32 cols, u32 rows, u32 cx, u32 cy);
static void vt_kitty_stamp(Term *t, const u8 *rgba, int w, int h, u32 cols,
		u32 rows, int no_cursor);
static void vt_kitty_finish(Term *t, int action, u32 cols, u32 rows, int no_cursor);
static void vt_kitty_reply(u32 id, const char *msg);
static u32 vt_kitty_one(const char *p, u32 n);
static void vt_kitty(const char *p, u32 n);

void
vt_kitty_reset(void)
{
	vt_kitty_p->b64_n = 0;
	vt_kitty_p->id = 0;
	vt_kitty_p->action = 0;
	vt_kitty_p->cols = 0;
	vt_kitty_p->rows = 0;
	vt_kitty_p->no_cursor = 0;
}

int
vt_kitty_append(const char *p, u32 n)
{
	VtKitty *k;
	u32 cap;
	char *nbuf;

	k = vt_kitty_p;
	if (!n)
		return 1;
	if (k->b64_n + n > VT_KITTY_MAX)
		return 0;
	if (k->b64_n + n > k->b64_cap) {
		cap = k->b64_cap ? k->b64_cap * 2u : 4096u;
		while (cap < k->b64_n + n)
			cap *= 2u;
		if (cap > VT_KITTY_MAX)
			cap = VT_KITTY_MAX;
		nbuf = realloc(k->b64, cap);
		if (!nbuf)
			return 0;
		k->b64 = nbuf;
		k->b64_cap = cap;
	}
	memcpy(k->b64 + k->b64_n, p, n);
	k->b64_n += n;
	return 1;
}

void
vt_kitty_slot_copy(u32 slot, const u8 *src, int src_w, int src_h, u32 cols,
		u32 rows, u32 cx, u32 cy)
{
	u32 cw;
	u32 ch;
	u32 atlas_w;
	u32 cell_x;
	u32 cell_y;
	u32 dest_w;
	u32 dest_h;
	u32 py;
	u32 px;

	glyph_slot_clear(slot);
	cw = atlas.cell_width;
	ch = atlas.cell_height;
	if (!cw || !ch || !src || src_w <= 0 || src_h <= 0 || !cols || !rows)
		return;
	dest_w = cols * cw;
	dest_h = rows * ch;
	atlas_w = atlas.slot_width * atlas.cols;
	cell_x = (slot % atlas.cols) * atlas.slot_width;
	cell_y = (slot / atlas.cols) * ch;
	for (py = 0; py < ch; py++) {
		for (px = 0; px < cw; px++) {
			u8 *dst;
			u32 dx;
			u32 dy;
			u32 sx;
			u32 sy;
			const u8 *sp;

			dst = atlas.atlas + ((cell_y + py) * atlas_w + cell_x + px) * 4u;
			dx = cx * cw + px;
			dy = cy * ch + py;
			sx = (u32)((u64)dx * (u32)src_w / dest_w);
			sy = (u32)((u64)dy * (u32)src_h / dest_h);
			if (sx >= (u32)src_w)
				sx = (u32)src_w - 1;
			if (sy >= (u32)src_h)
				sy = (u32)src_h - 1;
			sp = src + (sy * (u32)src_w + sx) * 4u;
			dst[0] = sp[0];
			dst[1] = sp[1];
			dst[2] = sp[2];
			dst[3] = sp[3];
		}
	}
	glyph_atlas_dirty = true;
}

void
vt_kitty_stamp(Term *t, const u8 *rgba, int w, int h, u32 cols, u32 rows,
		int no_cursor)
{
	TermScreen *s;
	u32 cw;
	u32 ch;
	u32 cx;
	u32 cy;
	u32 x0;
	u32 y0;

	if (!t || !atlas.atlas || !rgba || w <= 0 || h <= 0)
		return;
	cw = atlas.cell_width;
	ch = atlas.cell_height;
	if (!cw || !ch)
		return;
	s = term_screen(t);
	if (!s || !s->cell_buffer || !s->cols || !s->rows)
		return;
	x0 = t->cursor.x;
	y0 = t->cursor.y;
	if (x0 >= s->cols)
		x0 = s->cols - 1;
	if (y0 >= s->rows)
		y0 = s->rows - 1;
	if (!cols)
		cols = ((u32)w + cw - 1) / cw;
	if (!rows)
		rows = ((u32)h + ch - 1) / ch;
	if (!cols)
		cols = 1;
	if (!rows)
		rows = 1;
	if (cols > s->cols)
		cols = s->cols;
	if (rows > s->rows)
		rows = s->rows;
	if (!no_cursor)
		x0 = 0;
	else if (x0 + cols > s->cols) {
		term_feed_escape(t, "\033E", 2);
		s = term_screen(t);
		x0 = 0;
		y0 = t->cursor.y;
		if (y0 >= s->rows)
			y0 = s->rows - 1;
	}
	if (y0 + rows > s->rows) {
		u32 extra;
		u32 i;

		extra = y0 + rows - s->rows;
		t->cursor.y = s->rows - 1;
		for (i = 0; i < extra; i++)
			term_feed_escape(t, "\033D", 2);
		y0 = s->rows - rows;
	}
	for (cy = 0; cy < rows; cy++) {
		u32 gy;

		gy = y0 + cy;
		for (cx = 0; cx < cols; cx++) {
			u32 gx;
			u32 slot;
			codepoint_t cp;
			TermCell *cell;

			gx = x0 + cx;
			slot = vt_lru_alloc(&glyph_lru);
			if (slot == VT_LRU_NONE)
				return;
			vt_kitty_slot_copy(slot, rgba, w, h, cols, rows, cx, cy);
			cp = (codepoint_t)(VT_KITTY_PUA + (vt_kitty_tile % VT_KITTY_PUA_N));
			vt_kitty_tile++;
			vt_lru_put(&glyph_lru, cp, slot, 0, 1);
			cell = &s->cell_buffer[gy * s->cols + gx];
			cell->codepoint = cp;
			cell->style = t->cursor.style;
		}
	}
	if (no_cursor) {
		t->cursor.x = x0;
		t->cursor.y = y0;
		t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
		return;
	}
	t->cursor.x = 0;
	t->cursor.y = y0 + rows - 1;
	t->cursor.state &= (uint8_t)~TERM_WRAPNEXT;
	term_feed_escape(t, "\033E", 2);
}

void
vt_kitty_finish(Term *t, int action, u32 cols, u32 rows, int no_cursor)
{
	VtKitty *k;
	char *bin;
	u8 *rgba;
	size_t dn;
	int w;
	int h;
	int n;

	k = vt_kitty_p;
	if (!k->b64_n || action != 'T') {
		vt_kitty_reset();
		return;
	}
	bin = malloc(k->b64_n);
	if (!bin) {
		vt_kitty_reset();
		return;
	}
	dn = vt_base64_decode(k->b64, k->b64_n, bin, k->b64_n);
	rgba = stbi_load_from_memory((const u8 *)bin, (int)dn, &w, &h, &n, 4);
	free(bin);
	vt_kitty_reset();
	if (!rgba)
		return;
	vt_kitty_stamp(t, rgba, w, h, cols, rows, no_cursor);
	stbi_image_free(rgba);
}

void
vt_kitty_reply(u32 id, const char *msg)
{
	char buf[96];
	int n;

	if (!id || !msg)
		return;
	n = snprintf(buf, sizeof buf, "\033_Gi=%u;%s\033\\", id, msg);
	if (n > 0 && (size_t)n < sizeof buf)
		vt_sh_write(buf, (size_t)n);
}

u32
vt_kitty_one(const char *p, u32 n)
{
	u32 i;
	u32 end;
	u32 ctrl;
	u32 ctrln;
	u32 pay;
	u32 payn;
	int action;
	u32 id;
	u32 more;
	u32 cols;
	u32 rows;
	int no_cursor;
	int action_set;
	int medium;
	u32 quiet;

	if (n < 4 || (unsigned char)p[0] != 0x1b || p[1] != '_' || p[2] != 'G')
		return 0;
	i = 3;
	while (i < n && (unsigned char)p[i] != 0x07
			&& !((unsigned char)p[i] == 0x1b && i + 1 < n && p[i + 1] == '\\'))
		i++;
	if (i >= n)
		return 0;
	if ((unsigned char)p[i] == 0x07)
		end = i + 1;
	else
		end = i + 2;
	ctrl = 3;
	ctrln = 0;
	pay = 3;
	payn = 0;
	i = 3;
	while (i < n && (unsigned char)p[i] != 0x07
			&& !((unsigned char)p[i] == 0x1b && i + 1 < n && p[i + 1] == '\\')) {
		if (p[i] == ';' && !ctrln) {
			ctrln = i - ctrl;
			pay = i + 1;
		}
		i++;
	}
	if (!ctrln) {
		ctrln = i - ctrl;
		pay = i;
		payn = 0;
	} else {
		payn = i - pay;
	}
	action = 0;
	id = 0;
	more = 0;
	cols = 0;
	rows = 0;
	no_cursor = 0;
	action_set = 0;
	medium = 0;
	quiet = 0;
	i = 0;
	while (i < ctrln) {
		char key;
		u32 vs;
		u32 ve;
		u32 v;

		key = p[ctrl + i];
		i++;
		if (i < ctrln && p[ctrl + i] == '=')
			i++;
		vs = i;
		while (i < ctrln && p[ctrl + i] != ',')
			i++;
		ve = i;
		if (i < ctrln && p[ctrl + i] == ',')
			i++;
		if (key == 'a' && ve > vs) {
			action = (int)(unsigned char)p[ctrl + vs];
			action_set = 1;
		} else if (key == 'C' && ve > vs)
			no_cursor = p[ctrl + vs] == '1';
		else if (key == 't' && ve > vs)
			medium = (int)(unsigned char)p[ctrl + vs];
		else if (key == 'q') {
			v = 0;
			while (vs < ve && p[ctrl + vs] >= '0' && p[ctrl + vs] <= '9') {
				v = v * 10u + (u32)(p[ctrl + vs] - '0');
				vs++;
			}
			quiet = v;
		} else if (key == 'm') {
			v = 0;
			while (vs < ve && p[ctrl + vs] >= '0' && p[ctrl + vs] <= '9') {
				v = v * 10u + (u32)(p[ctrl + vs] - '0');
				vs++;
			}
			more = v;
		} else {
			v = 0;
			while (vs < ve && p[ctrl + vs] >= '0' && p[ctrl + vs] <= '9') {
				if (v < 100000000u)
					v = v * 10u + (u32)(p[ctrl + vs] - '0');
				vs++;
			}
			if (key == 'i')
				id = v;
			else if (key == 'c')
				cols = v;
			else if (key == 'r')
				rows = v;
		}
	}
	if (!medium)
		medium = 'd';
	if (action == 'd') {
		vt_kitty_reset();
		return end;
	}
	if (action == 'q') {
		if (quiet != 2u) {
			if (medium == 'd')
				vt_kitty_reply(id, "OK");
			else
				vt_kitty_reply(id, "EINVAL: only direct");
		}
		return end;
	}
	if (medium != 'd') {
		if (id && quiet != 2u)
			vt_kitty_reply(id, "EINVAL: only direct");
		vt_kitty_reset();
		return end;
	}
	if (id && vt_kitty_p->id && id != vt_kitty_p->id)
		vt_kitty_reset();
	if (id)
		vt_kitty_p->id = id;
	if (action_set)
		vt_kitty_p->action = action;
	if (cols)
		vt_kitty_p->cols = cols;
	if (rows)
		vt_kitty_p->rows = rows;
	if (no_cursor)
		vt_kitty_p->no_cursor = 1;
	if (!vt_kitty_append(p + pay, payn)) {
		vt_kitty_reset();
		return end;
	}
	if (!more)
		vt_kitty_finish(vt_term_p, vt_kitty_p->action, vt_kitty_p->cols,
			vt_kitty_p->rows, vt_kitty_p->no_cursor);
	return end;
}

void
vt_kitty(const char *p, u32 n)
{
	u32 off;

	off = 0;
	while (off < n) {
		u32 used;

		used = vt_kitty_one(p + off, n - off);
		if (!used)
			break;
		off += used;
	}
}
