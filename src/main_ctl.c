#define P_LOG_DEBUG_ENABLED 0
#define P_LOG_INFO_ENABLED 0
#define P_LOG_TRACE_ENABLED 0
#define P_LOG_WARN_ENABLED 0
#include "peak.h"
#include "peak.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	VTCTL_LINE = 8192,
	VTCTL_IN = 1 << 20,
	VTCTL_MS = 8000
};

static const char vtctl_hex[] = "0123456789abcdef";
static char vtctl_in[VTCTL_IN];
static int vtctl_in_n;
static int vtctl_in_off;

static void vtctl_usage(FILE *out);
static int vtctl_int(const char *s, int *out);
static int vtctl_join(char *dst, int cap, char **argv, int argc);
static int vtctl_put(char *dst, int cap, int n, const char *s);
static int vtctl_put_esc(char *dst, int cap, int n, const char *s);
static int vtctl_build(char *dst, int cap, const char *id, const char *op,
		int has_y, int y, int has_n, int nval, const char *data,
		const char *cmd, const char *path);
static PEAK_HANDLE vtctl_connect(const char *path);
static int vtctl_write_all(PEAK_HANDLE fd, const char *p, int n);
static int vtctl_read_line(PEAK_HANDLE fd, char *dst, int cap);
static int vtctl_fail(const char *msg);

void
vtctl_usage(FILE *out)
{
	fputs(
		"usage: vtctl [--sock PATH] [--id ID] <op> [args]\n"
		"       vtctl --help\n"
		"\n"
		"Drive a running vt / vt-live. Prints the JSONL reply.\n"
		"\n"
		"  read [Y N]              grid band (default 8 rows around cursor)\n"
		"  rg NEEDLE               substring per row\n"
		"  dump                    full grid\n"
		"  cursor                  cursor cell\n"
		"  size                    cols rows\n"
		"  write DATA              raw PTY bytes\n"
		"  run CMD                 off-grid sh -c\n"
		"  screenshot PATH         CPU atlas P6 PPM\n"
		"  clipboard [DATA]        get or set\n"
		"  split [v|h]             split pane (default v)\n"
		"  focus N                 focus pane\n"
		"  panes                   count + focus\n"
		"  move N [v|h|l|r|u|d|s]  focus N; split or swap\n"
		"  hit                     pointer in this window\n"
		"\n"
		"Socket: $XDG_RUNTIME_DIR/vt/latest.sock else /tmp/vt-<uid>/latest.sock\n"
		"Protocol: docs/agents/ctl.md\n",
		out);
}

int
vtctl_int(const char *s, int *out)
{
	char *end;
	unsigned long v;

	if (!s[0])
		return 0;
	v = strtoul(s, &end, 10);
	if (*end || v > 2147483647ul)
		return 0;
	*out = (int)v;
	return 1;
}

int
vtctl_join(char *dst, int cap, char **argv, int argc)
{
	int n;
	int i;
	int k;

	n = 0;
	for (i = 0; i < argc; i++) {
		if (i) {
			if (n + 1 >= cap)
				return -1;
			dst[n++] = ' ';
		}
		for (k = 0; argv[i][k]; k++) {
			if (n + 1 >= cap)
				return -1;
			dst[n++] = argv[i][k];
		}
	}
	if (n >= cap)
		return -1;
	dst[n] = 0;
	return n;
}

int
vtctl_put(char *dst, int cap, int n, const char *s)
{
	int i;

	for (i = 0; s[i]; i++) {
		if (n + 1 >= cap)
			return -1;
		dst[n++] = s[i];
	}
	return n;
}

int
vtctl_put_esc(char *dst, int cap, int n, const char *s)
{
	int i;
	unsigned char ch;

	if (n + 1 >= cap)
		return -1;
	dst[n++] = '"';
	for (i = 0; s[i]; i++) {
		ch = (unsigned char)s[i];
		if (ch == '"' || ch == '\\') {
			if (n + 2 >= cap)
				return -1;
			dst[n++] = '\\';
			dst[n++] = (char)ch;
		} else if (ch == '\n' || ch == '\r' || ch == '\t'
				|| ch == '\b' || ch == '\f') {
			if (n + 2 >= cap)
				return -1;
			dst[n++] = '\\';
			if (ch == '\n')
				dst[n++] = 'n';
			else if (ch == '\r')
				dst[n++] = 'r';
			else if (ch == '\t')
				dst[n++] = 't';
			else if (ch == '\b')
				dst[n++] = 'b';
			else
				dst[n++] = 'f';
		} else if (ch < 0x20) {
			if (n + 6 >= cap)
				return -1;
			dst[n++] = '\\';
			dst[n++] = 'u';
			dst[n++] = '0';
			dst[n++] = '0';
			dst[n++] = vtctl_hex[ch >> 4];
			dst[n++] = vtctl_hex[ch & 15];
		} else {
			if (n + 1 >= cap)
				return -1;
			dst[n++] = (char)ch;
		}
	}
	if (n + 1 >= cap)
		return -1;
	dst[n++] = '"';
	return n;
}

