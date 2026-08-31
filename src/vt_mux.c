#pragma once

/*
 * Pane tree only. No sessions, no windows, no detach.
 * Prefix + chords: config.h. Arrows focus. Prefix again sends the Ctrl letter.
 * Middle-drag a live pane: edge drop splits dest, center swaps.
 * Drop on another vt: adopt+SCM_RIGHTS (X11 one-shot; else offer two-click).
 * ctl: split / focus / panes / move / adopt / give. Dump and write stay on the focused pane.
 */

enum {
	VT_PANE_MAX = 8,
	VT_NODE_MAX = 15,
	VT_SPLIT_LEAF = 0,
	VT_SPLIT_H = 1,
	VT_SPLIT_V = 2,
	VT_MUX_WALL_MAX = 512 * 256,
	VT_MUX_ARM_N = 1,
	VT_MUX_ARM_E = 2,
	VT_MUX_ARM_S = 4,
	VT_MUX_ARM_W = 8
};

typedef struct VtKitty {
	char *b64;
	u32 b64_n;
	u32 b64_cap;
	u32 id;
	u32 cols;
	u32 rows;
	int action;
	int no_cursor;
} VtKitty;

typedef struct VtPane {
	Term term;
	VtRing ring;
	PeakProc sh;
	VtKitty kitty;
	u32 x;
	u32 y;
	u32 cols;
	u32 rows;
	int used;
} VtPane;

typedef struct VtNode {
	u8 split;
	u16 a;
	u16 b;
	u16 pane;
	u16 ratio;
} VtNode;

static VtPane vt_panes[VT_PANE_MAX];
static VtNode vt_mux_nodes[VT_NODE_MAX];
static u8 vt_mux_node_used[VT_NODE_MAX];
static u8 vt_mux_wall[VT_MUX_WALL_MAX];
static TermColors vt_mux_colors;
static Term *vt_term_p;
static VtRing *vt_ring_p;
static PeakProc *vt_sh_p;
static VtKitty *vt_kitty_p;
static u32 vt_focus;
static u32 vt_cur;
static u32 vt_mux_cols;
static u32 vt_mux_rows;
#ifndef VT_HEADLESS
static int vt_mux_prefix;
static int vt_mux_drag = -1;
static int vt_mux_hover = -1;
static int vt_mux_drop_dir;
static int vt_mux_drop_first;
static int vt_mux_click_paste;
#endif

#define vt_term (*vt_term_p)
#define vt_ring (*vt_ring_p)
#define vt_sh (*vt_sh_p)

static const codepoint_t vt_mux_box[16] = {
	0,
	0x2502, 0x2500, 0x2514,
	0x2502, 0x2502, 0x250C, 0x251C,
	0x2500, 0x2518, 0x2500, 0x2534,
	0x2510, 0x2524, 0x252C, 0x253C
};

static void vt_mux_bind(u32 i);
static void vt_mux_reset(void);
static int vt_mux_open(u32 i, u32 cols, u32 rows, const TermColors *colors);
static void vt_mux_close(u32 i);
static void vt_mux_destroy(void);
static u16 vt_mux_node_alloc(void);
static void vt_mux_node_free(u16 i);
static u16 vt_mux_leaf_of(u32 pane);
static u16 vt_mux_parent_of(u16 ni);
static void vt_mux_layout_node(u16 ni, u32 x, u32 y, u32 cols, u32 rows);
static void vt_mux_layout(u32 cols, u32 rows);
static u32 vt_mux_first(void);
static void vt_mux_focus(u32 i);
#ifndef VT_HEADLESS
static void vt_mux_focus_next(void);
static void vt_mux_focus_dir(int dx, int dy);
#endif
static int vt_mux_split(int dir);
static void vt_mux_attach_side(int *dir, int *first);
static int vt_mux_attach(PeakProc proc, int dir, int first);
static void vt_mux_handoff(u32 i);
#ifndef VT_HEADLESS
static PEAK_HANDLE vt_mux_connect_pid(int pid);
static int vt_mux_sock_line(PEAK_HANDLE fd, char *dst, size_t cap);
static int vt_mux_export(u32 src, int pid);
static int vt_mux_pull(const char *path, int pane);
static int vt_mux_find_hit(void);
static int vt_mux_offer_path(char *dst, size_t cap);
static void vt_mux_offer_write(u32 pane);
static void vt_mux_offer_clear(void);
static int vt_mux_offer_take(void);
#endif
static void vt_mux_collapse(u16 leaf);
static int vt_mux_move(u32 src, u32 dst, int dir, int first);
static void vt_mux_kill(u32 i);
static u32 vt_mux_fds(PEAK_HANDLE *fds);
#ifndef VT_HEADLESS
static void vt_mux_resize(u32 cols, u32 rows);
static int vt_mux_single(void);
static int vt_mux_pick(u32 x, u32 y, u32 *lx, u32 *ly);
static void vt_mux_drag_side(u32 i, u32 x, u32 y, int *dir, int *first);
static void vt_mux_drag_over(u32 x, u32 y);
static int vt_mux_pointer(u32 x, u32 y, PeakPointerState st, PeakKeyMod mod);
static int vt_mux_ch_hit(const char *s, PeakKeyCode key, u32 ch);
static int vt_mux_chord(PeakKeyCode want, PeakKeyMod want_mod, PeakKeyCode key, PeakKeyMod mod);
static int vt_mux_key(PeakKeyCode key, PeakKeyMod mod, u32 code);
static u32 vt_mux_fill_walls(VtInstance *inst, u32 n, u32 cap);
static u32 vt_mux_fill_drop(VtInstance *inst, u32 n, u32 cap);
static void vt_mux_present(void);
#endif

void
vt_mux_bind(u32 i)
{
	vt_cur = i;
	vt_term_p = &vt_panes[i].term;
	vt_ring_p = &vt_panes[i].ring;
	vt_sh_p = &vt_panes[i].sh;
	vt_kitty_p = &vt_panes[i].kitty;
}

