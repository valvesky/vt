#pragma once

#include "vt.h"

static PEAK_HANDLE ctl_listen = PEAK_HANDLE_INVALID;
static char ctl_path[256];
static char ctl_latest[256];
static VtCtlClient ctl_clients[CTL_CLIENTS];
static VtCtlJob ctl_job;

static void ctl_init(void);
static void ctl_destroy(void);
static void ctl_client_close(VtCtlClient *c);
static u32 ctl_fds(PEAK_HANDLE *fds);
static void ctl_pump(void);
static void reap_children(void);
static int ctl_skip_ws(const char *s, int i);
static int ctl_parse_string(const char *s, int i, const char **out, int *n);
static int ctl_parse(const char *s, VtCtlReq *req);
static int ctl_unescape(const char *s, int n, char *dst, size_t cap);
static int ctl_put(PEAK_HANDLE fd, const char *p, size_t n);
static int ctl_put_escaped(PEAK_HANDLE fd, const char *p, size_t n);
static int ctl_put_head(PEAK_HANDLE fd, const char *id, int id_n, int ok, const char *mid, size_t mid_n);
static void ctl_reply_err(VtCtlClient *c, const char *id, int id_n, const char *err);
static void ctl_ok(VtCtlClient *c, const char *id, int id_n, const char *tail);
static void ctl_okf(VtCtlClient *c, const VtCtlReq *req, const char *err, const char *fmt, ...);
static void ctl_job_clear(void);
static void ctl_job_finish(void);
static void ctl_job_read(void);
static int ctl_dump_put(void *ctx, const char *p, size_t n);
static const char *ctl_find(const char *hay, u32 hay_n, const char *needle, u32 needle_n);
static void ctl_read_band(const VtCtlReq *req, u32 rows, u32 cy, u32 *y0, u32 *n);
static void ctl_handle_read(VtCtlClient *c, const VtCtlReq *req);
static void ctl_handle_rg(VtCtlClient *c, const VtCtlReq *req);
static void ctl_handle_line(VtCtlClient *c, char *line);
static void ctl_client_read(VtCtlClient *c);
static void ctl_accept(void);

void
ctl_init(void)
{
	char dir[192];
	PEAK_HANDLE fd;
	int i;
	int n;

	peak_child_arm();
	ctl_listen = PEAK_HANDLE_INVALID;
	ctl_path[0] = 0;
	ctl_latest[0] = 0;
	ctl_job.pid = 0;
	ctl_job.fd = PEAK_HANDLE_INVALID;
	ctl_job.client = -1;
	ctl_job.id_n = 0;
	ctl_job.code = 0;
	ctl_job.out_n = 0;
	ctl_job.trunc = false;
	ctl_job.dead = false;
	for (i = 0; i < CTL_CLIENTS; i++) {
		ctl_clients[i].fd = PEAK_HANDLE_INVALID;
		ctl_clients[i].pass = PEAK_HANDLE_INVALID;
		ctl_clients[i].n = 0;
	}
	if (!peak_runtime_dir(dir, sizeof dir, "vt")) {
		VTERROR("ctl socket disabled");
	} else {
		n = snprintf(ctl_path, sizeof ctl_path, "%s/%d.sock", dir, peak_pid());
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
				VTINFO("ctl %s", ctl_path);
				n = snprintf(ctl_latest, sizeof ctl_latest, "%s/latest.sock", dir);
				if (n < 0 || (size_t)n >= sizeof ctl_latest) {
					ctl_latest[0] = 0;
				} else {
					char name[32];

					n = snprintf(name, sizeof name, "%d.sock", peak_pid());
					if (n < 0 || (size_t)n >= sizeof name) {
						ctl_latest[0] = 0;
					} else {
						peak_filesystem_rm(ctl_latest);
						if (!peak_filesystem_symlink(name, ctl_latest))
							ctl_latest[0] = 0;
					}
				}
			}
		}
	}
}

