#pragma once

void
mux_bind(VtMultiplexor *m, u32 i)
{
	m->pane = &m->panes[i];
}

void
mux_reset(VtMultiplexor *m)
{
	u32 i;

	memset(m->panes, 0, sizeof m->panes);
	memset(m->mux_nodes, 0, sizeof m->mux_nodes);
	memset(m->node_used, 0, sizeof m->node_used);
	for (i = 0; i < VT_PANE_MAX; i++) {
		m->panes[i].sh.fd = PEAK_HANDLE_INVALID;
		m->panes[i].sh.pid = 0;
	}
	m->node_used[0] = 1;
	m->mux_nodes[0].split = VT_SPLIT_LEAF;
	m->mux_nodes[0].pane = 0;
	m->mux_nodes[0].ratio = 500;
	m->focus = 0;
	m->cols = 0;
	m->rows = 0;
	m->prefix = 0;
	m->drag = -1;
	m->os_src = -1;
	m->hover = -1;
	m->click_paste = 0;
	mux_bind(m, 0);
}

int
mux_open(VtMultiplexor *m, u32 i, u32 cols, u32 rows, const TermColors *colors, Renderer *renderer)
{
	VtPane *p;
	RingBufferArgs args;
	void *mem;

	if (i >= VT_PANE_MAX || m->panes[i].used)
		return 0;
	p = &m->panes[i];
	memset(p, 0, sizeof *p);
	p->sh.fd = PEAK_HANDLE_INVALID;
	p->sh.pid = 0;
	p->cols = cols;
	p->rows = rows;
	if (colors)
		m->mux_colors = *colors;
	args.line_max = VT_LINE_MAX;
	args.run_max = RUN_MAX;
	args.pages = VT_RING_PAGES;
	mem = malloc(ringbuffer_size(args));
	if (!mem)
		return 0;
	p->rb = ringbuffer_create(args, mem);
	if (!p->rb) {
		free(mem);
		return 0;
	}
	if (!term_init(&p->term, cols, rows, &m->mux_colors)) {
		ringbuffer_destroy(p->rb);
		free(p->rb);
		p->rb = NULL;
		return 0;
	}
	(void)renderer;
	p->used = 1;
	return 1;
}

void
mux_close(VtMultiplexor *m, u32 i)
{
	VtPane *p;

	if (i >= VT_PANE_MAX || !m->panes[i].used)
		return;
	p = &m->panes[i];
	if (p->sh.pid)
		VTINFO("[Shell %-d] Exited successfully", p->sh.pid);
	vt_shell_close(&p->sh);
	term_destroy(&p->term);
	if (p->rb) {
		ringbuffer_destroy(p->rb);
		free(p->rb);
		p->rb = NULL;
	}
	free(p->kitty.b64);
	memset(&p->kitty, 0, sizeof p->kitty);
	p->used = 0;
	p->sh.fd = PEAK_HANDLE_INVALID;
	p->sh.pid = 0;
}

void
mux_destroy(VtMultiplexor *m)
{
	u32 i;

	for (i = 0; i < VT_PANE_MAX; i++)
		mux_close(m, i);
}

u16
mux_node_alloc(VtMultiplexor *m)
{
	u16 i;

	for (i = 0; i < VT_NODE_MAX; i++) {
		if (!m->node_used[i]) {
			m->node_used[i] = 1;
			memset(&m->mux_nodes[i], 0, sizeof m->mux_nodes[i]);
			m->mux_nodes[i].ratio = 500;
			return i;
		}
	}
	return 0xffff;
}

void
mux_node_free(VtMultiplexor *m, u16 i)
{
	if (i >= VT_NODE_MAX)
		return;
	m->node_used[i] = 0;
}

u16
mux_leaf_of(VtMultiplexor *m, u32 pane)
{
	u16 i;

	for (i = 0; i < VT_NODE_MAX; i++) {
		if (m->node_used[i] && m->mux_nodes[i].split == VT_SPLIT_LEAF
				&& m->mux_nodes[i].pane == pane)
			return i;
	}
	return 0xffff;
}

u16
mux_parent_of(VtMultiplexor *m, u16 ni)
{
	u16 i;

	for (i = 0; i < VT_NODE_MAX; i++) {
		if (m->node_used[i] && m->mux_nodes[i].split
				&& (m->mux_nodes[i].a == ni || m->mux_nodes[i].b == ni))
			return i;
	}
	return 0xffff;
}