void
vt_mux_reset(void)
{
	u32 i;

	memset(vt_panes, 0, sizeof vt_panes);
	memset(vt_mux_nodes, 0, sizeof vt_mux_nodes);
	memset(vt_mux_node_used, 0, sizeof vt_mux_node_used);
	for (i = 0; i < VT_PANE_MAX; i++) {
		vt_panes[i].sh.fd = PEAK_HANDLE_INVALID;
		vt_panes[i].sh.pid = 0;
	}
	vt_mux_node_used[0] = 1;
	vt_mux_nodes[0].split = VT_SPLIT_LEAF;
	vt_mux_nodes[0].pane = 0;
	vt_mux_nodes[0].ratio = 500;
	vt_focus = 0;
	vt_mux_cols = 0;
	vt_mux_rows = 0;
#ifndef VT_HEADLESS
	vt_mux_prefix = 0;
	vt_mux_drag = -1;
	vt_mux_hover = -1;
	vt_mux_click_paste = 0;
#endif
	vt_mux_bind(0);
}

int
vt_mux_open(u32 i, u32 cols, u32 rows, const TermColors *colors)
{
	VtPane *p;

	if (i >= VT_PANE_MAX || vt_panes[i].used)
		return 0;
	p = &vt_panes[i];
	memset(p, 0, sizeof *p);
	p->sh.fd = PEAK_HANDLE_INVALID;
	p->sh.pid = 0;
	p->cols = cols;
	p->rows = rows;
	if (colors)
		vt_mux_colors = *colors;
	if (!vt_ring_init(&p->ring, VT_RING_PAGES))
		return 0;
	if (!term_init(&p->term, cols, rows, &vt_mux_colors)) {
		vt_ring_destroy(&p->ring);
		return 0;
	}
	p->used = 1;
	return 1;
}

void
vt_mux_close(u32 i)
{
	VtPane *p;

	if (i >= VT_PANE_MAX || !vt_panes[i].used)
		return;
	p = &vt_panes[i];
	if (p->sh.pid)
		VTINFO("[Shell %-d] Exited successfully", p->sh.pid);
	peak_pty_close(&p->sh);
	term_destroy(&p->term);
	vt_ring_destroy(&p->ring);
	free(p->kitty.b64);
	memset(&p->kitty, 0, sizeof p->kitty);
	p->used = 0;
	p->sh.fd = PEAK_HANDLE_INVALID;
	p->sh.pid = 0;
}

void
vt_mux_destroy(void)
{
	u32 i;

	for (i = 0; i < VT_PANE_MAX; i++)
		vt_mux_close(i);
}

u16
vt_mux_node_alloc(void)
{
	u16 i;

	for (i = 0; i < VT_NODE_MAX; i++) {
		if (!vt_mux_node_used[i]) {
			vt_mux_node_used[i] = 1;
			memset(&vt_mux_nodes[i], 0, sizeof vt_mux_nodes[i]);
			vt_mux_nodes[i].ratio = 500;
			return i;
		}
	}
	return 0xffff;
}

void
vt_mux_node_free(u16 i)
{
	if (i >= VT_NODE_MAX)
		return;
	vt_mux_node_used[i] = 0;
}

u16
vt_mux_leaf_of(u32 pane)
{
	u16 i;

	for (i = 0; i < VT_NODE_MAX; i++) {
		if (vt_mux_node_used[i] && vt_mux_nodes[i].split == VT_SPLIT_LEAF
				&& vt_mux_nodes[i].pane == pane)
			return i;
	}
	return 0xffff;
}

u16
vt_mux_parent_of(u16 ni)
{
	u16 i;

	for (i = 0; i < VT_NODE_MAX; i++) {
		if (vt_mux_node_used[i] && vt_mux_nodes[i].split
				&& (vt_mux_nodes[i].a == ni || vt_mux_nodes[i].b == ni))
			return i;
	}
	return 0xffff;
}

void
vt_mux_layout_node(u16 ni, u32 x, u32 y, u32 cols, u32 rows)
{
	VtNode *n;
	VtPane *p;
	u32 a;
	u32 b;

	if (ni >= VT_NODE_MAX || !vt_mux_node_used[ni] || !cols || !rows)
		return;
	n = &vt_mux_nodes[ni];
	if (n->split == VT_SPLIT_LEAF) {
		if (n->pane >= VT_PANE_MAX || !vt_panes[n->pane].used)
			return;
		p = &vt_panes[n->pane];
		p->x = x;
		p->y = y;
		if (p->cols != cols || p->rows != rows) {
			term_resize(&p->term, cols, rows);
			if (p->sh.fd != PEAK_HANDLE_INVALID)
				peak_pty_resize(&p->sh, cols, rows,
					cols * atlas.cell_width, rows * atlas.cell_height);
		}
		p->cols = cols;
		p->rows = rows;
		return;
	}
	if (n->split == VT_SPLIT_V) {
		if (cols < 5)
			return;
		a = cols * (u32)n->ratio / 1000u;
		if (a < 2)
			a = 2;
		if (a + 3 > cols)
			a = cols - 3;
		b = cols - a - 1;
		if (vt_mux_cols && vt_mux_rows && (x + a) < vt_mux_cols) {
			u32 wy;

			for (wy = 0; wy < rows && y + wy < vt_mux_rows; wy++)
				vt_mux_wall[(y + wy) * vt_mux_cols + (x + a)] |= (u8)(VT_MUX_ARM_N | VT_MUX_ARM_S);
		}
		vt_mux_layout_node(n->a, x, y, a, rows);
		vt_mux_layout_node(n->b, x + a + 1, y, b, rows);
		return;
	}
	if (rows < 3)
		return;
	a = rows * (u32)n->ratio / 1000u;
	if (a < 1)
		a = 1;
	if (a + 2 > rows)
		a = rows - 2;
	b = rows - a - 1;
	if (vt_mux_cols && vt_mux_rows && (y + a) < vt_mux_rows) {
		u32 wx;

		for (wx = 0; wx < cols && x + wx < vt_mux_cols; wx++)
			vt_mux_wall[(y + a) * vt_mux_cols + (x + wx)] |= (u8)(VT_MUX_ARM_E | VT_MUX_ARM_W);
	}
	vt_mux_layout_node(n->a, x, y, cols, a);
	vt_mux_layout_node(n->b, x, y + a + 1, cols, b);
}