void
ctl_destroy(void)
{
	int i;
	PeakProc job;

	job.fd = ctl_job.fd;
	job.pid = ctl_job.pid;
	peak_job_kill(&job);
	ctl_job.fd = PEAK_HANDLE_INVALID;
	ctl_job.pid = 0;
	ctl_job.client = -1;
	ctl_job.out_n = 0;
	ctl_job.trunc = false;
	ctl_job.dead = false;
	for (i = 0; i < CTL_CLIENTS; i++)
		ctl_client_close(&ctl_clients[i]);
	if (ctl_listen != PEAK_HANDLE_INVALID) {
		peak_fd_close(ctl_listen);
		ctl_listen = PEAK_HANDLE_INVALID;
	}
	if (ctl_path[0]) {
		if (ctl_latest[0]) {
			char cur[256];
			char *base;

			base = strrchr(ctl_path, '/');
			if (!base)
				base = strrchr(ctl_path, '\\');
			base = base ? base + 1 : ctl_path;
			if (peak_filesystem_readlink(ctl_latest, cur, sizeof cur) && !strcmp(cur, base))
				peak_filesystem_rm(ctl_latest);
			ctl_latest[0] = 0;
		}
		peak_filesystem_rm(ctl_path);
		ctl_path[0] = 0;
	}
	peak_child_disarm();
}

int
ctl_skip_ws(const char *s, int i)
{
	while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')
		i++;
	return i;
}

int
ctl_parse_string(const char *s, int i, const char **out, int *n)
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

int
ctl_parse(const char *s, VtCtlReq *req)
{
	int i;

	memset(req, 0, sizeof *req);
	i = ctl_skip_ws(s, 0);
	if (s[i++] != '{')
		return 0;
	for (;;) {
		const char *key;
		int kn;
		int raw0;

		i = ctl_skip_ws(s, i);
		if (s[i] == '}')
			return 1;
		i = ctl_parse_string(s, i, &key, &kn);
		if (i < 0)
			return 0;
		i = ctl_skip_ws(s, i);
		if (s[i++] != ':')
			return 0;
		i = ctl_skip_ws(s, i);
		if (s[i] == '"') {
			const char *val;
			int vn;

			raw0 = i;
			i = ctl_parse_string(s, i, &val, &vn);
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
			int neg;

			raw0 = i;
			neg = 0;
			if (s[i] == '-') {
				neg = 1;
				i++;
			}
			if (s[i] >= '0' && s[i] <= '9') {
				unsigned long v;

				v = 0;
				while (s[i] >= '0' && s[i] <= '9') {
					v = v * 10ul + (unsigned long)(s[i] - '0');
					if (v > 2147483647ul)
						v = 2147483647ul;
					i++;
				}
				if (kn == 1 && key[0] == 'y') {
					req->has_y = 1;
					req->y = neg ? -(int)v : (int)v;
				} else if (kn == 1 && key[0] == 'n') {
					req->has_n = 1;
					req->n = neg ? -(int)v : (int)v;
				}
			} else if (!neg && strncmp(s + i, "true", 4) == 0) {
				i += 4;
			} else if (!neg && strncmp(s + i, "false", 5) == 0) {
				i += 5;
			} else if (!neg && strncmp(s + i, "null", 4) == 0) {
				i += 4;
			} else {
				return 0;
			}
			if (kn == 2 && key[0] == 'i' && key[1] == 'd') {
				req->id = s + raw0;
				req->id_n = i - raw0;
			}
		}
		i = ctl_skip_ws(s, i);
		if (s[i] == ',') {
			i++;
			continue;
		}
		if (s[i] == '}')
			return 1;
		return 0;
	}
}

int
ctl_unescape(const char *s, int n, char *dst, size_t cap)
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
			tn = utf8_encode((codepoint_t)v, tmp);
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

int
ctl_put(PEAK_HANDLE fd, const char *p, size_t n)
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

int
ctl_put_escaped(PEAK_HANDLE fd, const char *p, size_t n)
{
	static const char hex[] = "0123456789abcdef";
	char out[4096];
	size_t i;
	u32 o;

	o = 0;
	for (i = 0; i < n; i++) {
		unsigned char ch;
		const char *esc;
		size_t elen;
		char u[6];

		ch = (unsigned char)p[i];
		if (ch != '"' && ch != '\\' && ch >= 0x20) {
			if (o == sizeof out) {
				if (ctl_put(fd, out, o) < 0)
					return -1;
				o = 0;
			}
			out[o++] = (char)ch;
			continue;
		}
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
		if (o + (u32)elen > sizeof out) {
			if (o && ctl_put(fd, out, o) < 0)
				return -1;
			o = 0;
		}
		memcpy(out + o, esc, elen);
		o += (u32)elen;
	}
	if (o)
		return ctl_put(fd, out, o);
	return 0;
}