int
vtctl_build(char *dst, int cap, const char *id, const char *op,
		int has_y, int y, int has_n, int nval, const char *data,
		const char *cmd, const char *path)
{
	int n;
	char num[16];

	n = vtctl_put(dst, cap, 0, "{\"op\":");
	if (n < 0)
		return -1;
	n = vtctl_put_esc(dst, cap, n, op);
	if (n < 0)
		return -1;
	if (has_y) {
		if (snprintf(num, sizeof num, ",\"y\":%d", y) < 0)
			return -1;
		n = vtctl_put(dst, cap, n, num);
		if (n < 0)
			return -1;
	}
	if (has_n) {
		if (snprintf(num, sizeof num, ",\"n\":%d", nval) < 0)
			return -1;
		n = vtctl_put(dst, cap, n, num);
		if (n < 0)
			return -1;
	}
	if (data) {
		n = vtctl_put(dst, cap, n, ",\"data\":");
		if (n < 0)
			return -1;
		n = vtctl_put_esc(dst, cap, n, data);
		if (n < 0)
			return -1;
	}
	if (cmd) {
		n = vtctl_put(dst, cap, n, ",\"cmd\":");
		if (n < 0)
			return -1;
		n = vtctl_put_esc(dst, cap, n, cmd);
		if (n < 0)
			return -1;
	}
	if (path) {
		n = vtctl_put(dst, cap, n, ",\"path\":");
		if (n < 0)
			return -1;
		n = vtctl_put_esc(dst, cap, n, path);
		if (n < 0)
			return -1;
	}
	if (id && id[0]) {
		n = vtctl_put(dst, cap, n, ",\"id\":");
		if (n < 0)
			return -1;
		n = vtctl_put_esc(dst, cap, n, id);
		if (n < 0)
			return -1;
	}
	n = vtctl_put(dst, cap, n, "}\n");
	if (n < 0)
		return -1;
	dst[n] = 0;
	return n;
}

PEAK_HANDLE
vtctl_connect(const char *path)
{
	PEAK_HANDLE fd;
	uint64_t t0;

	t0 = peak_get_time();
	for (;;) {
		fd = peak_sock_connect(path);
		if (fd != PEAK_HANDLE_INVALID)
			return fd;
		if (peak_get_time() - t0 > 2ull * NANOS_PER_SEC)
			return PEAK_HANDLE_INVALID;
		peak_sleep_ns(50000000);
	}
}

int
vtctl_write_all(PEAK_HANDLE fd, const char *p, int n)
{
	int w;

	while (n > 0) {
		w = peak_fd_write(fd, p, (size_t)n);
		if (w < 0) {
			if (!peak_wait(NULL, &fd, 1, VTCTL_MS))
				return 0;
			continue;
		}
		if (w == 0)
			return 0;
		p += w;
		n -= w;
	}
	return 1;
}

int
vtctl_read_line(PEAK_HANDLE fd, char *dst, int cap)
{
	int r;
	int n;

	n = 0;
	while (vtctl_in_off < vtctl_in_n) {
		if (n + 1 >= cap)
			return -1;
		dst[n] = vtctl_in[vtctl_in_off++];
		if (dst[n] == '\n') {
			if (n && dst[n - 1] == '\r')
				n--;
			dst[n] = 0;
			return n;
		}
		n++;
	}
	vtctl_in_n = 0;
	vtctl_in_off = 0;
	for (;;) {
		r = peak_fd_read(fd, vtctl_in, sizeof vtctl_in);
		if (r < 0) {
			if (!peak_wait(NULL, &fd, 1, VTCTL_MS))
				return -2;
			continue;
		}
		if (r == 0) {
			if (!n)
				return 0;
			dst[n] = 0;
			return n;
		}
		vtctl_in_n = r;
		vtctl_in_off = 0;
		while (vtctl_in_off < vtctl_in_n) {
			if (n + 1 >= cap)
				return -1;
			dst[n] = vtctl_in[vtctl_in_off++];
			if (dst[n] == '\n') {
				if (n && dst[n - 1] == '\r')
					n--;
				dst[n] = 0;
				return n;
			}
			n++;
		}
		vtctl_in_n = 0;
		vtctl_in_off = 0;
	}
}

int
vtctl_fail(const char *msg)
{
	fprintf(stderr, "vtctl: %s\n", msg);
	return 1;
}