void
vt_mux_layout(u32 cols, u32 rows)
{
	vt_mux_cols = cols;
	vt_mux_rows = rows;
	if (cols * rows <= VT_MUX_WALL_MAX)
		memset(vt_mux_wall, 0, (size_t)cols * rows);
	vt_mux_layout_node(0, 0, 0, cols, rows);
	redraw = true;
}

#ifndef VT_HEADLESS
void
vt_mux_resize(u32 cols, u32 rows)
{
	if (!cols || !rows)
		return;
	if (cols == vt_mux_cols && rows == vt_mux_rows)
		return;
	vt_mux_layout(cols, rows);
}
#endif

u32
vt_mux_first(void)
{
	u32 i;

	for (i = 0; i < VT_PANE_MAX; i++) {
		if (vt_panes[i].used)
			return i;
	}
	return 0;
}

void
vt_mux_focus(u32 i)
{
	if (i >= VT_PANE_MAX || !vt_panes[i].used)
		return;
	if (vt_focus != i)
		vt_sel_on = 0;
	vt_focus = i;
	vt_mux_bind(i);
	redraw = true;
}

#ifndef VT_HEADLESS
void
vt_mux_focus_next(void)
{
	u32 i;

	i = vt_focus;
	do {
		i = (i + 1) % VT_PANE_MAX;
		if (vt_panes[i].used) {
			vt_mux_focus(i);
			return;
		}
	} while (i != vt_focus);
}

void
vt_mux_focus_dir(int dx, int dy)
{
	u32 i;
	u32 best;
	i32 best_d;
	i32 fx;
	i32 fy;

	if (!vt_panes[vt_focus].used)
		return;
	fx = (i32)(vt_panes[vt_focus].x + vt_panes[vt_focus].cols / 2);
	fy = (i32)(vt_panes[vt_focus].y + vt_panes[vt_focus].rows / 2);
	best = vt_focus;
	best_d = 0x7fffffff;
	for (i = 0; i < VT_PANE_MAX; i++) {
		VtPane *p;
		i32 px;
		i32 py;
		i32 dot;
		i32 d;

		if (!vt_panes[i].used || i == vt_focus)
			continue;
		p = &vt_panes[i];
		px = (i32)(p->x + p->cols / 2) - fx;
		py = (i32)(p->y + p->rows / 2) - fy;
		dot = px * dx + py * dy;
		if (dot <= 0)
			continue;
		d = px * px + py * py;
		if (d < best_d) {
			best_d = d;
			best = i;
		}
	}
	vt_mux_focus(best);
}
#endif

int
vt_mux_split(int dir)
{
	u32 old;
	u32 neu;
	u16 leaf;
	u16 na;
	u16 nb;
	VtPane *src;
	u32 cols;
	u32 rows;
	PeakProc proc;
	static const char *argv[] = { "bash", "--login", NULL };

	if (dir != VT_SPLIT_H && dir != VT_SPLIT_V)
		return 0;
	old = vt_focus;
	if (!vt_panes[old].used)
		return 0;
	src = &vt_panes[old];
	cols = src->cols;
	rows = src->rows;
	if (dir == VT_SPLIT_V && cols < 5)
		return 0;
	if (dir == VT_SPLIT_H && rows < 3)
		return 0;
	neu = VT_PANE_MAX;
	{
		u32 i;

		for (i = 0; i < VT_PANE_MAX; i++) {
			if (!vt_panes[i].used) {
				neu = i;
				break;
			}
		}
	}
	if (neu >= VT_PANE_MAX)
		return 0;
	leaf = vt_mux_leaf_of(old);
	if (leaf == 0xffff)
		return 0;
	na = vt_mux_node_alloc();
	nb = vt_mux_node_alloc();
	if (na == 0xffff || nb == 0xffff) {
		vt_mux_node_free(na);
		vt_mux_node_free(nb);
		return 0;
	}
	if (!vt_mux_open(neu, 2, 1, &vt_mux_colors)) {
		vt_mux_node_free(na);
		vt_mux_node_free(nb);
		return 0;
	}
	vt_mux_nodes[leaf].split = (u8)dir;
	vt_mux_nodes[leaf].ratio = 500;
	vt_mux_nodes[leaf].a = na;
	vt_mux_nodes[leaf].b = nb;
	vt_mux_nodes[na].split = VT_SPLIT_LEAF;
	vt_mux_nodes[na].pane = (u16)old;
	vt_mux_nodes[nb].split = VT_SPLIT_LEAF;
	vt_mux_nodes[nb].pane = (u16)neu;
	vt_mux_layout(vt_mux_cols ? vt_mux_cols : cols, vt_mux_rows ? vt_mux_rows : rows);
	{
		VtPane *np;

		np = &vt_panes[neu];
		proc = peak_pty_spawn("bash", argv, np->cols, np->rows,
			np->cols * atlas.cell_width, np->rows * atlas.cell_height);
	}
	if (proc.fd == PEAK_HANDLE_INVALID) {
		vt_mux_kill(neu);
		return 0;
	}
	vt_panes[neu].sh = proc;
	vt_mux_focus(neu);
	return 1;
}

void
vt_mux_attach_side(int *dir, int *first)
{
	if (!dir || !first)
		return;
	*dir = VT_SPLIT_V;
	*first = 0;
#ifndef VT_HEADLESS
	{
		int px;
		int py;
		u32 cx;
		u32 cy;
		int hit;

		if (!peak_pointer_local(&win, &px, &py))
			return;
		vt_cell_at((float)px, (float)py, &cx, &cy);
		hit = vt_mux_pick(cx, cy, NULL, NULL);
		if (hit < 0)
			return;
		vt_mux_focus((u32)hit);
		vt_mux_drag_side((u32)hit, cx, cy, dir, first);
		if (*dir != VT_SPLIT_H && *dir != VT_SPLIT_V) {
			*dir = VT_SPLIT_V;
			*first = 0;
		}
	}
#endif
}