int
ctl_put_head(PEAK_HANDLE fd, const char *id, int id_n, int ok, const char *mid, size_t mid_n)
{
	char buf[320];
	const char *okv;
	size_t n;
	size_t ok_n;

	n = 0;
	okv = ok ? "\"ok\":true" : "\"ok\":false";
	ok_n = ok ? 9 : 10;
	if (id && id_n > 0) {
		if (1 + 5 + (size_t)id_n + 1 + ok_n + mid_n > sizeof buf)
			return -1;
		buf[n++] = '{';
		memcpy(buf + n, "\"id\":", 5);
		n += 5;
		memcpy(buf + n, id, (size_t)id_n);
		n += (size_t)id_n;
		buf[n++] = ',';
	} else {
		if (1 + ok_n + mid_n > sizeof buf)
			return -1;
		buf[n++] = '{';
	}
	memcpy(buf + n, okv, ok_n);
	n += ok_n;
	if (mid_n) {
		memcpy(buf + n, mid, mid_n);
		n += mid_n;
	}
	return ctl_put(fd, buf, n);
}

void
ctl_client_close(VtCtlClient *c)
{
	VTASSERT(c);
	if (c->fd == PEAK_HANDLE_INVALID)
		return;
	if (ctl_job.client >= 0 && c == &ctl_clients[ctl_job.client])
		ctl_job.client = -1;
	peak_fd_close(c->fd);
	c->fd = PEAK_HANDLE_INVALID;
	if (c->pass != PEAK_HANDLE_INVALID) {
		peak_fd_close(c->pass);
		c->pass = PEAK_HANDLE_INVALID;
	}
	c->n = 0;
}

void
ctl_reply_err(VtCtlClient *c, const char *id, int id_n, const char *err)
{
	char tail[96];
	int n;

	if (c->fd == PEAK_HANDLE_INVALID)
		return;
	n = snprintf(tail, sizeof tail, ",\"error\":\"%s\"}\n", err ? err : "error");
	if (n < 0 || (size_t)n >= sizeof tail
			|| ctl_put_head(c->fd, id, id_n, 0, tail, (size_t)n) < 0)
		ctl_client_close(c);
}

void
ctl_ok(VtCtlClient *c, const char *id, int id_n, const char *tail)
{
	if (c->fd == PEAK_HANDLE_INVALID)
		return;
	if (ctl_put_head(c->fd, id, id_n, 1, tail, strlen(tail)) < 0)
		ctl_client_close(c);
}

void
ctl_okf(VtCtlClient *c, const VtCtlReq *req, const char *err, const char *fmt, ...)
{
	char tail[80];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(tail, sizeof tail, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= sizeof tail) {
		ctl_reply_err(c, req->id, req->id_n, err ? err : "reply failed");
		return;
	}
	ctl_ok(c, req->id, req->id_n, tail);
}

void
ctl_job_clear(void)
{
	ctl_job.dead = false;
	ctl_job.out_n = 0;
	ctl_job.trunc = false;
	ctl_job.client = -1;
	ctl_job.id_n = 0;
}

void
ctl_job_finish(void)
{
	VtCtlClient *c;
	int n;
	char head[192];

	if (!ctl_job.dead || ctl_job.fd != PEAK_HANDLE_INVALID)
		return;
	if (ctl_job.client >= 0 && ctl_job.client < CTL_CLIENTS) {
		c = &ctl_clients[ctl_job.client];
		if (c->fd != PEAK_HANDLE_INVALID) {
			n = snprintf(head, sizeof head, "{\"ev\":\"exit\",\"job\":%u,", ctl_job.seq);
			if (n < 0 || (size_t)n >= sizeof head)
				goto drop;
			if (ctl_job.id_n > 0) {
				if ((size_t)n + 5 + (size_t)ctl_job.id_n + 1 >= sizeof head)
					goto drop;
				memcpy(head + n, "\"id\":", 5);
				n += 5;
				memcpy(head + n, ctl_job.id, (size_t)ctl_job.id_n);
				n += ctl_job.id_n;
				head[n++] = ',';
			}
			{
				int m;

				m = snprintf(head + n, sizeof head - (size_t)n, "\"code\":%d,\"out\":\"", ctl_job.code);
				if (m < 0 || (size_t)m >= sizeof head - (size_t)n)
					goto drop;
				n += m;
			}
			if (ctl_put(c->fd, head, (size_t)n) < 0
					|| ctl_put_escaped(c->fd, ctl_job.out, ctl_job.out_n) < 0)
				goto drop;
			if (ctl_job.trunc && ctl_put(c->fd, "\",\"trunc\":true}\n", strlen("\",\"trunc\":true}\n")) < 0)
				goto drop;
			if (!ctl_job.trunc && ctl_put(c->fd, "\"}\n", strlen("\"}\n")) < 0)
				goto drop;
		}
	}
	ctl_job_clear();
	return;
drop:
	ctl_client_close(c);
	ctl_job_clear();
}