void
mux_layout_node(VtMultiplexor *m, u16 ni, u32 x, u32 y, u32 cols, u32 rows, Renderer *renderer)
{
	VtNode *n;
	VtPane *p;
	u32 a;
	u32 b;

	if (ni >= VT_NODE_MAX || !m->node_used[ni] || !cols || !rows)
		return;
	n = &m->mux_nodes[ni];
	if (n->split == VT_SPLIT_LEAF) {
		if (n->pane >= VT_PANE_MAX || !m->panes[n->pane].used)
			return;
		p = &m->panes[n->pane];
		p->x = x;
		p->y = y;
		if (p->cols != cols || p->rows != rows) {
			term_resize(&p->term, cols, rows);
			if (p->sh.fd != PEAK_HANDLE_INVALID)
				vt_shell_resize(&p->sh, cols, rows,
					cols * (renderer ? renderer->atlas.cell_width : 0), rows * (renderer ? renderer->atlas.cell_height : 0));
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
		if (m->cols && m->rows && (x + a) < m->cols) {
			u32 wy;

			for (wy = 0; wy < rows && y + wy < m->rows; wy++)
				m->wall[(y + wy) * m->cols + (x + a)] |= (u8)(VT_MUX_ARM_N | VT_MUX_ARM_S);
		}
		mux_layout_node(m, n->a, x, y, a, rows, renderer);
		mux_layout_node(m, n->b, x + a + 1, y, b, rows, renderer);
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
	if (m->cols && m->rows && (y + a) < m->rows) {
		u32 wx;

		for (wx = 0; wx < cols && x + wx < m->cols; wx++)
			m->wall[(y + a) * m->cols + (x + wx)] |= (u8)(VT_MUX_ARM_E | VT_MUX_ARM_W);
	}
	mux_layout_node(m, n->a, x, y, cols, a, renderer);
	mux_layout_node(m, n->b, x, y + a + 1, cols, b, renderer);
}

void
mux_frame(VtMultiplexor *m)
{
	u32 x;
	u32 y;
	u32 cols;
	u32 rows;

	cols = m->cols;
	rows = m->rows;
	VTASSERT(cols >= 2 && rows >= 2);
	for (x = 1; x + 1 < cols; x++) {
		m->wall[x] |= (u8)(VT_MUX_ARM_E | VT_MUX_ARM_W);
		m->wall[(rows - 1) * cols + x] |= (u8)(VT_MUX_ARM_E | VT_MUX_ARM_W);
	}
	for (y = 1; y + 1 < rows; y++) {
		m->wall[y * cols] |= (u8)(VT_MUX_ARM_N | VT_MUX_ARM_S);
		m->wall[y * cols + cols - 1] |= (u8)(VT_MUX_ARM_N | VT_MUX_ARM_S);
	}
	m->wall[0] |= (u8)(VT_MUX_ARM_S | VT_MUX_ARM_E);
	m->wall[cols - 1] |= (u8)(VT_MUX_ARM_S | VT_MUX_ARM_W);
	m->wall[(rows - 1) * cols] |= (u8)(VT_MUX_ARM_N | VT_MUX_ARM_E);
	m->wall[(rows - 1) * cols + cols - 1] |= (u8)(VT_MUX_ARM_N | VT_MUX_ARM_W);
}

void
mux_stitch(VtMultiplexor *m)
{
	u32 x;
	u32 y;
	u32 cols;
	u32 rows;
	u8 *w;

	cols = m->cols;
	rows = m->rows;
	w = m->wall;
	for (y = 0; y < rows; y++) {
		for (x = 0; x < cols; x++) {
			u32 i;
			u8 a;

			i = y * cols + x;
			a = w[i] & 15u;
			if (a & (VT_MUX_ARM_N | VT_MUX_ARM_S)) {
				if (y > 0 && (w[i - cols] & (VT_MUX_ARM_E | VT_MUX_ARM_W)))
					w[i - cols] |= VT_MUX_ARM_S;
				if (y + 1 < rows && (w[i + cols] & (VT_MUX_ARM_E | VT_MUX_ARM_W)))
					w[i + cols] |= VT_MUX_ARM_N;
			}
			if (a & (VT_MUX_ARM_E | VT_MUX_ARM_W)) {
				if (x > 0 && (w[i - 1] & (VT_MUX_ARM_N | VT_MUX_ARM_S)))
					w[i - 1] |= VT_MUX_ARM_E;
				if (x + 1 < cols && (w[i + 1] & (VT_MUX_ARM_N | VT_MUX_ARM_S)))
					w[i + 1] |= VT_MUX_ARM_W;
			}
		}
	}
}

u32
mux_used(VtMultiplexor *m)
{
	u32 n;
	u32 i;

	n = 0;
	for (i = 0; i < VT_PANE_MAX; i++) {
		if (m->panes[i].used)
			n++;
	}
	return n;
}

void
mux_layout(VtMultiplexor *m, u32 cols, u32 rows, Renderer *renderer)
{
	u32 nused;
	u32 x;
	u32 y;
	u32 w;
	u32 h;

	m->cols = cols;
	m->rows = rows;
	if (cols * rows <= VT_MUX_WALL_MAX)
		memset(m->wall, 0, (size_t)cols * rows);
	nused = mux_used(m);
	x = 0;
	y = 0;
	w = cols;
	h = rows;
	if (nused > 1 && cols >= 4 && rows >= 4 && cols * rows <= VT_MUX_WALL_MAX) {
		mux_frame(m);
		x = 1;
		y = 1;
		w = cols - 2;
		h = rows - 2;
	}
	mux_layout_node(m, 0, x, y, w, h, renderer);
	if (nused > 1 && cols * rows <= VT_MUX_WALL_MAX)
		mux_stitch(m);
	redraw = true;
}

void
mux_resize(VtMultiplexor *m, u32 cols, u32 rows, Renderer *renderer)
{
	if (!cols || !rows)
		return;
	if (cols == m->cols && rows == m->rows)
		return;
	mux_layout(m, cols, rows, renderer);
}

u32
mux_first(VtMultiplexor *m)
{
	u32 i;

	for (i = 0; i < VT_PANE_MAX; i++) {
		if (m->panes[i].used)
			return i;
	}
	return 0;
}

void
mux_focus(VtMultiplexor *m, u32 i)
{
	if (i >= VT_PANE_MAX || !m->panes[i].used)
		return;
	if (m->focus != i)
		sel_on = 0;
	m->focus = i;
	mux_bind(m, i);
	redraw = true;
}

void
mux_focus_next(VtMultiplexor *m)
{
	u32 i;

	i = m->focus;
	do {
		i = (i + 1) % VT_PANE_MAX;
		if (m->panes[i].used) {
			mux_focus(m, i);
			return;
		}
	} while (i != m->focus);
}

void
mux_focus_dir(VtMultiplexor *m, int dx, int dy)
{
	u32 i;
	u32 best;
	i32 best_d;
	i32 fx;
	i32 fy;

	if (!m->panes[m->focus].used)
		return;
	fx = (i32)(m->panes[m->focus].x + m->panes[m->focus].cols / 2);
	fy = (i32)(m->panes[m->focus].y + m->panes[m->focus].rows / 2);
	best = m->focus;
	best_d = 0x7fffffff;
	for (i = 0; i < VT_PANE_MAX; i++) {
		VtPane *p;
		i32 px;
		i32 py;
		i32 dot;
		i32 d;

		if (!m->panes[i].used || i == m->focus)
			continue;
		p = &m->panes[i];
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
	mux_focus(m, best);
}

int
mux_split(VtMultiplexor *m, int dir, Renderer *renderer)
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

	if (dir != VT_SPLIT_H && dir != VT_SPLIT_V)
		return 0;
	old = m->focus;
	if (!m->panes[old].used)
		return 0;
	src = &m->panes[old];
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
			if (!m->panes[i].used) {
				neu = i;
				break;
			}
		}
	}
	if (neu >= VT_PANE_MAX)
		return 0;
	leaf = mux_leaf_of(m, old);
	if (leaf == 0xffff)
		return 0;
	na = mux_node_alloc(m);
	nb = mux_node_alloc(m);
	if (na == 0xffff || nb == 0xffff) {
		mux_node_free(m, na);
		mux_node_free(m, nb);
		return 0;
	}
	if (!mux_open(m, neu, 2, 1, &m->mux_colors, renderer)) {
		mux_node_free(m, na);
		mux_node_free(m, nb);
		return 0;
	}
	m->mux_nodes[leaf].split = (u8)dir;
	m->mux_nodes[leaf].ratio = 500;
	m->mux_nodes[leaf].a = na;
	m->mux_nodes[leaf].b = nb;
	m->mux_nodes[na].split = VT_SPLIT_LEAF;
	m->mux_nodes[na].pane = (u16)old;
	m->mux_nodes[nb].split = VT_SPLIT_LEAF;
	m->mux_nodes[nb].pane = (u16)neu;
	mux_layout(m, m->cols ? m->cols : cols, m->rows ? m->rows : rows, renderer);
	{
		VtPane *np;

		np = &m->panes[neu];
		proc = vt_shell_spawn(np->cols, np->rows,
			np->cols * (renderer ? renderer->atlas.cell_width : 0), np->rows * (renderer ? renderer->atlas.cell_height : 0));
	}
	if (proc.fd == PEAK_HANDLE_INVALID) {
		mux_kill_pane(m, neu, renderer);
		return 0;
	}
	m->panes[neu].sh = proc;
	
	mux_focus(m, neu);
	return 1;
}