int
vt_mux_attach(PeakProc proc, int dir, int first)
{
	u32 old;
	u32 neu;
	u16 leaf;
	u16 na;
	u16 nb;
	VtPane *src;
	u32 cols;
	u32 rows;

	if (proc.fd == PEAK_HANDLE_INVALID)
		return 0;
	if (dir != VT_SPLIT_H && dir != VT_SPLIT_V)
		dir = VT_SPLIT_V;
	old = vt_focus;
	if (!vt_panes[old].used)
		return 0;
	src = &vt_panes[old];
	cols = src->cols;
	rows = src->rows;
	neu = VT_PANE_MAX;
	{
		u32 i;

		for (i = 0; i < VT_PANE_MAX; i++) {
			if (!vt_panes[i].used) {
				neu = i;
				break;
			}
		}
	}
	if (neu >= VT_PANE_MAX)
		return 0;
	leaf = vt_mux_leaf_of(old);
	if (leaf == 0xffff)
		return 0;
	na = vt_mux_node_alloc();
	nb = vt_mux_node_alloc();
	if (na == 0xffff || nb == 0xffff) {
		vt_mux_node_free(na);
		vt_mux_node_free(nb);
		return 0;
	}
	if (!vt_mux_open(neu, 2, 1, &vt_mux_colors)) {
		vt_mux_node_free(na);
		vt_mux_node_free(nb);
		return 0;
	}
	vt_mux_nodes[leaf].split = (u8)dir;
	vt_mux_nodes[leaf].ratio = 500;
	vt_mux_nodes[leaf].a = na;
	vt_mux_nodes[leaf].b = nb;
	vt_mux_nodes[na].split = VT_SPLIT_LEAF;
	vt_mux_nodes[nb].split = VT_SPLIT_LEAF;
	if (first) {
		vt_mux_nodes[na].pane = (u16)neu;
		vt_mux_nodes[nb].pane = (u16)old;
	} else {
		vt_mux_nodes[na].pane = (u16)old;
		vt_mux_nodes[nb].pane = (u16)neu;
	}
	vt_mux_layout(vt_mux_cols ? vt_mux_cols : cols, vt_mux_rows ? vt_mux_rows : rows);
	vt_panes[neu].sh = proc;
#ifndef VT_HEADLESS
	peak_pty_resize(&vt_panes[neu].sh, vt_panes[neu].cols, vt_panes[neu].rows,
		vt_panes[neu].cols * atlas.cell_width, vt_panes[neu].rows * atlas.cell_height);
#endif
	vt_mux_focus(neu);
	return 1;
}

void
vt_mux_handoff(u32 i)
{
	VtPane *p;

	if (i >= VT_PANE_MAX || !vt_panes[i].used)
		return;
	p = &vt_panes[i];
	if (p->sh.fd != PEAK_HANDLE_INVALID) {
		peak_fd_close(p->sh.fd);
		p->sh.fd = PEAK_HANDLE_INVALID;
	}
	p->sh.pid = 0;
	vt_mux_kill(i);
}

#ifndef VT_HEADLESS
PEAK_HANDLE
vt_mux_connect_pid(int pid)
{
	char dir[192];
	char path[256];
	PEAK_HANDLE sock;
	int n;

	if (pid <= 0 || pid == peak_pid())
		return PEAK_HANDLE_INVALID;
	if (!peak_runtime_dir(dir, sizeof dir, "vt"))
		return PEAK_HANDLE_INVALID;
	n = snprintf(path, sizeof path, "%s/%d.sock", dir, pid);
	if (n < 0 || (size_t)n >= sizeof path)
		return PEAK_HANDLE_INVALID;
	sock = peak_sock_connect(path);
	if (sock == PEAK_HANDLE_INVALID)
		peak_filesystem_rm(path);
	return sock;
}

int
vt_mux_sock_line(PEAK_HANDLE fd, char *dst, size_t cap)
{
	size_t n;
	int spins;

	n = 0;
	spins = 0;
	if (fd == PEAK_HANDLE_INVALID || !dst || cap < 2)
		return 0;
	while (n + 1 < cap && spins < 80) {
		int r;

		r = peak_fd_read(fd, dst + n, 1);
		if (r > 0) {
			if (dst[n] == '\n') {
				dst[n] = 0;
				return 1;
			}
			n++;
			continue;
		}
		if (r == 0)
			return 0;
		peak_wait(NULL, &fd, 1, 25);
		spins++;
	}
	return 0;
}

int
vt_mux_export(u32 src, int pid)
{
	char line[160];
	char reply[256];
	VtPane *p;
	PEAK_HANDLE sock;
	int n;

	if (src >= VT_PANE_MAX || !vt_panes[src].used || pid <= 0)
		return 0;
	p = &vt_panes[src];
	if (p->sh.fd == PEAK_HANDLE_INVALID)
		return 0;
	sock = vt_mux_connect_pid(pid);
	if (sock == PEAK_HANDLE_INVALID)
		return 0;
	n = snprintf(line, sizeof line, "{\"op\":\"adopt\",\"n\":%d}\n", p->sh.pid);
	if (n < 0 || (size_t)n >= sizeof line
			|| !peak_sock_send(sock, line, (size_t)n, p->sh.fd)) {
		peak_fd_close(sock);
		return 0;
	}
	reply[0] = 0;
	if (!vt_mux_sock_line(sock, reply, sizeof reply) || !strstr(reply, "\"ok\":true")) {
		peak_fd_close(sock);
		return 0;
	}
	peak_fd_close(sock);
	vt_mux_handoff(src);
	vt_mux_offer_clear();
	return 1;
}