void
reap_children(void)
{
	int pid, code;
	u32 pi;
	PeakProc job;

	for (;;) {
		int hit;

		if (!peak_child_reap(&pid, &code))
			break;
		hit = 0;
		for (pi = 0; pi < PANE_MAX; pi++) {
			if (multiplexor.panes[pi].used && multiplexor.panes[pi].sh.pid > 0 && pid == multiplexor.panes[pi].sh.pid) {
				multiplexor.panes[pi].sh.pid = 0;
				mux_kill_pane(&multiplexor, pi, renderer);
				hit = 1;
				break;
			}
		}
		if (!hit && ctl_job.pid > 0 && pid == ctl_job.pid) {
			ctl_job.pid = 0;
			ctl_job.dead = true;
			ctl_job.code = code;
			ctl_job_finish();
		}
	}
	for (pi = 0; pi < PANE_MAX; pi++) {
		if (!multiplexor.panes[pi].used || multiplexor.panes[pi].sh.pid <= 0)
			continue;
		if (vt_shell_reap(&multiplexor.panes[pi].sh))
			mux_kill_pane(&multiplexor, pi, renderer);
	}
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
	ctl_job_finish();
}

void
ctl_job_read(void)
{
	if (ctl_job.fd == PEAK_HANDLE_INVALID)
		return;
	for (;;) {
		char buf[4096];
		int r;
		u32 room;

		r = peak_fd_read(ctl_job.fd, buf, sizeof buf);
		if (r > 0) {
			room = CTL_JOB_OUT - ctl_job.out_n;
			if ((u32)r > room) {
				if (room)
					memcpy(ctl_job.out + ctl_job.out_n, buf, room);
				ctl_job.out_n = CTL_JOB_OUT;
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
		reap_children();
		return;
	}
}

typedef struct {
	PEAK_HANDLE fd;
	u32 n;
	char buf[4096];
} VtCtlDump;

int
ctl_dump_put(void *ctx, const char *p, size_t n)
{
	VtCtlDump *o;

	o = ctx;
	while (n) {
		u32 room;
		u32 k;

		room = (u32)sizeof o->buf - o->n;
		if (!room) {
			if (ctl_put_escaped(o->fd, o->buf, o->n) < 0)
				return -1;
			o->n = 0;
			room = (u32)sizeof o->buf;
		}
		k = n < room ? (u32)n : room;
		memcpy(o->buf + o->n, p, k);
		o->n += k;
		p += k;
		n -= k;
	}
	return 0;
}

const char *
ctl_find(const char *hay, u32 hay_n, const char *needle, u32 needle_n)
{
	u32 i;

	if (needle_n > hay_n)
		return NULL;
	for (i = 0; i + needle_n <= hay_n; i++) {
		if (memcmp(hay + i, needle, needle_n) == 0)
			return hay + i;
	}
	return NULL;
}

void
ctl_read_band(const VtCtlReq *req, u32 rows, u32 cy, u32 *y0, u32 *n)
{
	int want;

	want = CTL_READ_N;
	if (req->has_n && req->n > 0)
		want = req->n;
	if (rows == 0) {
		*y0 = 0;
		*n = 0;
		return;
	}
	if ((u32)want > rows)
		want = (int)rows;
	if (req->has_y) {
		if (req->y <= 0)
			*y0 = 0;
		else if ((u32)req->y >= rows)
			*y0 = rows - 1;
		else
			*y0 = (u32)req->y;
	} else {
		u32 half;

		half = (u32)want / 2;
		if (cy < half)
			*y0 = 0;
		else
			*y0 = cy - half;
	}
	if (*y0 + (u32)want > rows)
		want = (int)(rows - *y0);
	*n = (u32)want;
}

void
ctl_handle_read(VtCtlClient *c, const VtCtlReq *req)
{
	TermScreen *s;
	VtCtlDump o;
	char mid[96];
	u32 y0;
	u32 n;
	int k;

	s = term_screen(&multiplexor.pane->term);
	if (!s || !s->line) {
		ctl_reply_err(c, req->id, req->id_n, "read failed");
		return;
	}
	ctl_read_band(req, s->rows, multiplexor.pane->term.cursor.y, &y0, &n);
	o.fd = c->fd;
	o.n = 0;
	k = snprintf(mid, sizeof mid, ",\"x\":%u,\"y\":%u,\"cols\":%u,\"rows\":%u,\"text\":\"",
			multiplexor.pane->term.cursor.x, multiplexor.pane->term.cursor.y, s->cols, s->rows);
	if (k < 0 || (size_t)k >= sizeof mid
			|| ctl_put_head(c->fd, req->id, req->id_n, 1, mid, (size_t)k) < 0
			|| dump_walk_rows(ctl_dump_put, &o, y0, n) < 0
			|| (o.n && ctl_put_escaped(o.fd, o.buf, o.n) < 0)
			|| ctl_put(c->fd, "\"}\n", strlen("\"}\n")) < 0)
		ctl_client_close(c);
}

void
ctl_handle_rg(VtCtlClient *c, const VtCtlReq *req)
{
	TermScreen *s;
	char needle[CTL_LINE];
	char *row;
	char *out;
	char head[64];
	u32 y;
	u32 hits;
	u32 out_n;
	int needle_n;
	int n;
	int trunc;

	if (!req->data) {
		ctl_reply_err(c, req->id, req->id_n, "missing data");
		return;
	}
	needle_n = ctl_unescape(req->data, req->data_n, needle, sizeof needle);
	if (needle_n < 0) {
		ctl_reply_err(c, req->id, req->id_n, "bad json");
		return;
	}
	if (needle_n == 0) {
		ctl_reply_err(c, req->id, req->id_n, "empty data");
		return;
	}
	s = term_screen(&multiplexor.pane->term);
	if (!s || !s->line) {
		ctl_reply_err(c, req->id, req->id_n, "rg failed");
		return;
	}
	row = malloc((size_t)s->cols * 4u + 1u);
	out = malloc(CTL_RG_OUT);
	if (!row || !out) {
		free(row);
		free(out);
		ctl_reply_err(c, req->id, req->id_n, "rg failed");
		return;
	}
	hits = 0;
	out_n = 0;
	trunc = 0;
	for (y = 0; y < s->rows; y++) {
		u32 row_n;
		char pre[16];
		int pn;

		row_n = dump_row_utf8(s, y, row, s->cols * 4u);
		if (!ctl_find(row, row_n, needle, (u32)needle_n))
			continue;
		if (hits >= CTL_RG_HITS) {
			trunc = 1;
			break;
		}
		pn = snprintf(pre, sizeof pre, "%s%u:", hits ? "\n" : "", y);
		if (pn < 0 || (size_t)pn >= sizeof pre) {
			trunc = 1;
			break;
		}
		if (out_n + (u32)pn + row_n > CTL_RG_OUT) {
			trunc = 1;
			break;
		}
		memcpy(out + out_n, pre, (size_t)pn);
		out_n += (u32)pn;
		memcpy(out + out_n, row, row_n);
		out_n += row_n;
		hits++;
	}
	n = snprintf(head, sizeof head, ",\"n\":%u,\"text\":\"", hits);
	if (n < 0 || (size_t)n >= sizeof head
			|| ctl_put_head(c->fd, req->id, req->id_n, 1, head, (size_t)n) < 0
			|| ctl_put_escaped(c->fd, out, out_n) < 0
			|| (trunc && ctl_put(c->fd, "\",\"trunc\":true}\n", strlen("\",\"trunc\":true}\n")) < 0)
			|| (!trunc && ctl_put(c->fd, "\"}\n", strlen("\"}\n")) < 0))
		ctl_client_close(c);
	free(row);
	free(out);
}

void
ctl_handle_line(VtCtlClient *c, char *line)
{
	VtCtlReq req;

	if (!line[0])
		return;
	if (!ctl_parse(line, &req) || !req.op) {
		ctl_reply_err(c, req.id, req.id_n, "bad json");
		return;
	}
	if (req.op_n == 4 && memcmp(req.op, "dump", 4) == 0) {
		TermScreen *s;
		VtCtlDump o;
		char mid[80];
		int n;

		s = term_screen(&multiplexor.pane->term);
		if (!s || !s->line) {
			ctl_reply_err(c, req.id, req.id_n, "dump failed");
			return;
		}
		o.fd = c->fd;
		o.n = 0;
		n = snprintf(mid, sizeof mid, ",\"cols\":%u,\"rows\":%u,\"text\":\"",
				s->cols, s->rows);
		if (n < 0 || (size_t)n >= sizeof mid
				|| ctl_put_head(c->fd, req.id, req.id_n, 1, mid, (size_t)n) < 0
				|| dump_walk(ctl_dump_put, &o) < 0
				|| (o.n && ctl_put_escaped(o.fd, o.buf, o.n) < 0)
				|| ctl_put(c->fd, "\"}\n", strlen("\"}\n")) < 0)
			ctl_client_close(c);
	} else if (req.op_n == 4 && memcmp(req.op, "read", 4) == 0) {
		ctl_handle_read(c, &req);
	} else if (req.op_n == 2 && memcmp(req.op, "rg", 2) == 0) {
		ctl_handle_rg(c, &req);
	} else if (req.op_n == 6 && memcmp(req.op, "cursor", 6) == 0) {
		ctl_okf(c, &req, "cursor failed", ",\"x\":%u,\"y\":%u}\n",
				multiplexor.pane->term.cursor.x, multiplexor.pane->term.cursor.y);
	} else if (req.op_n == 4 && memcmp(req.op, "size", 4) == 0) {
		TermScreen *s;

		s = term_screen(&multiplexor.pane->term);
		ctl_okf(c, &req, "size failed", ",\"cols\":%u,\"rows\":%u}\n", s->cols, s->rows);
	} else if (req.op_n == 5 && memcmp(req.op, "split", 5) == 0) {
		int dir;

		dir = SPLIT_V;
		if (req.data && req.data_n == 1 && req.data[0] == 'h')
			dir = SPLIT_H;
		if (!mux_split(&multiplexor, dir, renderer)) {
			ctl_reply_err(c, req.id, req.id_n, "split failed");
			return;
		}
		ctl_okf(c, &req, "split failed", ",\"pane\":%u}\n", multiplexor.focus);
	} else if (req.op_n == 5 && memcmp(req.op, "focus", 5) == 0) {
		u32 pane;

		pane = req.has_n ? (u32)req.n : multiplexor.focus;
		if (pane >= PANE_MAX || !multiplexor.panes[pane].used) {
			ctl_reply_err(c, req.id, req.id_n, "bad pane");
			return;
		}
		mux_focus(&multiplexor, pane);
		ctl_okf(c, &req, "focus failed", ",\"pane\":%u}\n", multiplexor.focus);
	} else if (req.op_n == 5 && memcmp(req.op, "panes", 5) == 0) {
		u32 i;
		u32 n;

		n = 0;
		for (i = 0; i < PANE_MAX; i++) {
			if (multiplexor.panes[i].used)
				n++;
		}
		ctl_okf(c, &req, "panes failed", ",\"n\":%u,\"focus\":%u}\n", n, multiplexor.focus);
	} else if (req.op_n == 4 && memcmp(req.op, "move", 4) == 0) {
		u32 dst;
		int dir;
		int first;

		if (!req.has_n) {
			ctl_reply_err(c, req.id, req.id_n, "missing n");
			return;
		}
		dst = (u32)req.n;
		dir = SPLIT_V;
		first = 0;
		if (req.data && req.data_n == 1) {
			switch (req.data[0]) {
			case 'h':
				dir = SPLIT_H;
				break;
			case 'v':
				dir = SPLIT_V;
				break;
			case 's':
				dir = 0;
				break;
			case 'l':
				dir = SPLIT_V;
				first = 1;
				break;
			case 'r':
				dir = SPLIT_V;
				first = 0;
				break;
			case 'u':
				dir = SPLIT_H;
				first = 1;
				break;
			case 'd':
				dir = SPLIT_H;
				first = 0;
				break;
			default:
				ctl_reply_err(c, req.id, req.id_n, "bad move");
				return;
			}
		} else if (req.data && req.data_n) {
			ctl_reply_err(c, req.id, req.id_n, "bad move");
			return;
		}
		if (!mux_move(&multiplexor, multiplexor.focus, dst, dir, first, renderer)) {
			ctl_reply_err(c, req.id, req.id_n, "move failed");
			return;
		}
		ctl_okf(c, &req, "move failed", ",\"pane\":%u}\n", multiplexor.focus);
	} else if (req.op_n == 5 && memcmp(req.op, "write", 5) == 0) {
		char data[CTL_LINE];
		int n;

		if (!req.data) {
			ctl_reply_err(c, req.id, req.id_n, "missing data");
			return;
		}
		n = ctl_unescape(req.data, req.data_n, data, sizeof data);
		if (n < 0) {
			ctl_reply_err(c, req.id, req.id_n, "bad json");
			return;
		}
		if (!multiplexor.pane || multiplexor.pane->sh.fd == PEAK_HANDLE_INVALID) {
			ctl_reply_err(c, req.id, req.id_n, "no pty");
			return;
		}
		ctl_okf(c, &req, "write failed", ",\"n\":%d}\n", (int)pane_write(multiplexor.pane, data, (size_t)n));
	} else if (req.op_n == 10 && memcmp(req.op, "screenshot", 10) == 0) {
		char path[CTL_LINE];
		TermScreen *s;
		int n;

		if (!req.path) {
			ctl_reply_err(c, req.id, req.id_n, "missing path");
			return;
		}
		n = ctl_unescape(req.path, req.path_n, path, sizeof path);
		if (n < 0) {
			ctl_reply_err(c, req.id, req.id_n, "bad json");
			return;
		}
		if (n == 0 || path[0] == 0) {
			ctl_reply_err(c, req.id, req.id_n, "empty path");
			return;
		}
		if (!renderer || !renderer->atlas.atlas) {
			ctl_reply_err(c, req.id, req.id_n, "no atlas");
			return;
		}
		s = term_screen(&multiplexor.pane->term);
		(void)s;
		renderer_screenshot_term_ppm(renderer, &multiplexor.pane->term, path);
		ctl_ok(c, req.id, req.id_n, "}\n");
	} else if (req.op_n == 9 && memcmp(req.op, "clipboard", 9) == 0) {
		char data[CTL_LINE];
		int n;

		if (req.data) {
			n = ctl_unescape(req.data, req.data_n, data, sizeof data);
			if (n < 0) {
				ctl_reply_err(c, req.id, req.id_n, "bad json");
				return;
			}
			if (!peak_clip_set(renderer ? &renderer->win : NULL, PEAK_CLIP_CLIPBOARD, data, (size_t)n)) {
				ctl_reply_err(c, req.id, req.id_n, "clipboard set failed");
				return;
			}
			ctl_ok(c, req.id, req.id_n, "}\n");
		} else {
			size_t gn;
			char *got;

			if (!peak_clip_request(NULL, PEAK_CLIP_CLIPBOARD)) {
				ctl_reply_err(c, req.id, req.id_n, "clipboard get failed");
				return;
			}
			got = malloc(CLIP_MAX + 1);
			if (!got) {
				ctl_reply_err(c, req.id, req.id_n, "clipboard get failed");
				return;
			}
			gn = 0;
			if (!peak_clip_take(NULL, got, CLIP_MAX, &gn))
				gn = 0;
			if (ctl_put_head(c->fd, req.id, req.id_n, 1, ",\"data\":\"", 9) < 0
					|| ctl_put_escaped(c->fd, got, gn) < 0
					|| ctl_put(c->fd, "\"}\n", strlen("\"}\n")) < 0)
				ctl_client_close(c);
			free(got);
		}
	} else if (req.op_n == 3 && memcmp(req.op, "run", 3) == 0) {
		char cmd[CTL_LINE];
		char cwd[512];
		const char *dir;
		PeakProc job;
		int n;

		if (!req.cmd) {
			ctl_reply_err(c, req.id, req.id_n, "missing cmd");
			return;
		}
		n = ctl_unescape(req.cmd, req.cmd_n, cmd, sizeof cmd);
		if (n < 0) {
			ctl_reply_err(c, req.id, req.id_n, "bad json");
			return;
		}
		if (n == 0) {
			ctl_reply_err(c, req.id, req.id_n, "empty cmd");
			return;
		}
		if (ctl_job.pid > 0 || ctl_job.dead || ctl_job.fd != PEAK_HANDLE_INVALID) {
			ctl_reply_err(c, req.id, req.id_n, "busy");
			return;
		}
		dir = NULL;
		if (multiplexor.pane && multiplexor.pane->sh.pid > 0 && peak_pid_cwd(multiplexor.pane->sh.pid, cwd, sizeof cwd))
			dir = cwd;
		job = peak_job_run(cmd, dir);
		if (job.fd == PEAK_HANDLE_INVALID) {
			ctl_reply_err(c, req.id, req.id_n, "fork failed");
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
		ctl_okf(c, &req, "run failed", ",\"job\":%u}\n", ctl_job.seq);
	} else if (req.op_n == 5 && memcmp(req.op, "adopt", 5) == 0) {
		PeakProc proc;
		int dir;
		int first;

		if (c->pass == PEAK_HANDLE_INVALID) {
			ctl_reply_err(c, req.id, req.id_n, "missing fd");
			return;
		}
		proc.fd = c->pass;
		proc.pid = req.has_n ? req.n : 0;
		c->pass = PEAK_HANDLE_INVALID;
		mux_attach_side(&multiplexor, &dir, &first);
		if (!mux_attach(&multiplexor, proc, dir, first, renderer)) {
			peak_fd_close(proc.fd);
			ctl_reply_err(c, req.id, req.id_n, "adopt failed");
			return;
		}
		ctl_okf(c, &req, "adopt failed", ",\"pane\":%u}\n", multiplexor.focus);
	} else if (req.op_n == 4 && memcmp(req.op, "give", 4) == 0) {
		u32 i;
		u32 n;
		u32 sent;

		if (req.has_n) {
			i = (u32)req.n;
			if (i >= PANE_MAX || !multiplexor.panes[i].used
					|| multiplexor.panes[i].sh.fd == PEAK_HANDLE_INVALID) {
				ctl_reply_err(c, req.id, req.id_n, "no pty");
				return;
			}
			ctl_okf(c, &req, "give failed", ",\"n\":1,\"pid\":%d}\n",
				multiplexor.panes[i].sh.pid);
			if (peak_sock_send(c->fd, ".", 1, multiplexor.panes[i].sh.fd))
				mux_handoff(&multiplexor, i, renderer);
			return;
		}
		n = 0;
		for (i = 0; i < PANE_MAX; i++) {
			if (!multiplexor.panes[i].used || multiplexor.panes[i].sh.fd == PEAK_HANDLE_INVALID)
				continue;
			n++;
		}
		if (!n) {
			ctl_reply_err(c, req.id, req.id_n, "no pty");
			return;
		}
		ctl_okf(c, &req, "give failed", ",\"n\":%u}\n", n);
		sent = 0;
		for (i = 0; i < PANE_MAX && sent < n; i++) {
			if (!multiplexor.panes[i].used || multiplexor.panes[i].sh.fd == PEAK_HANDLE_INVALID)
				continue;
			if (!peak_sock_send(c->fd, ".", 1, multiplexor.panes[i].sh.fd))
				break;
			mux_handoff(&multiplexor, i, renderer);
			sent++;
		}
	} else if (req.op_n == 3 && memcmp(req.op, "hit", 3) == 0) {
		int px;
		int py;
		u32 cx;
		u32 cy;

		if (renderer && peak_pointer_local(&renderer->win, &px, &py)) {
			cell_at((float)px, (float)py, &cx, &cy);
			ctl_okf(c, &req, "hit failed", ",\"hit\":1,\"x\":%u,\"y\":%u}\n", cx, cy);
			return;
		}
		ctl_okf(c, &req, "hit failed", ",\"hit\":0}\n");
	} else {
		ctl_reply_err(c, req.id, req.id_n, "unknown op");
	}
}

void
ctl_client_read(VtCtlClient *c)
{
	for (;;) {
		int r;
		u32 i;

		{
			PEAK_HANDLE got;

			got = PEAK_HANDLE_INVALID;
			r = peak_sock_recv(c->fd, c->buf + c->n, sizeof c->buf - c->n, &got);
			if (got != PEAK_HANDLE_INVALID) {
				if (c->pass != PEAK_HANDLE_INVALID)
					peak_fd_close(c->pass);
				c->pass = got;
			}
		}
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
				ctl_handle_line(c, c->buf + i);
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
				ctl_reply_err(c, NULL, 0, "line too long");
				ctl_client_close(c);
				return;
			}
			continue;
		}
		if (r < 0)
			return;
		ctl_client_close(c);
		return;
	}
}

void
ctl_accept(void)
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
		for (i = 0; i < CTL_CLIENTS; i++) {
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
		ctl_clients[slot].pass = PEAK_HANDLE_INVALID;
		ctl_clients[slot].n = 0;
	}
}

u32
ctl_fds(PEAK_HANDLE *fds)
{
	u32 n;
	int i;

	n = 0;
	if (ctl_listen != PEAK_HANDLE_INVALID)
		fds[n++] = ctl_listen;
	for (i = 0; i < CTL_CLIENTS; i++) {
		if (ctl_clients[i].fd != PEAK_HANDLE_INVALID)
			fds[n++] = ctl_clients[i].fd;
	}
	if (ctl_job.fd != PEAK_HANDLE_INVALID)
		fds[n++] = ctl_job.fd;
	{
		PEAK_HANDLE ch;

		ch = peak_child_fd();
		if (ch != PEAK_HANDLE_INVALID)
			fds[n++] = ch;
	}
	return n;
}

void
ctl_pump(void)
{
	int i;

	ctl_accept();
	for (i = 0; i < CTL_CLIENTS; i++) {
		if (ctl_clients[i].fd != PEAK_HANDLE_INVALID)
			ctl_client_read(&ctl_clients[i]);
	}
	ctl_job_read();
	peak_child_ack();
	reap_children();
}

