#pragma once

#define VT_CTL_CLIENTS 4
#define VT_CTL_LINE 8192
#define VT_CTL_JOB_OUT 65536

typedef struct {
	PEAK_HANDLE fd;
	u32 n;
	char buf[VT_CTL_LINE];
} VtCtlClient;

typedef struct {
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

typedef struct {
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

static PEAK_HANDLE ctl_listen = PEAK_HANDLE_INVALID;
static char ctl_path[256];
static VtCtlClient ctl_clients[VT_CTL_CLIENTS];
static VtCtlJob ctl_job;
#ifndef _WIN32
static PEAK_HANDLE vt_chld_r = PEAK_HANDLE_INVALID;
static int vt_chld_w = -1;
#endif

static void vt_ctl_init(void);
static void vt_ctl_destroy(void);
static void vt_ctl_client_close(VtCtlClient *c);
static u32 vt_ctl_fds(PEAK_HANDLE *fds);
static void vt_ctl_pump(void);
#ifndef _WIN32
static void vt_sigchld_handler(int sig);
static int vt_status_code(int status);
static void vt_reap_children(void);
#else
static void vt_ctl_job_reap(void);
#endif
static int vt_ctl_skip_ws(const char *s, int i);
static int vt_ctl_parse_string(const char *s, int i, const char **out, int *n);
static int vt_ctl_parse(const char *s, VtCtlReq *req);
static int vt_ctl_unescape(const char *s, int n, char *dst, size_t cap);
static int vt_ctl_put(PEAK_HANDLE fd, const char *p, size_t n);
static int vt_ctl_put_escaped(PEAK_HANDLE fd, const char *p, size_t n);
static int vt_ctl_put_prefix(PEAK_HANDLE fd, const char *id, int id_n, int ok);
static void vt_ctl_reply_err(VtCtlClient *c, const char *id, int id_n, const char *err);
static void vt_ctl_ok(VtCtlClient *c, const char *id, int id_n, const char *tail);
static void vt_ctl_okf(VtCtlClient *c, const VtCtlReq *req, const char *err, const char *fmt, ...);
static void vt_ctl_job_clear(void);
static void vt_ctl_job_finish(void);
static void vt_ctl_job_read(void);
static int vt_ctl_dump_put(void *ctx, const char *p, size_t n);
static void vt_ctl_handle_line(VtCtlClient *c, char *line);
static void vt_ctl_client_read(VtCtlClient *c);
static void vt_ctl_accept(void);

void
vt_ctl_init(void)
{
	char dir[192];
	PEAK_HANDLE fd;
	int i;
	int n;
#ifndef _WIN32
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
#endif
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

void
vt_ctl_destroy(void)
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
}

#ifndef _WIN32
void
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

int
vt_status_code(int status)
{
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return 1;
}
#endif

int
vt_ctl_skip_ws(const char *s, int i)
{
	while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')
		i++;
	return i;
}

int
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

int
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

int
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

int
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

int
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

int
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

void
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

void
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

void
vt_ctl_ok(VtCtlClient *c, const char *id, int id_n, const char *tail)
{
	if (c->fd == PEAK_HANDLE_INVALID)
		return;
	if (vt_ctl_put_prefix(c->fd, id, id_n, 1) < 0
			|| vt_ctl_put(c->fd, tail, strlen(tail)) < 0)
		vt_ctl_client_close(c);
}

void
vt_ctl_okf(VtCtlClient *c, const VtCtlReq *req, const char *err, const char *fmt, ...)
{
	char tail[80];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(tail, sizeof tail, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= sizeof tail) {
		vt_ctl_reply_err(c, req->id, req->id_n, err ? err : "reply failed");
		return;
	}
	vt_ctl_ok(c, req->id, req->id_n, tail);
}

void
vt_ctl_job_clear(void)
{
	ctl_job.dead = false;
	ctl_job.out_n = 0;
	ctl_job.trunc = false;
	ctl_job.client = -1;
	ctl_job.id_n = 0;
}

void
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
	vt_ctl_job_clear();
	return;
drop:
	vt_ctl_client_close(c);
	vt_ctl_job_clear();
}

#ifndef _WIN32
void
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
void
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

void
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

typedef struct {
	PEAK_HANDLE fd;
	u32 n;
	char buf[4096];
} VtCtlDump;

int
vt_ctl_dump_put(void *ctx, const char *p, size_t n)
{
	VtCtlDump *o;

	o = ctx;
	while (n) {
		u32 room;
		u32 k;

		room = (u32)sizeof o->buf - o->n;
		if (!room) {
			if (vt_ctl_put_escaped(o->fd, o->buf, o->n) < 0)
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

void
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
		VtCtlDump o;
		char mid[80];
		int n;

		s = term_screen(&term);
		if (!s || !s->cell_buffer) {
			vt_ctl_reply_err(c, req.id, req.id_n, "dump failed");
			return;
		}
		o.fd = c->fd;
		o.n = 0;
		n = snprintf(mid, sizeof mid, ",\"cols\":%u,\"rows\":%u,\"text\":\"",
				s->cols, s->rows);
		if (n < 0 || (size_t)n >= sizeof mid
				|| vt_ctl_put_prefix(c->fd, req.id, req.id_n, 1) < 0
				|| vt_ctl_put(c->fd, mid, (size_t)n) < 0
				|| vt_dump_walk(vt_ctl_dump_put, &o) < 0
				|| (o.n && vt_ctl_put_escaped(o.fd, o.buf, o.n) < 0)
				|| vt_ctl_put(c->fd, "\"}\n", strlen("\"}\n")) < 0)
			vt_ctl_client_close(c);
	} else if (req.op_n == 6 && memcmp(req.op, "cursor", 6) == 0) {
		vt_ctl_okf(c, &req, "cursor failed", ",\"x\":%u,\"y\":%u}\n",
				term.cursor.x, term.cursor.y);
	} else if (req.op_n == 4 && memcmp(req.op, "size", 4) == 0) {
		TermScreen *s;

		s = term_screen(&term);
		vt_ctl_okf(c, &req, "size failed", ",\"cols\":%u,\"rows\":%u}\n", s->cols, s->rows);
	} else if (req.op_n == 5 && memcmp(req.op, "write", 5) == 0) {
		char data[VT_CTL_LINE];
		int n;

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
		vt_ctl_okf(c, &req, "write failed", ",\"n\":%d}\n", (int)vt_sh_write(data, (size_t)n));
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
		vt_ctl_ok(c, req.id, req.id_n, "}\n");
	} else if (req.op_n == 9 && memcmp(req.op, "clipboard", 9) == 0) {
		char data[VT_CTL_LINE];
		int n;

		if (req.data) {
			n = vt_ctl_unescape(req.data, req.data_n, data, sizeof data);
			if (n < 0) {
				vt_ctl_reply_err(c, req.id, req.id_n, "bad json");
				return;
			}
			if (!peak_clip_set(VT_PEAK_WIN, PEAK_CLIP_CLIPBOARD, data, (size_t)n)) {
				vt_ctl_reply_err(c, req.id, req.id_n, "clipboard set failed");
				return;
			}
			vt_ctl_ok(c, req.id, req.id_n, "}\n");
		} else {
			size_t gn;
			char *got;

			if (!peak_clip_request(NULL, PEAK_CLIP_CLIPBOARD)) {
				vt_ctl_reply_err(c, req.id, req.id_n, "clipboard get failed");
				return;
			}
			got = malloc(VT_CLIP_MAX + 1);
			if (!got) {
				vt_ctl_reply_err(c, req.id, req.id_n, "clipboard get failed");
				return;
			}
			gn = 0;
			if (!peak_clip_take(NULL, got, VT_CLIP_MAX, &gn))
				gn = 0;
			if (vt_ctl_put_prefix(c->fd, req.id, req.id_n, 1) < 0
					|| vt_ctl_put(c->fd, ",\"data\":\"", strlen(",\"data\":\"")) < 0
					|| vt_ctl_put_escaped(c->fd, got, gn) < 0
					|| vt_ctl_put(c->fd, "\"}\n", strlen("\"}\n")) < 0)
				vt_ctl_client_close(c);
			free(got);
		}
	} else if (req.op_n == 3 && memcmp(req.op, "run", 3) == 0) {
		char cmd[VT_CTL_LINE];
		char cwd[512];
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
		vt_ctl_okf(c, &req, "run failed", ",\"job\":%u}\n", ctl_job.seq);
	} else {
		vt_ctl_reply_err(c, req.id, req.id_n, "unknown op");
	}
}

void
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

void
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

u32
vt_ctl_fds(PEAK_HANDLE *fds)
{
	u32 n;
	int i;

	n = 0;
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
	return n;
}

void
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