int
vt_mux_pull(const char *path, int pane)
{
	char line[80];
	char reply[256];
	PEAK_HANDLE sock;
	PEAK_HANDLE pass;
	PeakProc proc;
	int n;
	int i;
	int r;
	int spins;
	int pid;
	int dir;
	int first;

	if (!path || !path[0] || !peak_file_exists(path))
		return 0;
	sock = peak_sock_connect(path);
	if (sock == PEAK_HANDLE_INVALID)
		return 0;
	if (pane >= 0)
		n = snprintf(line, sizeof line, "{\"op\":\"give\",\"n\":%d}\n", pane);
	else
		n = snprintf(line, sizeof line, "{\"op\":\"give\"}\n");
	if (n < 0 || (size_t)n >= sizeof line || peak_fd_write(sock, line, (size_t)n) <= 0) {
		peak_fd_close(sock);
		return 0;
	}
	reply[0] = 0;
	if (!vt_mux_sock_line(sock, reply, sizeof reply) || !strstr(reply, "\"ok\":true")) {
		peak_fd_close(sock);
		return 0;
	}
	n = 0;
	pid = 0;
	{
		const char *s;

		s = strstr(reply, "\"n\":");
		if (s)
			n = atoi(s + 4);
		s = strstr(reply, "\"pid\":");
		if (s)
			pid = atoi(s + 6);
	}
	if (n <= 0) {
		peak_fd_close(sock);
		return 0;
	}
	for (i = 0; i < n; i++) {
		spins = 0;
		pass = PEAK_HANDLE_INVALID;
		for (;;) {
			r = peak_sock_recv(sock, line, 1, &pass);
			if (r > 0 && pass != PEAK_HANDLE_INVALID)
				break;
			if (r == 0) {
				peak_fd_close(sock);
				return 0;
			}
			if (spins++ > 80) {
				peak_fd_close(sock);
				return 0;
			}
			peak_wait(NULL, &sock, 1, 25);
		}
		proc.fd = pass;
		proc.pid = n == 1 ? pid : 0;
		vt_mux_attach_side(&dir, &first);
		if (!vt_mux_attach(proc, dir, first)) {
			peak_fd_close(pass);
			peak_fd_close(sock);
			return 0;
		}
	}
	peak_fd_close(sock);
	return 1;
}

typedef struct {
	int self;
	int hit;
} VtMuxHit;

static int
vt_mux_hit_name(const char *name, void *ud)
{
	VtMuxHit *h;
	char reply[256];
	PEAK_HANDLE sock;
	int pid;
	int n;
	int i;

	h = ud;
	n = (int)strlen(name);
	if (n < 6 || memcmp(name + n - 5, ".sock", 5) != 0)
		return 1;
	if (!strcmp(name, "latest.sock"))
		return 1;
	pid = 0;
	for (i = 0; i < n - 5; i++) {
		if (name[i] < '0' || name[i] > '9')
			return 1;
		pid = pid * 10 + (name[i] - '0');
	}
	if (pid <= 0 || pid == h->self)
		return 1;
	sock = vt_mux_connect_pid(pid);
	if (sock == PEAK_HANDLE_INVALID)
		return 1;
	if (peak_fd_write(sock, "{\"op\":\"hit\"}\n", sizeof "{\"op\":\"hit\"}\n" - 1) <= 0) {
		peak_fd_close(sock);
		return 1;
	}
	reply[0] = 0;
	if (vt_mux_sock_line(sock, reply, sizeof reply) && strstr(reply, "\"hit\":1"))
		h->hit = pid;
	peak_fd_close(sock);
	return h->hit ? 0 : 1;
}

int
vt_mux_find_hit(void)
{
	char dir[192];
	PEAK_HANDLE sock;
	VtMuxHit h;
	int pid;

	h.self = peak_pid();
	h.hit = 0;
	pid = peak_pointer_pid(&win);
	if (pid > 0 && pid != h.self) {
		sock = vt_mux_connect_pid(pid);
		if (sock != PEAK_HANDLE_INVALID) {
			peak_fd_close(sock);
			return pid;
		}
	}
	if (!peak_runtime_dir(dir, sizeof dir, "vt"))
		return 0;
	peak_filesystem_list(dir, vt_mux_hit_name, &h);
	return h.hit;
}

int
vt_mux_offer_path(char *dst, size_t cap)
{
	char dir[192];
	int n;

	if (!dst || cap < 8 || !peak_runtime_dir(dir, sizeof dir, "vt"))
		return 0;
	n = snprintf(dst, cap, "%s/offer", dir);
	return n > 0 && (size_t)n < cap;
}

void
vt_mux_offer_write(u32 pane)
{
	char path[256];
	char buf[64];
	int n;

	if (!vt_mux_offer_path(path, sizeof path))
		return;
	n = snprintf(buf, sizeof buf, "%d %u\n", peak_pid(), pane);
	if (n > 0 && (size_t)n < sizeof buf)
		peak_file_write(path, buf, (size_t)n);
}

void
vt_mux_offer_clear(void)
{
	char path[256];

	if (vt_mux_offer_path(path, sizeof path))
		peak_filesystem_rm(path);
}

int
vt_mux_offer_take(void)
{
	char path[256];
	char sock[256];
	char dir[192];
	unsigned long n;
	char *p;
	int pid;
	int pane;
	int i;

	if (!vt_mux_offer_path(path, sizeof path))
		return 0;
	p = peak_file_alloc(path, &n);
	if (!p || !n) {
		free(p);
		return 0;
	}
	pid = 0;
	pane = -1;
	i = 0;
	while (i < (int)n && p[i] >= '0' && p[i] <= '9') {
		pid = pid * 10 + (p[i] - '0');
		i++;
	}
	while (i < (int)n && p[i] == ' ')
		i++;
	pane = 0;
	{
		int any;

		any = 0;
		while (i < (int)n && p[i] >= '0' && p[i] <= '9') {
			pane = pane * 10 + (p[i] - '0');
			i++;
			any = 1;
		}
		if (!any)
			pane = -1;
	}
	free(p);
	if (pid <= 0 || pid == peak_pid() || pane < 0)
		return 0;
	if (!peak_runtime_dir(dir, sizeof dir, "vt"))
		return 0;
	i = snprintf(sock, sizeof sock, "%s/%d.sock", dir, pid);
	if (i < 0 || (size_t)i >= sizeof sock)
		return 0;
	if (!vt_mux_pull(sock, pane))
		return 0;
	vt_mux_offer_clear();
	return 1;
}
#endif