void
mux_attach_side(VtMultiplexor *m, int *dir, int *first)
{
	if (!dir || !first)
		return;
	*dir = VT_SPLIT_V;
	*first = 0;
	{
		int px;
		int py;
		u32 cx;
		u32 cy;
		int hit;

		if (!peak_pointer_local(VT_PEAK_WIN, &px, &py))
			return;
		cell_at((float)px, (float)py, &cx, &cy);
		hit = mux_pick(m, cx, cy, NULL, NULL);
		if (hit < 0)
			return;
		mux_focus(m, (u32)hit);
		mux_drag_side(m, (u32)hit, cx, cy, dir, first);
		if (*dir != VT_SPLIT_H && *dir != VT_SPLIT_V) {
			*dir = VT_SPLIT_V;
			*first = 0;
		}
	}
}

int
mux_attach(VtMultiplexor *m, PeakProc proc, int dir, int first, Renderer *renderer)
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
	old = m->focus;
	if (!m->panes[old].used)
		return 0;
	src = &m->panes[old];
	cols = src->cols;
	rows = src->rows;
	neu = VT_PANE_MAX;
	{
		u32 i;

		for (i = 0; i < VT_PANE_MAX; i++) {
			if (!m->panes[i].used) {
				neu = i;
				break;
			}
		}
	}
	if (neu >= VT_PANE_MAX)
		return 0;
	leaf = mux_leaf_of(m, old);
	if (leaf == 0xffff)
		return 0;
	na = mux_node_alloc(m);
	nb = mux_node_alloc(m);
	if (na == 0xffff || nb == 0xffff) {
		mux_node_free(m, na);
		mux_node_free(m, nb);
		return 0;
	}
	if (!mux_open(m, neu, 2, 1, &m->mux_colors, renderer)) {
		mux_node_free(m, na);
		mux_node_free(m, nb);
		return 0;
	}
	m->mux_nodes[leaf].split = (u8)dir;
	m->mux_nodes[leaf].ratio = 500;
	m->mux_nodes[leaf].a = na;
	m->mux_nodes[leaf].b = nb;
	m->mux_nodes[na].split = VT_SPLIT_LEAF;
	m->mux_nodes[nb].split = VT_SPLIT_LEAF;
	if (first) {
		m->mux_nodes[na].pane = (u16)neu;
		m->mux_nodes[nb].pane = (u16)old;
	} else {
		m->mux_nodes[na].pane = (u16)old;
		m->mux_nodes[nb].pane = (u16)neu;
	}
	mux_layout(m, m->cols ? m->cols : cols, m->rows ? m->rows : rows, renderer);
	m->panes[neu].sh = proc;
	vt_shell_resize(&m->panes[neu].sh, m->panes[neu].cols, m->panes[neu].rows,
		m->panes[neu].cols * (renderer ? renderer->atlas.cell_width : 0), m->panes[neu].rows * (renderer ? renderer->atlas.cell_height : 0));
	mux_focus(m, neu);
	return 1;
}