int
main(int argc, char **argv)
{
	char req[VTCTL_LINE];
	char reply[VTCTL_IN];
	char joined[VTCTL_LINE];
	char dir[192];
	char sock[256];
	const char *sock_path;
	const char *id;
	const char *op;
	const char *data;
	const char *cmd;
	const char *path;
	PEAK_HANDLE fd;
	int i;
	int y;
	int nval;
	int has_y;
	int has_n;
	int n;
	int rc;
	int wait_exit;

	sock_path = NULL;
	id = NULL;
	data = NULL;
	cmd = NULL;
	path = NULL;
	y = 0;
	nval = 0;
	has_y = 0;
	has_n = 0;
	wait_exit = 0;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			vtctl_usage(stdout);
			return 0;
		}
		if (argv[i][0] != '-')
			break;
		if (strcmp(argv[i], "--sock") == 0 || strcmp(argv[i], "--id") == 0) {
			if (i + 1 >= argc)
				return vtctl_fail("flag needs a value");
			if (argv[i][2] == 's')
				sock_path = argv[i + 1];
			else
				id = argv[i + 1];
			i++;
		} else {
			return vtctl_fail("unknown flag");
		}
	}
	if (i >= argc) {
		vtctl_usage(stderr);
		return 1;
	}
	op = argv[i++];
	if (strcmp(op, "help") == 0) {
		vtctl_usage(stdout);
		return 0;
	}
	if (strcmp(op, "read") == 0) {
		if (i < argc) {
			if (!vtctl_int(argv[i], &y))
				return vtctl_fail("bad y");
			has_y = 1;
			i++;
		}
		if (i < argc) {
			if (!vtctl_int(argv[i], &nval))
				return vtctl_fail("bad n");
			has_n = 1;
			i++;
		}
	} else if (strcmp(op, "rg") == 0 || strcmp(op, "write") == 0
			|| strcmp(op, "run") == 0) {
		if (i >= argc)
			return vtctl_fail("missing argument");
		if (vtctl_join(joined, sizeof joined, argv + i, argc - i) < 0)
			return vtctl_fail("argument too long");
		i = argc;
		if (op[0] == 'r' && op[1] == 'u')
			cmd = joined;
		else
			data = joined;
		wait_exit = op[0] == 'r' && op[1] == 'u';
	} else if (strcmp(op, "clipboard") == 0) {
		if (i < argc) {
			if (vtctl_join(joined, sizeof joined, argv + i, argc - i) < 0)
				return vtctl_fail("argument too long");
			data = joined;
			i = argc;
		}
	} else if (strcmp(op, "screenshot") == 0) {
		if (i >= argc)
			return vtctl_fail("missing path");
		path = argv[i++];
	} else if (strcmp(op, "split") == 0) {
		if (i < argc)
			data = argv[i++];
	} else if (strcmp(op, "focus") == 0) {
		if (i >= argc || !vtctl_int(argv[i], &nval))
			return vtctl_fail("missing pane");
		has_n = 1;
		i++;
	} else if (strcmp(op, "move") == 0) {
		if (i >= argc || !vtctl_int(argv[i], &nval))
			return vtctl_fail("missing pane");
		has_n = 1;
		i++;
		if (i < argc)
			data = argv[i++];
	} else if (strcmp(op, "dump") != 0 && strcmp(op, "cursor") != 0
			&& strcmp(op, "size") != 0 && strcmp(op, "panes") != 0
			&& strcmp(op, "hit") != 0) {
		return vtctl_fail("unknown op");
	}
	if (i != argc)
		return vtctl_fail("extra arg");
	n = vtctl_build(req, sizeof req, id, op, has_y, y, has_n, nval, data, cmd, path);
	if (n < 0 || n >= VTCTL_LINE)
		return vtctl_fail("request too long");
	if (!sock_path) {
		if (!peak_runtime_dir(dir, sizeof dir, "vt"))
			return vtctl_fail("no runtime dir");
		n = snprintf(sock, sizeof sock, "%s/latest.sock", dir);
		if (n < 0 || (size_t)n >= sizeof sock)
			return vtctl_fail("socket path too long");
		sock_path = sock;
	}
	fd = vtctl_connect(sock_path);
	if (fd == PEAK_HANDLE_INVALID) {
		fprintf(stderr, "vtctl: connect %s failed\n", sock_path);
		return 1;
	}
	if (!vtctl_write_all(fd, req, (int)strlen(req))) {
		peak_fd_close(fd);
		return vtctl_fail("send failed");
	}
	n = vtctl_read_line(fd, reply, sizeof reply);
	if (n < 0) {
		peak_fd_close(fd);
		return vtctl_fail(n == -2 ? "timeout" : "reply too long");
	}
	if (!n) {
		peak_fd_close(fd);
		return vtctl_fail("empty reply");
	}
	puts(reply);
	rc = strstr(reply, "\"ok\":false") ? 1 : 0;
	if (wait_exit && !rc) {
		n = vtctl_read_line(fd, reply, sizeof reply);
		if (n < 0) {
			peak_fd_close(fd);
			return vtctl_fail(n == -2 ? "timeout" : "reply too long");
		}
		if (!n) {
			peak_fd_close(fd);
			return vtctl_fail("empty reply");
		}
		puts(reply);
		if (strstr(reply, "\"ok\":false"))
			rc = 1;
	}
	peak_fd_close(fd);
	return rc;
}