void
vt_mux_collapse(u16 leaf)
{
	u16 parent;
	u16 sib;

	parent = vt_mux_parent_of(leaf);
	if (parent == 0xffff)
		return;
	sib = vt_mux_nodes[parent].a == leaf ? vt_mux_nodes[parent].b : vt_mux_nodes[parent].a;
	vt_mux_nodes[parent] = vt_mux_nodes[sib];
	vt_mux_node_free(sib);
	vt_mux_node_free(leaf);
}

int
vt_mux_move(u32 src, u32 dst, int dir, int first)
{
	u16 sl;
	u16 dl;
	u16 na;
	u16 nb;
	u16 tmp;

	if (src >= VT_PANE_MAX || dst >= VT_PANE_MAX || src == dst)
		return 0;
	if (!vt_panes[src].used || !vt_panes[dst].used)
		return 0;
	sl = vt_mux_leaf_of(src);
	dl = vt_mux_leaf_of(dst);
	if (sl == 0xffff || dl == 0xffff)
		return 0;
	if (!dir) {
		tmp = vt_mux_nodes[sl].pane;
		vt_mux_nodes[sl].pane = vt_mux_nodes[dl].pane;
		vt_mux_nodes[dl].pane = tmp;
		if (vt_mux_cols && vt_mux_rows)
			vt_mux_layout(vt_mux_cols, vt_mux_rows);
		vt_mux_focus(src);
		return 1;
	}
	if (dir != VT_SPLIT_H && dir != VT_SPLIT_V)
		return 0;
	if (vt_mux_parent_of(sl) == 0xffff)
		return 0;
	na = vt_mux_node_alloc();
	nb = vt_mux_node_alloc();
	if (na == 0xffff || nb == 0xffff) {
		vt_mux_node_free(na);
		vt_mux_node_free(nb);
		return 0;
	}
	vt_mux_collapse(sl);
	dl = vt_mux_leaf_of(dst);
	if (dl == 0xffff) {
		vt_mux_node_free(na);
		vt_mux_node_free(nb);
		return 0;
	}
	vt_mux_nodes[dl].split = (u8)dir;
	vt_mux_nodes[dl].ratio = 500;
	vt_mux_nodes[dl].a = na;
	vt_mux_nodes[dl].b = nb;
	vt_mux_nodes[na].split = VT_SPLIT_LEAF;
	vt_mux_nodes[nb].split = VT_SPLIT_LEAF;
	if (first) {
		vt_mux_nodes[na].pane = (u16)src;
		vt_mux_nodes[nb].pane = (u16)dst;
	} else {
		vt_mux_nodes[na].pane = (u16)dst;
		vt_mux_nodes[nb].pane = (u16)src;
	}
	if (vt_mux_cols && vt_mux_rows)
		vt_mux_layout(vt_mux_cols, vt_mux_rows);
	vt_mux_focus(src);
	return 1;
}

void
vt_mux_kill(u32 i)
{
	u16 leaf;
	u32 nused;
	u32 k;

	if (i >= VT_PANE_MAX || !vt_panes[i].used)
		return;
#ifndef VT_HEADLESS
	if (vt_mux_drag >= 0 && (u32)vt_mux_drag == i) {
		vt_mux_drag = -1;
		vt_mux_hover = -1;
	}
#endif
	nused = 0;
	for (k = 0; k < VT_PANE_MAX; k++) {
		if (vt_panes[k].used)
			nused++;
	}
	leaf = vt_mux_leaf_of(i);
	vt_mux_close(i);
	if (nused <= 1) {
		running = false;
		return;
	}
	if (leaf != 0xffff)
		vt_mux_collapse(leaf);
	if (!vt_panes[vt_focus].used)
		vt_mux_focus(vt_mux_first());
	if (vt_mux_cols && vt_mux_rows)
		vt_mux_layout(vt_mux_cols, vt_mux_rows);
}

#ifndef VT_HEADLESS
int
vt_mux_single(void)
{
	u32 i;
	u32 n;

	n = 0;
	for (i = 0; i < VT_PANE_MAX; i++) {
		if (vt_panes[i].used)
			n++;
	}
	return n <= 1;
}

int
vt_mux_pick(u32 x, u32 y, u32 *lx, u32 *ly)
{
	u32 i;

	for (i = 0; i < VT_PANE_MAX; i++) {
		VtPane *p;

		if (!vt_panes[i].used)
			continue;
		p = &vt_panes[i];
		if (x >= p->x && x < p->x + p->cols && y >= p->y && y < p->y + p->rows) {
			if (lx)
				*lx = x - p->x;
			if (ly)
				*ly = y - p->y;
			return (int)i;
		}
	}
	return -1;
}

void
vt_mux_drag_side(u32 i, u32 x, u32 y, int *dir, int *first)
{
	VtPane *p;
	u32 lx;
	u32 ly;
	u32 dl;
	u32 dr;
	u32 dt;
	u32 db;
	u32 m;
	u32 band;

	p = &vt_panes[i];
	lx = x - p->x;
	ly = y - p->y;
	dl = lx;
	dr = p->cols ? p->cols - 1u - lx : 0;
	dt = ly;
	db = p->rows ? p->rows - 1u - ly : 0;
	band = p->cols < p->rows ? p->cols / 4u : p->rows / 4u;
	if (band < 1)
		band = 1;
	*dir = VT_SPLIT_V;
	*first = 1;
	m = dl;
	if (dr < m) {
		m = dr;
		*first = 0;
	}
	if (dt < m) {
		m = dt;
		*dir = VT_SPLIT_H;
		*first = 1;
	}
	if (db < m) {
		m = db;
		*dir = VT_SPLIT_H;
		*first = 0;
	}
	if (m > band)
		*dir = 0;
}