void
mux_handoff(VtMultiplexor *m, u32 i, Renderer *renderer)
{
	VtPane *p;

	if (i >= VT_PANE_MAX || !m->panes[i].used)
		return;
	p = &m->panes[i];
	if (p->sh.fd != PEAK_HANDLE_INVALID) {
		peak_fd_close(p->sh.fd);
		p->sh.fd = PEAK_HANDLE_INVALID;
	}
	p->sh.pid = 0;
	mux_kill_pane(m, i, renderer);
}

PEAK_HANDLE
mux_connect_pid(int pid)
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
mux_sock_line(PEAK_HANDLE fd, char *dst, size_t cap)
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
mux_export(VtMultiplexor *m, u32 src, int pid, Renderer *renderer)
{
	char line[160];
	char reply[256];
	VtPane *p;
	PEAK_HANDLE sock;
	int n;

	if (src >= VT_PANE_MAX || !m->panes[src].used || pid <= 0)
		return 0;
	p = &m->panes[src];
	if (p->sh.fd == PEAK_HANDLE_INVALID)
		return 0;
	sock = mux_connect_pid(pid);
	if (sock == PEAK_HANDLE_INVALID)
		return 0;
	n = snprintf(line, sizeof line, "{\"op\":\"adopt\",\"n\":%d}\n", p->sh.pid);
	if (n < 0 || (size_t)n >= sizeof line
			|| !peak_sock_send(sock, line, (size_t)n, p->sh.fd)) {
		peak_fd_close(sock);
		return 0;
	}
	reply[0] = 0;
	if (!mux_sock_line(sock, reply, sizeof reply) || !strstr(reply, "\"ok\":true")) {
		peak_fd_close(sock);
		return 0;
	}
	peak_fd_close(sock);
	mux_handoff(m, src, renderer);
	mux_offer_clear();
	return 1;
}