void
vt_mux_drag_over(u32 x, u32 y)
{
	int hit;

	hit = vt_mux_pick(x, y, NULL, NULL);
	if (hit < 0 || hit == vt_mux_drag) {
		vt_mux_hover = -1;
		return;
	}
	vt_mux_hover = hit;
	vt_mux_drag_side((u32)hit, x, y, &vt_mux_drop_dir, &vt_mux_drop_first);
}

int
vt_mux_pointer(u32 x, u32 y, PeakPointerState st, PeakKeyMod mod)
{
	int hit;

	if (st == PEAK_POINTER_PRESSED) {
		if (mod & (PEAK_KEYMOD_SHIFT | PEAK_KEYMOD_CTRL | PEAK_KEYMOD_ALT | PEAK_KEYMOD_SUPER))
			return 0;
		hit = vt_mux_pick(x, y, NULL, NULL);
		if (hit < 0)
			return 0;
		vt_mux_focus((u32)hit);
		if (vt_mux_offer_take()) {
			vt_sel_on = 0;
			vt_sel_drag = 0;
			return 1;
		}
		vt_mux_drag = hit;
		vt_mux_hover = -1;
		vt_mux_click_paste = 0;
		vt_mux_offer_write((u32)hit);
		vt_sel_on = 0;
		vt_sel_drag = 0;
		return 1;
	}
	if (vt_mux_drag < 0)
		return 0;
	if (st == PEAK_POINTER_MOVED) {
		int px;
		int py;

		if (!peak_pointer_local(&win, &px, &py)) {
			vt_mux_hover = -1;
			return 1;
		}
		vt_mux_drag_over(x, y);
		return 1;
	}
	if (st == PEAK_POINTER_RELEASED) {
		int src;
		int pid;
		int px;
		int py;
		int local;

		src = vt_mux_drag;
		vt_mux_drag = -1;
		local = peak_pointer_local(&win, &px, &py);
		vt_mux_hover = -1;
		if (src >= 0) {
			pid = vt_mux_find_hit();
			VTINFO("mux drop pid=%d local=%d", pid, local);
			if (vt_mux_export((u32)src, pid))
				return 1;
		}
		if (src >= 0 && local) {
			u32 cx;
			u32 cy;

			vt_cell_at((float)px, (float)py, &cx, &cy);
			vt_mux_drag_over(cx, cy);
			if (vt_mux_hover >= 0) {
				vt_mux_move((u32)src, (u32)vt_mux_hover, vt_mux_drop_dir, vt_mux_drop_first);
				vt_mux_offer_clear();
				vt_mux_hover = -1;
				return 1;
			}
			vt_mux_click_paste = 1;
		}
		vt_mux_hover = -1;
		return 1;
	}
	return 0;
}

int
vt_mux_ch_hit(const char *s, PeakKeyCode key, u32 ch)
{
	for (; *s; s++) {
		u32 c;

		c = (u32)(unsigned char)*s;
		if (c >= 'A' && c <= 'Z')
			c += 32;
		if (ch && ch == c)
			return 1;
		if (c >= 'a' && c <= 'z' && key == (PeakKeyCode)(PEAK_KEY_A + (c - 'a')))
			return 1;
		if (c >= '0' && c <= '9' && key == (PeakKeyCode)(PEAK_KEY_0 + (c - '0')))
			return 1;
	}
	return 0;
}

int
vt_mux_chord(PeakKeyCode want, PeakKeyMod want_mod, PeakKeyCode key, PeakKeyMod mod)
{
	return key == want && ((mod & ~PEAK_KEYMOD_CAPS) == want_mod);
}

int
vt_mux_key(PeakKeyCode key, PeakKeyMod mod, u32 code)
{
	u32 ch;

	ch = code;
	if (ch >= 'A' && ch <= 'Z')
		ch += 32;
	if (vt_mux_prefix) {
		/* Shift/Ctrl KEY_DOWN is a separate event. Do not eat prefix. */
		if (key == PEAK_KEY_UNKNOWN && ch < 32u)
			return 1;
		vt_mux_prefix = 0;
		if (key == PEAK_KEY_ESCAPE)
			return 1;
		if (vt_mux_chord(mux_prefix_key, mux_prefix_mod, key, mod)) {
			if ((mux_prefix_mod & PEAK_KEYMOD_CTRL)
					&& mux_prefix_key >= PEAK_KEY_A
					&& mux_prefix_key <= PEAK_KEY_Z) {
				char b;

				b = (char)(1 + (mux_prefix_key - PEAK_KEY_A));
				vt_sh_write(&b, 1);
			}
			return 1;
		}
		if (vt_mux_ch_hit(mux_split_v, key, ch)) {
			vt_mux_split(VT_SPLIT_V);
			return 1;
		}
		if (vt_mux_ch_hit(mux_split_h, key, ch)) {
			vt_mux_split(VT_SPLIT_H);
			return 1;
		}
		if (key == PEAK_KEY_LEFT || vt_mux_ch_hit(mux_left, key, ch)) {
			vt_mux_focus_dir(-1, 0);
			return 1;
		}
		if (key == PEAK_KEY_RIGHT || vt_mux_ch_hit(mux_right, key, ch)) {
			vt_mux_focus_dir(1, 0);
			return 1;
		}
		if (key == PEAK_KEY_UP || vt_mux_ch_hit(mux_up, key, ch)) {
			vt_mux_focus_dir(0, -1);
			return 1;
		}
		if (key == PEAK_KEY_DOWN || vt_mux_ch_hit(mux_down, key, ch)) {
			vt_mux_focus_dir(0, 1);
			return 1;
		}
		if (vt_mux_ch_hit(mux_next, key, ch)) {
			vt_mux_focus_next();
			return 1;
		}
		if (vt_mux_ch_hit(mux_kill, key, ch)) {
			vt_mux_kill(vt_focus);
			return 1;
		}
		return 1;
	}
	if (vt_mux_chord(mux_prefix_key, mux_prefix_mod, key, mod)) {
		vt_mux_prefix = 1;
		return 1;
	}
	return 0;
}
#endif

u32
vt_mux_fds(PEAK_HANDLE *fds)
{
	u32 n;
	u32 i;

	n = 0;
	for (i = 0; i < VT_PANE_MAX; i++) {
		if (vt_panes[i].used && vt_panes[i].sh.fd != PEAK_HANDLE_INVALID)
			fds[n++] = vt_panes[i].sh.fd;
	}
	return n;
}

#ifndef VT_HEADLESS
u32
vt_mux_fill_walls(VtInstance *inst, u32 n, u32 cap)
{
	u32 x;
	u32 y;
	color_packed_t dim;
	color_packed_t lit;
	VtPane *f;

	if (!vt_mux_cols || !vt_mux_rows || vt_mux_cols * vt_mux_rows > VT_MUX_WALL_MAX)
		return n;
	dim = (color_packed_t)ansi_fg[8] << 8;
	lit = (color_packed_t)ansi_fg[15] << 8;
	f = vt_panes[vt_focus].used ? &vt_panes[vt_focus] : NULL;
	for (y = 0; y < vt_mux_rows; y++) {
		for (x = 0; x < vt_mux_cols; x++) {
			u8 arm;
			codepoint_t cp;
			color_packed_t fg;
			int hot;

			arm = vt_mux_wall[y * vt_mux_cols + x] & 15u;
			if (!arm)
				continue;
			cp = vt_mux_box[arm];
			if (!cp)
				continue;
			hot = 0;
			if (f) {
				if (x + 1 == f->x && y >= f->y && y < f->y + f->rows)
					hot = 1;
				if (x == f->x + f->cols && y >= f->y && y < f->y + f->rows)
					hot = 1;
				if (y + 1 == f->y && x >= f->x && x < f->x + f->cols)
					hot = 1;
				if (y == f->y + f->rows && x >= f->x && x < f->x + f->cols)
					hot = 1;
			}
			fg = hot ? lit : dim;
			n = renderer_fill_cp(x, y, cp, fg, (color_packed_t)ansi_bg[bg_color] << 8, inst, n, cap);
		}
	}
	return n;
}

u32
vt_mux_fill_drop(VtInstance *inst, u32 n, u32 cap)
{
	VtPane *p;
	color_packed_t fg;
	color_packed_t bg;
	u32 x;
	u32 y;
	u32 a;

	if (vt_mux_drag < 0 || vt_mux_hover < 0)
		return n;
	p = &vt_panes[vt_mux_hover];
	if (!p->used || !p->cols || !p->rows)
		return n;
	fg = (color_packed_t)ansi_fg[11] << 8;
	bg = (color_packed_t)ansi_bg[bg_color] << 8;
	if (!vt_mux_drop_dir) {
		for (x = p->x; x < p->x + p->cols; x++) {
			n = renderer_fill_cp(x, p->y, 0x2500, fg, bg, inst, n, cap);
			n = renderer_fill_cp(x, p->y + p->rows - 1, 0x2500, fg, bg, inst, n, cap);
		}
		for (y = p->y; y < p->y + p->rows; y++) {
			n = renderer_fill_cp(p->x, y, 0x2502, fg, bg, inst, n, cap);
			n = renderer_fill_cp(p->x + p->cols - 1, y, 0x2502, fg, bg, inst, n, cap);
		}
		return n;
	}
	if (vt_mux_drop_dir == VT_SPLIT_V) {
		a = p->x + (vt_mux_drop_first ? p->cols / 4u : (p->cols * 3u) / 4u);
		if (a >= p->x + p->cols)
			a = p->x + p->cols - 1;
		for (y = p->y; y < p->y + p->rows; y++)
			n = renderer_fill_cp(a, y, 0x2502, fg, bg, inst, n, cap);
		return n;
	}
	a = p->y + (vt_mux_drop_first ? p->rows / 4u : (p->rows * 3u) / 4u);
	if (a >= p->y + p->rows)
		a = p->y + p->rows - 1;
	for (x = p->x; x < p->x + p->cols; x++)
		n = renderer_fill_cp(x, a, 0x2500, fg, bg, inst, n, cap);
	return n;
}

void
vt_mux_present(void)
{
	VtInstance *inst;
	u32 n;
	u32 i;
	u32 cap;

	inst = rend_buffer_mapped(&renderer.instance);
	if (!inst)
		return;
	cap = renderer_ninst;
	n = 0;
	for (i = 0; i < VT_PANE_MAX; i++) {
		VtPane *p;
		TermScreen *s;
		u32 a;
		u32 b;
		u32 sel0;
		u32 sel1;
		int cur;
		int sel;

		if (!vt_panes[i].used)
			continue;
		p = &vt_panes[i];
		s = term_screen(&p->term);
		if (!s || !s->cell_buffer)
			continue;
		cur = (i == vt_focus) && !(p->term.mode & TERM_MODE_HIDE);
		sel = (i == vt_focus) && vt_sel_on;
		a = vt_sel_ay * s->cols + vt_sel_ax;
		b = vt_sel_by * s->cols + vt_sel_bx;
		sel0 = a < b ? a : b;
		sel1 = a < b ? b : a;
		n = renderer_fill(&p->term, s, p->x, p->y, p->term.cursor.x, p->term.cursor.y, cur,
			(p->term.cursor.fg << 8) | p->term.cursor.attr, p->term.cursor.bg << 8,
			sel, sel0, sel1, inst, n, cap);
	}
	n = vt_mux_fill_walls(inst, n, cap);
	n = vt_mux_fill_drop(inst, n, cap);
	renderer_flush(n);
}
#endif