int
mux_pull(VtMultiplexor *m, const char *path, int pane, Renderer *renderer)
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

	if (!path || !path[0])
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
	if (!mux_sock_line(sock, reply, sizeof reply) || !strstr(reply, "\"ok\":true")) {
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
		mux_attach_side(m, &dir, &first);
		if (!mux_attach(m, proc, dir, first, renderer)) {
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
mux_hit_name(const char *name, void *ud)
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
	sock = mux_connect_pid(pid);
	if (sock == PEAK_HANDLE_INVALID)
		return 1;
	if (peak_fd_write(sock, "{\"op\":\"hit\"}\n", sizeof "{\"op\":\"hit\"}\n" - 1) <= 0) {
		peak_fd_close(sock);
		return 1;
	}
	reply[0] = 0;
	if (mux_sock_line(sock, reply, sizeof reply) && strstr(reply, "\"hit\":1"))
		h->hit = pid;
	peak_fd_close(sock);
	return h->hit ? 0 : 1;
}

int
mux_find_hit(void)
{
	char dir[192];
	PEAK_HANDLE sock;
	VtMuxHit h;
	int pid;

	h.self = peak_pid();
	h.hit = 0;
	pid = peak_pointer_pid(VT_PEAK_WIN);
	if (pid > 0 && pid != h.self) {
		sock = mux_connect_pid(pid);
		if (sock != PEAK_HANDLE_INVALID) {
			peak_fd_close(sock);
			return pid;
		}
	}
	if (!peak_runtime_dir(dir, sizeof dir, "vt"))
		return 0;
	peak_filesystem_list(dir, mux_hit_name, &h);
	return h.hit;
}

int
mux_offer_path(char *dst, size_t cap)
{
	char dir[192];
	int n;

	if (!dst || cap < 8 || !peak_runtime_dir(dir, sizeof dir, "vt"))
		return 0;
	n = snprintf(dst, cap, "%s/offer", dir);
	return n > 0 && (size_t)n < cap;
}

void
mux_offer_write(u32 pane)
{
	char path[256];
	char buf[64];
	int n;

	if (!mux_offer_path(path, sizeof path))
		return;
	n = snprintf(buf, sizeof buf, "%d %u\n", peak_pid(), pane);
	if (n > 0 && (size_t)n < sizeof buf)
		peak_file_write(path, buf, (size_t)n);
}

void
mux_offer_clear(void)
{
	char path[256];

	if (mux_offer_path(path, sizeof path))
		peak_filesystem_rm(path);
}

int
mux_offer_take(VtMultiplexor *m, Renderer *renderer)
{
	char path[256];
	char sock[256];
	char dir[192];
	unsigned long n;
	char *p;
	int pid;
	int pane;
	int i;

	if (!mux_offer_path(path, sizeof path))
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
	if (!mux_pull(m, sock, pane, renderer))
		return 0;
	mux_offer_clear();
	return 1;
}

void
mux_collapse(VtMultiplexor *m, u16 leaf)
{
	u16 parent;
	u16 sib;

	parent = mux_parent_of(m, leaf);
	if (parent == 0xffff)
		return;
	sib = m->mux_nodes[parent].a == leaf ? m->mux_nodes[parent].b : m->mux_nodes[parent].a;
	m->mux_nodes[parent] = m->mux_nodes[sib];
	mux_node_free(m, sib);
	mux_node_free(m, leaf);
}

int
mux_move(VtMultiplexor *m, u32 src, u32 dst, int dir, int first, Renderer *renderer)
{
	u16 sl;
	u16 dl;
	u16 na;
	u16 nb;
	u16 tmp;

	if (src >= VT_PANE_MAX || dst >= VT_PANE_MAX || src == dst)
		return 0;
	if (!m->panes[src].used || !m->panes[dst].used)
		return 0;
	sl = mux_leaf_of(m, src);
	dl = mux_leaf_of(m, dst);
	if (sl == 0xffff || dl == 0xffff)
		return 0;
	if (!dir) {
		tmp = m->mux_nodes[sl].pane;
		m->mux_nodes[sl].pane = m->mux_nodes[dl].pane;
		m->mux_nodes[dl].pane = tmp;
		if (m->cols && m->rows)
			mux_layout(m, m->cols, m->rows, renderer);
		mux_focus(m, src);
		return 1;
	}
	if (dir != VT_SPLIT_H && dir != VT_SPLIT_V)
		return 0;
	if (mux_parent_of(m, sl) == 0xffff)
		return 0;
	na = mux_node_alloc(m);
	nb = mux_node_alloc(m);
	if (na == 0xffff || nb == 0xffff) {
		mux_node_free(m, na);
		mux_node_free(m, nb);
		return 0;
	}
	mux_collapse(m, sl);
	dl = mux_leaf_of(m, dst);
	if (dl == 0xffff) {
		mux_node_free(m, na);
		mux_node_free(m, nb);
		return 0;
	}
	m->mux_nodes[dl].split = (u8)dir;
	m->mux_nodes[dl].ratio = 500;
	m->mux_nodes[dl].a = na;
	m->mux_nodes[dl].b = nb;
	m->mux_nodes[na].split = VT_SPLIT_LEAF;
	m->mux_nodes[nb].split = VT_SPLIT_LEAF;
	if (first) {
		m->mux_nodes[na].pane = (u16)src;
		m->mux_nodes[nb].pane = (u16)dst;
	} else {
		m->mux_nodes[na].pane = (u16)dst;
		m->mux_nodes[nb].pane = (u16)src;
	}
	if (m->cols && m->rows)
		mux_layout(m, m->cols, m->rows, renderer);
	mux_focus(m, src);
	return 1;
}

void
mux_kill_pane(VtMultiplexor *m, u32 i, Renderer *renderer)
{
	u16 leaf;
	u32 nused;
	u32 k;

	if (i >= VT_PANE_MAX || !m->panes[i].used)
		return;
	if (m->drag >= 0 && (u32)m->drag == i) {
		m->drag = -1;
		m->hover = -1;
	}
	nused = 0;
	for (k = 0; k < VT_PANE_MAX; k++) {
		if (m->panes[k].used)
			nused++;
	}
	leaf = mux_leaf_of(m, i);
	mux_close(m, i);
	if (nused <= 1) {
		running = false;
		return;
	}
	if (leaf != 0xffff)
		mux_collapse(m, leaf);
	if (!m->panes[m->focus].used)
		mux_focus(m, mux_first(m));
	if (m->cols && m->rows)
		mux_layout(m, m->cols, m->rows, renderer);
}

int
mux_pick(VtMultiplexor *m, u32 x, u32 y, u32 *lx, u32 *ly)
{
	u32 i;

	for (i = 0; i < VT_PANE_MAX; i++) {
		VtPane *p;

		if (!m->panes[i].used)
			continue;
		p = &m->panes[i];
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

int
mux_pick_near(VtMultiplexor *m, u32 x, u32 y, u32 *lx, u32 *ly)
{
	int hit;
	u32 i;
	u32 best;
	u32 best_d;

	hit = mux_pick(m, x, y, lx, ly);
	if (hit >= 0)
		return hit;
	best = 0xffff;
	best_d = 0xffffffff;
	for (i = 0; i < VT_PANE_MAX; i++) {
		VtPane *p;
		i32 dx;
		i32 dy;
		u32 d;

		if (!m->panes[i].used || !m->panes[i].cols || !m->panes[i].rows)
			continue;
		p = &m->panes[i];
		if (x < p->x)
			dx = (i32)p->x - (i32)x;
		else if (x >= p->x + p->cols)
			dx = (i32)x - (i32)(p->x + p->cols - 1);
		else
			dx = 0;
		if (y < p->y)
			dy = (i32)p->y - (i32)y;
		else if (y >= p->y + p->rows)
			dy = (i32)y - (i32)(p->y + p->rows - 1);
		else
			dy = 0;
		d = (u32)(dx * dx + dy * dy);
		if (d < best_d) {
			best_d = d;
			best = i;
		}
	}
	if (best == 0xffff)
		return -1;
	{
		VtPane *p;
		u32 px;
		u32 py;

		p = &m->panes[best];
		px = x;
		py = y;
		if (px < p->x)
			px = p->x;
		if (px >= p->x + p->cols)
			px = p->x + p->cols - 1;
		if (py < p->y)
			py = p->y;
		if (py >= p->y + p->rows)
			py = p->y + p->rows - 1;
		if (lx)
			*lx = px - p->x;
		if (ly)
			*ly = py - p->y;
	}
	return (int)best;
}

void
mux_drag_side(VtMultiplexor *m, u32 i, u32 x, u32 y, int *dir, int *first)
{
	VtPane *p;
	u32 lx;
	u32 ly;
	u32 dl;
	u32 dr;
	u32 dt;
	u32 db;
	u32 best;
	u32 band;

	p = &m->panes[i];
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
	best = dl;
	if (dr < best) {
		best = dr;
		*first = 0;
	}
	if (dt < best) {
		best = dt;
		*dir = VT_SPLIT_H;
		*first = 1;
	}
	if (db < best) {
		best = db;
		*dir = VT_SPLIT_H;
		*first = 0;
	}
	if (best > band)
		*dir = 0;
}

void
mux_os_drag_start(VtMultiplexor *m)
{
	char dir[192];
	char path[256];
	int n;

	if (m->drag < 0 || m->os_src >= 0)
		return;
	if (!peak_runtime_dir(dir, sizeof dir, "vt"))
		return;
	n = snprintf(path, sizeof path, "%s/%d.sock", dir, peak_pid());
	if (n <= 0 || (size_t)n >= sizeof path)
		return;
	if (peak_drop_drag(VT_PEAK_WIN, path, (size_t)n))
		m->os_src = m->drag;
}

int
mux_drop_self(VtMultiplexor *m, Renderer *renderer)
{
	int src;
	int px;
	int py;
	u32 cx;
	u32 cy;

	src = m->os_src >= 0 ? m->os_src : m->drag;
	m->os_src = -1;
	if (src < 0) {
		m->drag = -1;
		return 0;
	}
	if (!peak_pointer_local(VT_PEAK_WIN, &px, &py)) {
		m->drag = -1;
		return 1;
	}
	cell_at((float)px, (float)py, &cx, &cy);
	m->drag = src;
	mux_drag_over(m, cx, cy);
	m->drag = -1;
	if (m->hover >= 0) {
		mux_move(m, (u32)src, (u32)m->hover, m->drop_dir, m->drop_first, renderer);
		mux_offer_clear();
	}
	m->hover = -1;
	return 1;
}

void
mux_drag_over(VtMultiplexor *m, u32 x, u32 y)
{
	int hit;

	hit = mux_pick(m, x, y, NULL, NULL);
	if (hit < 0 || hit == m->drag) {
		m->hover = -1;
		return;
	}
	m->hover = hit;
	mux_drag_side(m, (u32)hit, x, y, &m->drop_dir, &m->drop_first);
}

int
mux_pointer(VtMultiplexor *m, u32 x, u32 y, PeakPointerState st, PeakKeyMod mod, Renderer *renderer)
{
	int hit;

	if (st == PEAK_POINTER_PRESSED) {
		if (mod & (PEAK_KEYMOD_SHIFT | PEAK_KEYMOD_CTRL | PEAK_KEYMOD_ALT | PEAK_KEYMOD_SUPER))
			return 0;
		hit = mux_pick(m, x, y, NULL, NULL);
		if (hit < 0)
			return 0;
		mux_focus(m, (u32)hit);
		if (mux_offer_take(m, renderer)) {
			sel_on = 0;
			sel_drag = 0;
			return 1;
		}
		m->drag = hit;
		m->os_src = -1;
		m->hover = -1;
		m->click_paste = 0;
		mux_offer_write((u32)hit);
		sel_on = 0;
		sel_drag = 0;
		return 1;
	}
	if (m->drag < 0)
		return 0;
	if (st == PEAK_POINTER_MOVED) {
		int px;
		int py;

		mux_os_drag_start(m);
		if (!peak_pointer_local(VT_PEAK_WIN, &px, &py)) {
			m->hover = -1;
			return 1;
		}
		mux_drag_over(m, x, y);
		return 1;
	}
	if (st == PEAK_POINTER_RELEASED) {
		int src;
		int pid;
		int px;
		int py;
		int local;

		src = m->drag;
		m->drag = -1;
		local = peak_pointer_local(VT_PEAK_WIN, &px, &py);
		m->hover = -1;
		if (m->os_src >= 0)
			return 1;
		if (src >= 0) {
			pid = mux_find_hit();
			VTINFO("mux drop pid=%d local=%d", pid, local);
			if (mux_export(m, (u32)src, pid, renderer))
				return 1;
		}
		if (src >= 0 && local) {
			u32 cx;
			u32 cy;

			cell_at((float)px, (float)py, &cx, &cy);
			mux_drag_over(m, cx, cy);
			if (m->hover >= 0) {
				mux_move(m, (u32)src, (u32)m->hover, m->drop_dir, m->drop_first, renderer);
				mux_offer_clear();
				m->hover = -1;
				return 1;
			}
			m->click_paste = 1;
		}
		m->hover = -1;
		return 1;
	}
	return 0;
}

int
mux_ch_hit(const char *s, PeakKeyCode key, u32 ch)
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
mux_chord(PeakKeyCode want, PeakKeyMod want_mod, PeakKeyCode key, PeakKeyMod mod)
{
	return key == want && ((mod & ~PEAK_KEYMOD_CAPS) == want_mod);
}

int
mux_key(VtMultiplexor *m, PeakKeyCode key, PeakKeyMod mod, u32 code, Renderer *renderer)
{
	u32 ch;

	ch = code;
	if (ch >= 'A' && ch <= 'Z')
		ch += 32;
	if (m->prefix) {
		/* Shift/Ctrl KEY_DOWN is a separate event. Do not eat prefix. */
		if (key == PEAK_KEY_UNKNOWN && ch < 32u)
			return 1;
		m->prefix = 0;
		if (key == PEAK_KEY_ESCAPE)
			return 1;
		if (mux_chord(mux_prefix_key, mux_prefix_mod, key, mod)) {
			if ((mux_prefix_mod & PEAK_KEYMOD_CTRL)
					&& mux_prefix_key >= PEAK_KEY_A
					&& mux_prefix_key <= PEAK_KEY_Z) {
				char b;

				b = (char)(1 + (mux_prefix_key - PEAK_KEY_A));
				pane_write(m->pane, &b, 1);
			}
			return 1;
		}
		if (mux_ch_hit(mux_split_v, key, ch)) {
			mux_split(m, VT_SPLIT_V, renderer);
			return 1;
		}
		if (mux_ch_hit(mux_split_h, key, ch)) {
			mux_split(m, VT_SPLIT_H, renderer);
			return 1;
		}
		if (key == PEAK_KEY_LEFT || mux_ch_hit(mux_left, key, ch)) {
			mux_focus_dir(m, -1, 0);
			return 1;
		}
		if (key == PEAK_KEY_RIGHT || mux_ch_hit(mux_right, key, ch)) {
			mux_focus_dir(m, 1, 0);
			return 1;
		}
		if (key == PEAK_KEY_UP || mux_ch_hit(mux_up, key, ch)) {
			mux_focus_dir(m, 0, -1);
			return 1;
		}
		if (key == PEAK_KEY_DOWN || mux_ch_hit(mux_down, key, ch)) {
			mux_focus_dir(m, 0, 1);
			return 1;
		}
		if (mux_ch_hit(mux_next, key, ch)) {
			mux_focus_next(m);
			return 1;
		}
		if (mux_ch_hit(mux_kill, key, ch)) {
			mux_kill_pane(m, m->focus, renderer);
			return 1;
		}
		return 1;
	}
	if (mux_chord(mux_prefix_key, mux_prefix_mod, key, mod)) {
		m->prefix = 1;
		return 1;
	}
	return 0;
}

u32
mux_fds(VtMultiplexor *m, PEAK_HANDLE *fds)
{
	u32 n;
	u32 i;

	n = 0;
	for (i = 0; i < VT_PANE_MAX; i++) {
		if (m->panes[i].used && m->panes[i].sh.fd != PEAK_HANDLE_INVALID)
			fds[n++] = m->panes[i].sh.fd;
	}
	return n;
}

u32
mux_fill_walls(VtMultiplexor *m, Renderer *r, VtInstance *inst, u32 n, u32 cap)
{
	u32 x;
	u32 y;
	color_packed_t dim;
	color_packed_t lit;
	color_packed_t bg;
	VtPane *f;

	if (!m->cols || !m->rows || m->cols * m->rows > VT_MUX_WALL_MAX)
		return n;
	dim = m->mux_colors.fg[8] << 8;
	lit = m->mux_colors.fg[11] << 8;
	bg = m->mux_colors.bg[m->mux_colors.bg_default < 8 ? m->mux_colors.bg_default : 0] << 8;
	f = m->panes[m->focus].used ? &m->panes[m->focus] : NULL;
	for (y = 0; y < m->rows; y++) {
		for (x = 0; x < m->cols; x++) {
			u8 arm;
			codepoint_t cp;
			color_packed_t fg;
			int hot;

			arm = m->wall[y * m->cols + x] & 15u;
			if (!arm)
				continue;
			cp = mux_box[arm];
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
			n = renderer_fill_cp(r, x, y, cp, fg, bg, inst, n, cap);
		}
	}
	return n;
}

u32
mux_fill_drop(VtMultiplexor *m, Renderer *r, VtInstance *inst, u32 n, u32 cap)
{
	VtPane *p;
	color_packed_t fg;
	color_packed_t bg;
	u32 x;
	u32 y;
	u32 a;

	if (m->drag < 0 || m->hover < 0)
		return n;
	p = &m->panes[m->hover];
	if (!p->used || !p->cols || !p->rows)
		return n;
	fg = m->mux_colors.fg[11] << 8;
	bg = m->mux_colors.bg[m->mux_colors.bg_default < 8 ? m->mux_colors.bg_default : 0] << 8;
	if (!m->drop_dir) {
		for (x = p->x; x < p->x + p->cols; x++) {
			n = renderer_fill_cp(r, x, p->y, 0x2500, fg, bg, inst, n, cap);
			n = renderer_fill_cp(r, x, p->y + p->rows - 1, 0x2500, fg, bg, inst, n, cap);
		}
		for (y = p->y; y < p->y + p->rows; y++) {
			n = renderer_fill_cp(r, p->x, y, 0x2502, fg, bg, inst, n, cap);
			n = renderer_fill_cp(r, p->x + p->cols - 1, y, 0x2502, fg, bg, inst, n, cap);
		}
		return n;
	}
	if (m->drop_dir == VT_SPLIT_V) {
		a = p->x + (m->drop_first ? p->cols / 4u : (p->cols * 3u) / 4u);
		if (a >= p->x + p->cols)
			a = p->x + p->cols - 1;
		for (y = p->y; y < p->y + p->rows; y++)
			n = renderer_fill_cp(r, a, y, 0x2502, fg, bg, inst, n, cap);
		return n;
	}
	a = p->y + (m->drop_first ? p->rows / 4u : (p->rows * 3u) / 4u);
	if (a >= p->y + p->rows)
		a = p->y + p->rows - 1;
	for (x = p->x; x < p->x + p->cols; x++)
		n = renderer_fill_cp(r, x, a, 0x2500, fg, bg, inst, n, cap);
	return n;
}
