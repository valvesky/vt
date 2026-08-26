#define _GNU_SOURCE

#ifdef DEBUG
#define P_LOG_DEBUG_ENABLED 1
#define P_LOG_TRACE_ENABLED 1
#endif

#include "term.h"
#include "term.c"

#ifndef VT_HEADLESS
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "rend.h"
#endif
#include "peak.c"
#ifndef VT_HEADLESS
#include "rend.c"
#pragma GCC diagnostic pop
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "lib/stb_truetype.h"

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <immintrin.h>
#include <emmintrin.h>

#include "vt_circ_buf.c"
#include "vt.h"
#include "config.h"
#include "vt_debug.h"
#include "vt_renderer.c"

typedef struct Line {
  u64 start;
  u32 len;
  bool control_codes;
  bool high_bit;
} Line;

#define VT_CTL_CLIENTS 4
#define VT_CTL_LINE 8192
#define VT_CTL_JOB_OUT 65536
#define VT_WHEEL 3

typedef struct VtCtlClient {
  PEAK_HANDLE fd;
  u32 n;
  char buf[VT_CTL_LINE];
} VtCtlClient;

typedef struct VtCtlReq {
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

typedef struct VtCtlJob {
  int pid;
  PEAK_HANDLE fd;
  int client;
  int id_n;
  u32 seq;
  u32 out_n;
  bool trunc;
  char id[96];
  char out[VT_CTL_JOB_OUT];
} VtCtlJob;

static Term term;
static CBuffer scrollback;
static PeakProc sh = { PEAK_HANDLE_INVALID, 0 };
static PEAK_HANDLE ctl_listen = PEAK_HANDLE_INVALID;
static char ctl_path[256];
static VtCtlClient ctl_clients[VT_CTL_CLIENTS];
static VtCtlJob ctl_job;

static bool running = true;
static bool vt_headless;
static u32 view_off;

static bool vt_init_core(u32 cols, u32 rows);
static bool vt_init(u32 cols, u32 rows);
static void vt_destroy(void);
static void vt_feed(const char *data, size_t len);
static int vt_utf8_encode(codepoint_t c, char out[4]);
static void vt_dump_screen(FILE *out);
static int vt_headless_run(const char *path, const char *shot, u32 cols, u32 rows);
static int vt_headless_live_run(u32 cols, u32 rows);
#ifndef VT_HEADLESS
static void vt_resize(u32 cols, u32 rows);
static void vt_cmd_putc(codepoint_t c);
#endif
static void vt_shell_gone(void);
static size_t vt_sh_read(void);
static size_t vt_sh_write(const char *const src, size_t len);
static void vt_ingest(const char *data, size_t len);
static void vt_flush_reply(void);
#ifndef VT_HEADLESS
static void vt_key(const PeakEvent *event);
#endif
static void vt_line_feed(Line line);
static void vt_parse_input(void);
#ifndef VT_HEADLESS
static void vt_mouse_wheel(const PeakEvent *event);
static void vt_peak_pump(bool *dirty);
#endif
static void vt_wait(bool ready);
#ifndef VT_HEADLESS
static void vt_draw_cells(const TermCell *row, u32 cols, u32 y);
static void vt_present(void);
#endif
static int vt_ctl_skip_ws(const char *s, int i);
static int vt_ctl_parse_string(const char *s, int i, const char **out, int *n);
static int vt_ctl_parse(const char *s, VtCtlReq *req);
static int vt_ctl_op_eq(const char *op, int n, const char *lit);
static int vt_ctl_hex(char c);
static int vt_ctl_unescape(const char *s, int n, char *dst, size_t cap);
static int vt_ctl_put(int fd, const char *p, size_t n);
static int vt_ctl_puts(int fd, const char *s);
static int vt_ctl_put_escaped(int fd, const char *p, size_t n);
static int vt_ctl_put_prefix(int fd, const char *id, int id_n, int ok);
static void vt_ctl_client_close(VtCtlClient *c);
static void vt_ctl_reply_err(VtCtlClient *c, const char *id, int id_n, const char *err);
static void vt_ctl_op_size(VtCtlClient *c, const char *id, int id_n);
static void vt_ctl_op_cursor(VtCtlClient *c, const char *id, int id_n);
static void vt_ctl_op_dump(VtCtlClient *c, const char *id, int id_n);
static void vt_ctl_op_write(VtCtlClient *c, const char *id, int id_n, const char *raw, int raw_n);
static void vt_ctl_op_screenshot(VtCtlClient *c, const char *id, int id_n, const char *raw, int raw_n);
static void vt_ctl_op_run(VtCtlClient *c, const char *id, int id_n, const char *raw, int raw_n);
static void vt_ctl_job_reap(void);
static void vt_ctl_job_read(void);
static void vt_ctl_job_kill(void);
static void vt_ctl_handle_line(VtCtlClient *c, char *line);
static void vt_ctl_client_read(VtCtlClient *c);
static void vt_ctl_accept(void);
static void vt_ctl_pump(void);
static bool vt_ctl_init(void);
static void vt_ctl_destroy(void);

static bool
vt_init_core(u32 cols, u32 rows)
{
  TermColors colors;

  memset(&colors, 0, sizeof colors);
  memcpy(colors.fg, ansi_fg, sizeof colors.fg);
  memcpy(colors.bg, ansi_bg, sizeof colors.bg);
  colors.fg_default = (uint32_t)fg_color;
  colors.bg_default = (uint32_t)bg_color;

  sh.fd = PEAK_HANDLE_INVALID;
  sh.pid = 0;
  cbuffer_init(&scrollback, peak_page_size() * 3);
  if (!term_init(&term, cols, rows, &colors)) {
    cbuffer_destroy(&scrollback);
    memset(&scrollback, 0, sizeof scrollback);
    return false;
  }
  return true;
}

static bool
vt_init(u32 cols, u32 rows)
{
  static const char *argv[] = { "bash", "--login", NULL };
  PeakProc proc;

  if (!vt_init_core(cols, rows))
    return false;

  setenv("TERM", "xterm-256color", 1);
  unsetenv("COLUMNS");
  unsetenv("LINES");
  proc = peak_pty_spawn("bash", argv, cols, rows,
      cols * atlas.cell_width, rows * atlas.cell_height);
  if (proc.fd == PEAK_HANDLE_INVALID) {
    VTFATAL("Could not open tty.");
    return false;
  }
  sh = proc;
  if (!vt_ctl_init())
    VTERROR("ctl socket disabled");
  return true;
}

static void
vt_destroy(void)
{
  vt_ctl_destroy();
  VTINFO("[Shell %-d] Exited successfully", sh.pid);
  peak_pty_close(&sh);
  term_destroy(&term);
  if (scrollback.buffer)
    cbuffer_destroy(&scrollback);
  memset(&scrollback, 0, sizeof scrollback);
}

#ifndef VT_HEADLESS
static void
vt_resize(u32 cols, u32 rows)
{
  TermScreen *s;

  if (!cols || !rows)
    return;
  s = term_screen(&term);
  if (s && s->cols == cols && s->rows == rows)
    return;
  term_resize(&term, cols, rows);
  if (view_off > term_hist_count(&term))
    view_off = term_hist_count(&term);
  peak_pty_resize(&sh, cols, rows,
      cols * atlas.cell_width, rows * atlas.cell_height);
}

static void
vt_cmd_putc(codepoint_t c)
{
  char ch = (char)c;
  vt_sh_write(&ch, 1);
}
#endif

static void
vt_flush_reply(void)
{
  if (!term.reply_n || sh.fd == PEAK_HANDLE_INVALID)
    return;
  if (peak_fd_write(sh.fd, term.reply, term.reply_n) <= 0)
    VTFATAL("Failed to write to the shell");
  term.reply_n = 0;
}

#ifndef VT_HEADLESS
static void
vt_key(const PeakEvent *event)
{
  PeakKeyCode key;
  PeakKeyMod mod;
  uint32_t code;
  char ch;

  key = event->key.key;
  mod = event->key.mod;
  code = event->key.code;

  switch (key) {
  case PEAK_KEY_ENTER:
    vt_cmd_putc('\r');
    return;
  case PEAK_KEY_BACKSPACE:
    vt_sh_write("\x7f", 1);
    return;
  case PEAK_KEY_TAB:
    vt_cmd_putc('\t');
    return;
  case PEAK_KEY_ESCAPE:
    vt_cmd_putc('\x1b');
    return;
  case PEAK_KEY_DELETE:
    vt_sh_write("\033[3~", 4);
    return;
  case PEAK_KEY_UP:
    vt_sh_write("\033[A", 3);
    return;
  case PEAK_KEY_DOWN:
    vt_sh_write("\033[B", 3);
    return;
  case PEAK_KEY_LEFT:
    vt_sh_write("\033[D", 3);
    return;
  case PEAK_KEY_RIGHT:
    vt_sh_write("\033[C", 3);
    return;
  default:
    break;
  }

  if (mod == PEAK_KEYMOD_CTRL) {
    if (key >= PEAK_KEY_A && key <= PEAK_KEY_Z) {
      ch = (char)(1 + (key - PEAK_KEY_A));
      vt_sh_write(&ch, 1);
      return;
    }
    if (code >= 1 && code < 32) {
      ch = (char)code;
      vt_sh_write(&ch, 1);
      return;
    }
  }

  if (code >= 32 && code < 127) {
    ch = (char)code;
    vt_sh_write(&ch, 1);
    return;
  }

  if (key >= PEAK_KEY_0 && key <= PEAK_KEY_9) {
    ch = (char)('0' + (key - PEAK_KEY_0));
    vt_sh_write(&ch, 1);
    return;
  }
  if (key >= PEAK_KEY_A && key <= PEAK_KEY_Z) {
    ch = (char)('a' + (key - PEAK_KEY_A));
    if (mod == PEAK_KEYMOD_SHIFT || mod == PEAK_KEYMOD_CAPS)
      ch = (char)(ch - 32);
    vt_cmd_putc((codepoint_t)ch);
  }
}
#endif

static void
vt_shell_gone(void)
{
  if (sh.pid > 0)
    peak_pty_reap(&sh);
  running = false;
}

static size_t
vt_sh_read(void)
{
  size_t total;

  total = 0;
  if (sh.fd == PEAK_HANDLE_INVALID)
    return 0;
  for (;;) {
    int r;
    char data[16000];

    r = peak_fd_read(sh.fd, data, sizeof data);
    if (r > 0) {
      vt_ingest(data, (size_t)r);
      total += (size_t)r;
      continue;
    }
    if (r == 0) {
      vt_shell_gone();
      break;
    }
    break;
  }
  return total;
}

static size_t
vt_sh_write(const char *const src, size_t len)
{
  int r;

  if (sh.fd == PEAK_HANDLE_INVALID)
    return 0;
  r = peak_fd_write(sh.fd, src, len);
  if (r <= 0) {
    if (r == 0)
      vt_shell_gone();
    return 0;
  }
  return (size_t)r;
}

#ifndef VT_HEADLESS
static void
vt_mouse_wheel(const PeakEvent *event)
{
  u32 cols, rows;
  u32 cw, ch;
  int x, y;
  int btn;
  char buf[32];
  int n;

  renderer_get_grid(&renderer, &cols, &rows);
  cw = atlas.cell_width;
  ch = atlas.cell_height;
  if (!cw || !ch || !cols || !rows)
    return;
  x = (int)event->pointer.x / (int)cw + 1;
  y = (int)event->pointer.y / (int)ch + 1;
  if (x < 1)
    x = 1;
  if (y < 1)
    y = 1;
  if (x > (int)cols)
    x = (int)cols;
  if (y > (int)rows)
    y = (int)rows;
  btn = event->pointer.type == PEAK_POINTER_WHEEL_UP ? 64 : 65;
  if (term.mode & TERM_MODE_MOUSESGR) {
    n = snprintf(buf, sizeof buf, "\033[<%d;%d;%dM", btn, x, y);
    if (n > 0)
      vt_sh_write(buf, (size_t)n);
    return;
  }
  if (x > 223)
    x = 223;
  if (y > 223)
    y = 223;
  buf[0] = '\033';
  buf[1] = '[';
  buf[2] = 'M';
  buf[3] = (char)(32 + btn);
  buf[4] = (char)(32 + x);
  buf[5] = (char)(32 + y);
  vt_sh_write(buf, 6);
}

static void
vt_peak_pump(bool *dirty)
{
  PeakEvent event;

  while (peak_window_epoll(&win, &event)) {
    switch (event.type) {
    case PEAK_EVENT_WINDOW_CLOSE:
      running = false;
      break;
    case PEAK_EVENT_WINDOW_RESIZE:
      renderer_resize(event.resize.width, event.resize.height);
      if (sh.fd != PEAK_HANDLE_INVALID) {
        u32 cols, rows;

        renderer_get_grid(&renderer, &cols, &rows);
        vt_resize(cols, rows);
      }
      if (dirty)
        *dirty = true;
      break;
    case PEAK_EVENT_KEY_DOWN:
      if (view_off) {
        view_off = 0;
        if (dirty)
          *dirty = true;
      }
      vt_key(&event);
      break;
    case PEAK_EVENT_POINTER:
      if (event.pointer.state == PEAK_POINTER_PRESSED
          && (event.pointer.type == PEAK_POINTER_WHEEL_UP
              || event.pointer.type == PEAK_POINTER_WHEEL_DOWN)) {
        if (term.mode & TERM_MODE_MOUSE) {
          vt_mouse_wheel(&event);
        } else if (term.mode & TERM_MODE_ALTSCREEN) {
          if (event.pointer.type == PEAK_POINTER_WHEEL_UP)
            vt_sh_write("\033[A", 3);
          else
            vt_sh_write("\033[B", 3);
        } else {
          u32 maxh;

          maxh = term_hist_count(&term);
          if (event.pointer.type == PEAK_POINTER_WHEEL_UP) {
            if (view_off + VT_WHEEL > maxh)
              view_off = maxh;
            else
              view_off += VT_WHEEL;
          } else if (view_off > VT_WHEEL) {
            view_off -= VT_WHEEL;
          } else {
            view_off = 0;
          }
          if (dirty)
            *dirty = true;
        }
      }
      break;
    default:
      break;
    }
  }
}
#endif

static int
vt_ctl_skip_ws(const char *s, int i)
{
  while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')
    i++;
  return i;
}

static int
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

static int
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

static int
vt_ctl_hex(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static int
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
      char tmp[4];
      int tn;

      if (i + 4 >= n)
        return -1;
      v = 0;
      for (k = 0; k < 4; k++) {
        h = vt_ctl_hex(s[i + 1 + k]);
        if (h < 0)
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

static int
vt_ctl_op_eq(const char *op, int n, const char *lit)
{
  int i;

  if (!op || n != (int)strlen(lit))
    return 0;
  for (i = 0; i < n; i++) {
    if (op[i] != lit[i])
      return 0;
  }
  return 1;
}

static int
vt_ctl_put(PEAK_HANDLE fd, const char *p, size_t n)
{
  while (n) {
    int w;

    w = peak_fd_write(fd, p, n);
    if (w < 0)
      return -1;
    if (w == 0)
      return -1;
    p += (size_t)w;
    n -= (size_t)w;
  }
  return 0;
}

static int
vt_ctl_puts(PEAK_HANDLE fd, const char *s)
{
  return vt_ctl_put(fd, s, strlen(s));
}

static int
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

static int
vt_ctl_put_prefix(PEAK_HANDLE fd, const char *id, int id_n, int ok)
{
  if (vt_ctl_put(fd, "{", 1) < 0)
    return -1;
  if (id && id_n > 0) {
    if (vt_ctl_puts(fd, "\"id\":") < 0)
      return -1;
    if (vt_ctl_put(fd, id, (size_t)id_n) < 0)
      return -1;
    if (vt_ctl_put(fd, ",", 1) < 0)
      return -1;
  }
  if (vt_ctl_puts(fd, "\"ok\":") < 0)
    return -1;
  return vt_ctl_puts(fd, ok ? "true" : "false");
}

static void
vt_ctl_client_close(VtCtlClient *c)
{
  if (!c || c->fd == PEAK_HANDLE_INVALID)
    return;
  if (ctl_job.client >= 0 && c == &ctl_clients[ctl_job.client])
    ctl_job.client = -1;
  peak_fd_close(c->fd);
  c->fd = PEAK_HANDLE_INVALID;
  c->n = 0;
}

static void
vt_ctl_reply_err(VtCtlClient *c, const char *id, int id_n, const char *err)
{
  if (c->fd == PEAK_HANDLE_INVALID)
    return;
  if (vt_ctl_put_prefix(c->fd, id, id_n, 0) < 0
      || vt_ctl_puts(c->fd, ",\"error\":\"") < 0
      || vt_ctl_puts(c->fd, err) < 0
      || vt_ctl_puts(c->fd, "\"}\\n") < 0)
    vt_ctl_client_close(c);
}

static void
vt_ctl_op_size(VtCtlClient *c, const char *id, int id_n)
{
  TermScreen *s;
  char tail[64];
  int n;

  s = term_screen(&term);
  n = snprintf(tail, sizeof tail, ",\"cols\":%u,\"rows\":%u}\\n", s->cols, s->rows);
  if (n < 0 || (size_t)n >= sizeof tail) {
    vt_ctl_reply_err(c, id, id_n, "size failed");
    return;
  }
  if (vt_ctl_put_prefix(c->fd, id, id_n, 1) < 0
      || vt_ctl_put(c->fd, tail, (size_t)n) < 0)
    vt_ctl_client_close(c);
}

static void
vt_ctl_op_cursor(VtCtlClient *c, const char *id, int id_n)
{
  char tail[64];
  int n;

  n = snprintf(tail, sizeof tail, ",\"x\":%u,\"y\":%u}\\n",
      term.cursor.x, term.cursor.y);
  if (n < 0 || (size_t)n >= sizeof tail) {
    vt_ctl_reply_err(c, id, id_n, "cursor failed");
    return;
  }
  if (vt_ctl_put_prefix(c->fd, id, id_n, 1) < 0
      || vt_ctl_put(c->fd, tail, (size_t)n) < 0)
    vt_ctl_client_close(c);
}

static void
vt_ctl_op_dump(VtCtlClient *c, const char *id, int id_n)
{
  TermScreen *s;
  FILE *m;
  char *text;
  size_t len;
  char mid[80];
  int n;

  s = term_screen(&term);
  text = NULL;
  len = 0;
  m = open_memstream(&text, &len);
  if (!m) {
    vt_ctl_reply_err(c, id, id_n, "dump failed");
    return;
  }
  vt_dump_screen(m);
  if (fclose(m) != 0) {
    free(text);
    vt_ctl_reply_err(c, id, id_n, "dump failed");
    return;
  }
  n = snprintf(mid, sizeof mid, ",\"cols\":%u,\"rows\":%u,\"text\":\"",
      s->cols, s->rows);
  if (n < 0 || (size_t)n >= sizeof mid
      || vt_ctl_put_prefix(c->fd, id, id_n, 1) < 0
      || vt_ctl_put(c->fd, mid, (size_t)n) < 0
      || vt_ctl_put_escaped(c->fd, text, len) < 0
      || vt_ctl_puts(c->fd, "\"}\\n") < 0)
    vt_ctl_client_close(c);
  free(text);
}

static void
vt_ctl_op_write(VtCtlClient *c, const char *id, int id_n, const char *raw, int raw_n)
{
  char data[VT_CTL_LINE];
  char tail[32];
  int n, wn;

  if (!raw) {
    vt_ctl_reply_err(c, id, id_n, "missing data");
    return;
  }
  n = vt_ctl_unescape(raw, raw_n, data, sizeof data);
  if (n < 0) {
    vt_ctl_reply_err(c, id, id_n, "bad json");
    return;
  }
  if (sh.fd == PEAK_HANDLE_INVALID) {
    vt_ctl_reply_err(c, id, id_n, "no pty");
    return;
  }
  wn = (int)vt_sh_write(data, (size_t)n);
  n = snprintf(tail, sizeof tail, ",\"n\":%d}\\n", wn);
  if (n < 0 || (size_t)n >= sizeof tail
      || vt_ctl_put_prefix(c->fd, id, id_n, 1) < 0
      || vt_ctl_put(c->fd, tail, (size_t)n) < 0)
    vt_ctl_client_close(c);
}

static void
vt_ctl_op_screenshot(VtCtlClient *c, const char *id, int id_n, const char *raw, int raw_n)
{
  char path[VT_CTL_LINE];
  TermScreen *s;
  int n;

  if (!raw) {
    vt_ctl_reply_err(c, id, id_n, "missing path");
    return;
  }
  n = vt_ctl_unescape(raw, raw_n, path, sizeof path);
  if (n < 0) {
    vt_ctl_reply_err(c, id, id_n, "bad json");
    return;
  }
  if (n == 0 || path[0] == 0) {
    vt_ctl_reply_err(c, id, id_n, "empty path");
    return;
  }
  if (!atlas.atlas) {
    vt_ctl_reply_err(c, id, id_n, "no atlas");
    return;
  }
  s = term_screen(&term);
  if (!renderer_screenshot_ppm(s, term.cursor.x, term.cursor.y,
      (term.cursor.fg << 8) | term.cursor.attr, term.cursor.bg << 8, path)) {
    vt_ctl_reply_err(c, id, id_n, "screenshot failed");
    return;
  }
  if (vt_ctl_put_prefix(c->fd, id, id_n, 1) < 0
      || vt_ctl_puts(c->fd, "}\n") < 0)
    vt_ctl_client_close(c);
}

static void
vt_ctl_job_reap(void)
{
  PeakProc job;
  VtCtlClient *c;
  int code, n;
  char head[80];

  if (ctl_job.pid <= 0)
    return;
  job.fd = ctl_job.fd;
  job.pid = ctl_job.pid;
  if (!peak_job_reap(&job, &code))
    return;
  ctl_job.pid = 0;
  if (ctl_job.fd != PEAK_HANDLE_INVALID) {
    peak_fd_close(ctl_job.fd);
    ctl_job.fd = PEAK_HANDLE_INVALID;
  }
  if (ctl_job.client < 0 || ctl_job.client >= VT_CTL_CLIENTS)
    return;
  c = &ctl_clients[ctl_job.client];
  if (c->fd == PEAK_HANDLE_INVALID)
    return;
  n = snprintf(head, sizeof head, "{\"ev\":\"exit\",\"job\":%u,", ctl_job.seq);
  if (n < 0 || (size_t)n >= sizeof head
      || vt_ctl_put(c->fd, head, (size_t)n) < 0)
    goto drop;
  if (ctl_job.id_n > 0) {
    if (vt_ctl_puts(c->fd, "\"id\":") < 0
        || vt_ctl_put(c->fd, ctl_job.id, (size_t)ctl_job.id_n) < 0
        || vt_ctl_put(c->fd, ",", 1) < 0)
      goto drop;
  }
  n = snprintf(head, sizeof head, "\"code\":%d,\"out\":\"", code);
  if (n < 0 || (size_t)n >= sizeof head
      || vt_ctl_put(c->fd, head, (size_t)n) < 0
      || vt_ctl_put_escaped(c->fd, ctl_job.out, ctl_job.out_n) < 0)
    goto drop;
  if (ctl_job.trunc && vt_ctl_puts(c->fd, "\",\"trunc\":true}\\n") < 0)
    goto drop;
  if (!ctl_job.trunc && vt_ctl_puts(c->fd, "\"}\\n") < 0)
    goto drop;
  return;
drop:
  vt_ctl_client_close(c);
}

static void
vt_ctl_job_read(void)
{
  if (ctl_job.pid <= 0 || ctl_job.fd == PEAK_HANDLE_INVALID)
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
    vt_ctl_job_reap();
    return;
  }
}

static void
vt_ctl_job_kill(void)
{
  PeakProc job;

  job.fd = ctl_job.fd;
  job.pid = ctl_job.pid;
  peak_job_kill(&job);
  ctl_job.fd = PEAK_HANDLE_INVALID;
  ctl_job.pid = 0;
  ctl_job.client = -1;
  ctl_job.out_n = 0;
  ctl_job.trunc = false;
}

static void
vt_ctl_op_run(VtCtlClient *c, const char *id, int id_n, const char *raw, int raw_n)
{
  char cmd[VT_CTL_LINE];
  char cwd[512];
  char tail[48];
  const char *dir;
  PeakProc job;
  int n;

  if (!raw) {
    vt_ctl_reply_err(c, id, id_n, "missing cmd");
    return;
  }
  n = vt_ctl_unescape(raw, raw_n, cmd, sizeof cmd);
  if (n < 0) {
    vt_ctl_reply_err(c, id, id_n, "bad json");
    return;
  }
  if (n == 0) {
    vt_ctl_reply_err(c, id, id_n, "empty cmd");
    return;
  }
  if (ctl_job.pid > 0) {
    vt_ctl_reply_err(c, id, id_n, "busy");
    return;
  }
  dir = NULL;
  if (sh.pid > 0 && peak_pid_cwd(sh.pid, cwd, sizeof cwd))
    dir = cwd;
  job = peak_job_run(cmd, dir);
  if (job.fd == PEAK_HANDLE_INVALID) {
    vt_ctl_reply_err(c, id, id_n, "fork failed");
    return;
  }
  ctl_job.pid = job.pid;
  ctl_job.fd = job.fd;
  ctl_job.client = (int)(c - ctl_clients);
  if (id && id_n > 0 && (size_t)id_n < sizeof ctl_job.id) {
    memcpy(ctl_job.id, id, (size_t)id_n);
    ctl_job.id_n = id_n;
  } else {
    ctl_job.id_n = 0;
  }
  ctl_job.out_n = 0;
  ctl_job.trunc = false;
  ctl_job.seq++;
  if (ctl_job.seq == 0)
    ctl_job.seq = 1;
  n = snprintf(tail, sizeof tail, ",\"job\":%u}\\n", ctl_job.seq);
  if (n < 0 || (size_t)n >= sizeof tail
      || vt_ctl_put_prefix(c->fd, id, id_n, 1) < 0
      || vt_ctl_put(c->fd, tail, (size_t)n) < 0)
    vt_ctl_client_close(c);
}

static void
vt_ctl_handle_line(VtCtlClient *c, char *line)
{
  VtCtlReq req;

  if (!line[0])
    return;
  if (!vt_ctl_parse(line, &req) || !req.op) {
    vt_ctl_reply_err(c, req.id, req.id_n, "bad json");
    return;
  }
  if (vt_ctl_op_eq(req.op, req.op_n, "dump"))
    vt_ctl_op_dump(c, req.id, req.id_n);
  else if (vt_ctl_op_eq(req.op, req.op_n, "cursor"))
    vt_ctl_op_cursor(c, req.id, req.id_n);
  else if (vt_ctl_op_eq(req.op, req.op_n, "size"))
    vt_ctl_op_size(c, req.id, req.id_n);
  else if (vt_ctl_op_eq(req.op, req.op_n, "write"))
    vt_ctl_op_write(c, req.id, req.id_n, req.data, req.data_n);
  else if (vt_ctl_op_eq(req.op, req.op_n, "screenshot"))
    vt_ctl_op_screenshot(c, req.id, req.id_n, req.path, req.path_n);
  else if (vt_ctl_op_eq(req.op, req.op_n, "run"))
    vt_ctl_op_run(c, req.id, req.id_n, req.cmd, req.cmd_n);
  else
    vt_ctl_reply_err(c, req.id, req.id_n, "unknown op");
}

static void
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

static void
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

static void
vt_ctl_pump(void)
{
  int i;

  vt_ctl_accept();
  for (i = 0; i < VT_CTL_CLIENTS; i++) {
    if (ctl_clients[i].fd != PEAK_HANDLE_INVALID)
      vt_ctl_client_read(&ctl_clients[i]);
  }
  vt_ctl_job_read();
  vt_ctl_job_reap();
}

static bool
vt_ctl_init(void)
{
  char dir[192];
  PEAK_HANDLE fd;
  int i;
  int n;

  ctl_listen = PEAK_HANDLE_INVALID;
  ctl_path[0] = 0;
  ctl_job.pid = 0;
  ctl_job.fd = PEAK_HANDLE_INVALID;
  ctl_job.client = -1;
  ctl_job.id_n = 0;
  ctl_job.out_n = 0;
  ctl_job.trunc = false;
  for (i = 0; i < VT_CTL_CLIENTS; i++) {
    ctl_clients[i].fd = PEAK_HANDLE_INVALID;
    ctl_clients[i].n = 0;
  }

  if (!peak_runtime_dir(dir, sizeof dir, "vt"))
    return false;
  n = snprintf(ctl_path, sizeof ctl_path, "%s/%d.sock", dir, (int)getpid());
  if (n < 0 || (size_t)n >= sizeof ctl_path) {
    ctl_path[0] = 0;
    return false;
  }
  fd = peak_sock_listen(ctl_path);
  if (fd == PEAK_HANDLE_INVALID) {
    VTERROR("ctl bind %s", ctl_path);
    ctl_path[0] = 0;
    return false;
  }
  ctl_listen = fd;
  return true;
}

static void
vt_ctl_destroy(void)
{
  int i;

  vt_ctl_job_kill();
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
}

static void
vt_wait(bool ready)
{
  PEAK_HANDLE fds[2 + VT_CTL_CLIENTS + 1];
  uint32_t n;
  int i;
#ifndef VT_HEADLESS
  PeakWindow *w;
#endif

  n = 0;
  if (sh.fd != PEAK_HANDLE_INVALID)
    fds[n++] = sh.fd;
  if (ctl_listen != PEAK_HANDLE_INVALID)
    fds[n++] = ctl_listen;
  for (i = 0; i < VT_CTL_CLIENTS; i++) {
    if (ctl_clients[i].fd != PEAK_HANDLE_INVALID)
      fds[n++] = ctl_clients[i].fd;
  }
  if (ctl_job.fd != PEAK_HANDLE_INVALID)
    fds[n++] = ctl_job.fd;
#ifndef VT_HEADLESS
  w = vt_headless ? NULL : &win;
  peak_wait(w, fds, n, ready ? 0 : -1);
#else
  peak_wait(NULL, fds, n, ready ? 0 : -1);
#endif
}

#ifndef VT_HEADLESS
static void
vt_draw_cells(const TermCell *row, u32 cols, u32 y)
{
  u32 x;

  if (!row)
    return;
  for (x = 0; x < cols; x++) {
    const TermCell *c;
    codepoint_t cp;

    c = &row[x];
    if (!c->codepoint && !c->fg && !c->bg)
      continue;
    cp = c->codepoint ? c->codepoint : (codepoint_t)' ';
    renderer_draw_codepoint(cp, x, y, c->fg, c->bg);
  }
}

static void
vt_present(void)
{
  TermScreen *scr;
  codepoint_t cp;
  color_packed_t fg, bg;
  u32 idx;

  scr = term_screen(&term);
  if (view_off && !(term.mode & TERM_MODE_ALTSCREEN)) {
    u32 y;

    for (y = 0; y < scr->rows; y++) {
      int src;

      src = (int)y - (int)view_off;
      if (src < 0)
        vt_draw_cells(term_hist_line(&term, (u32)(-src - 1)), scr->cols, y);
      else
        vt_draw_cells(scr->cell_buffer + (u32)src * scr->cols, scr->cols, y);
    }
  } else {
    renderer_draw_screen(scr);
    if (!(term.mode & TERM_MODE_HIDE)) {
      idx = term.cursor.x + term.cursor.y * scr->cols;
      if (idx < scr->capacity) {
        renderer_cell_cursor(&scr->cell_buffer[idx],
            (term.cursor.fg << 8) | term.cursor.attr, term.cursor.bg << 8,
            &cp, &fg, &bg);
        renderer_draw_codepoint(cp, term.cursor.x, term.cursor.y, fg, bg);
      }
    }
  }
  renderer_sync();
}
#endif

static void
vt_line_feed(Line line)
{
  char *ptr;

  ptr = &scrollback.buffer[line.start % scrollback.buffer_size];
  if (!(line.control_codes || line.high_bit || (term.state & TERM_ESC_START) || term.utf8_rem))
    term_feed_ascii(&term, ptr, line.len);
  else
    term_feed(&term, ptr, line.len);
}

static void
vt_parse_input(void)
{
  CBuffer *sc;
  __m128i utf8;
  __m128i nl;
  __m128i esc;
  char *ll_beg;
  char *data;
  char *ll_end;
  u32 remaining;

  sc = &scrollback;
  if (sc->read == sc->write)
    return;

  utf8 = _mm_set1_epi8((char)0x80);
  nl = _mm_set1_epi8('\n');
  esc = _mm_set1_epi8(0x1B);

  ll_beg = sc->buffer + (sc->read % sc->buffer_size);
  data = ll_beg;
  ll_end = ll_beg;
  remaining = (u32)(sc->write - sc->read);

  while (remaining > 0) {
    Line ll;
    bool found_newline;

    ll.start = 0;
    ll.len = 0;
    ll.control_codes = false;
    ll.high_bit = false;
    found_newline = false;

    while (remaining >= 16) {
      __m128i batch;
      __m128i test_nl;
      __m128i test_esc;
      __m128i test_delim;
      __m128i test_utf;
      int delim_mask;

      batch = _mm_loadu_si128((const __m128i *)data);
      test_nl = _mm_cmpeq_epi8(batch, nl);
      test_esc = _mm_cmpeq_epi8(batch, esc);
      test_delim = _mm_or_si128(test_nl, test_esc);
      test_utf = _mm_and_si128(batch, utf8);
      delim_mask = _mm_movemask_epi8(test_delim);

      if (delim_mask) {
        unsigned int advance;
        u32 utf_mask;
        u32 prefix_mask;

        advance = __tzcnt_u32((unsigned int)delim_mask);
        utf_mask = (u32)_mm_movemask_epi8(test_utf);
        prefix_mask = 0;
        if (advance > 0)
          prefix_mask = utf_mask & ((1u << advance) - 1u);
        if (prefix_mask)
          ll.high_bit = true;
        if (data[advance] == 0x1B)
          ll.control_codes = true;
        data += advance;
        remaining -= (u32)advance;
        ll_end = data;
        break;
      }

      if ((u32)_mm_movemask_epi8(test_utf))
        ll.high_bit = true;
      data += 16;
      remaining -= 16;
      ll_end += 16;
    }

    while (remaining > 0) {
      unsigned char ch;

      ch = (unsigned char)*data++;
      remaining--;
      ll_end++;
      if (!ll.control_codes && ch == 0x1B)
        ll.control_codes = true;
      if (!ll.high_bit && (ch & 0x80u))
        ll.high_bit = true;
      if (ch == '\n') {
        found_newline = true;
        break;
      }
    }

    ll.len = (u32)(ll_end - ll_beg);
    ll.start = (u64)(ll_beg - sc->buffer);
    vt_line_feed(ll);
    if (!found_newline)
      break;
    ll_beg = data;
    ll_end = ll_beg;
  }

  sc->read = sc->write;
}

static void
vt_ingest(const char *data, size_t len)
{
  if (!data || !len)
    return;
  view_off = 0;
  cbuffer_push_overwrite(&scrollback, (char *)data, len);
  vt_parse_input();
  vt_flush_reply();
}

static void
vt_feed(const char *data, size_t len)
{
  vt_ingest(data, len);
}

static int
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

static void
vt_dump_screen(FILE *out)
{
  TermScreen *s;
  u32 y, x, last_row;

  s = term_screen(&term);
  last_row = 0;
  for (y = 0; y < s->rows; y++) {
    for (x = 0; x < s->cols; x++) {
      if (s->cell_buffer[y * s->cols + x].codepoint)
        last_row = y;
    }
  }

  for (y = 0; y <= last_row; y++) {
    u32 last_col = 0;
    for (x = 0; x < s->cols; x++) {
      if (s->cell_buffer[y * s->cols + x].codepoint)
        last_col = x + 1;
    }
    for (x = 0; x < last_col; x++) {
      codepoint_t c = s->cell_buffer[y * s->cols + x].codepoint;
      char buf[4];
      int n;

      if (c == 0) {
        fputc(' ', out);
        continue;
      }
      n = vt_utf8_encode(c, buf);
      fwrite(buf, 1, (size_t)n, out);
    }
    fputc('\n', out);
  }
}

static int
vt_headless_run(const char *path, const char *shot, u32 cols, u32 rows)
{
  FILE *in;
  FILE *dump;
  FILE *devnull;
  TermScreen *scr;
  char buf[4096];
  size_t n;
  int saved;
  int rc;

  rc = 0;
  if (!vt_init_core(cols, rows))
    return 1;

  in = path ? fopen(path, "rb") : stdin;
  if (!in) {
    fprintf(stderr, "vt: cannot open %s\n", path);
    return 1;
  }

  saved = dup(STDOUT_FILENO);
  devnull = fopen("/dev/null", "w");
  if (saved >= 0 && devnull) {
    dup2(fileno(devnull), STDOUT_FILENO);
    fclose(devnull);
  }

  while ((n = fread(buf, 1, sizeof buf, in)) > 0)
    vt_feed(buf, n);
  if (path)
    fclose(in);

  dump = (saved >= 0) ? fdopen(saved, "w") : stdout;
  vt_dump_screen(dump ? dump : stdout);
  if (dump && dump != stdout)
    fclose(dump);

  if (shot) {
    if (!glyth_table_init(font_path, (float)font_size_px)) {
      fprintf(stderr, "vt: cannot load font %s\n", font_path);
      rc = 1;
    } else {
      scr = term_screen(&term);
      if (!renderer_screenshot_ppm(scr, term.cursor.x, term.cursor.y,
          (term.cursor.fg << 8) | term.cursor.attr, term.cursor.bg << 8, shot)) {
        fprintf(stderr, "vt: cannot write %s\n", shot);
        rc = 1;
      }
      glyth_table_destroy();
    }
  }

  vt_destroy();
  return rc;
}

static int
vt_headless_live_run(u32 cols, u32 rows)
{
  vt_headless = true;
  if (!vt_init(cols, rows)) {
    fprintf(stderr, "vt: headless live init failed\n");
    vt_destroy();
    return 1;
  }
  if (!glyth_table_init(font_path, (float)font_size_px)) {
    fprintf(stderr, "vt: cannot load font %s\n", font_path);
    vt_destroy();
    return 1;
  }
  while (running) {
    vt_wait(false);
    vt_ctl_pump();
    vt_sh_read();
  }
  glyth_table_destroy();
  vt_destroy();
  return 0;
}

int
main(int argc, char **argv)
{
  if (argc >= 2 && strcmp(argv[1], "--headless") == 0) {
    const char *path = NULL;
    const char *shot = NULL;
    char *end;
    u32 cols = 80;
    u32 rows = 24;
    int live = 0;
    int i;
    unsigned long v;

    for (i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--screenshot") == 0) {
        if (i + 1 >= argc) {
          fprintf(stderr, "vt: --screenshot needs a path\n");
          return 1;
        }
        shot = argv[++i];
      } else if (strcmp(argv[i], "--live") == 0) {
        live = 1;
      } else if (strcmp(argv[i], "--cols") == 0
          || strcmp(argv[i], "--rows") == 0) {
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
      } else {
        fprintf(stderr, "vt: extra arg %s\n", argv[i]);
        return 1;
      }
    }
    if (live)
      return vt_headless_live_run(cols, rows);
    return vt_headless_run(path, shot, cols, rows);
  }

#ifdef VT_HEADLESS
  fprintf(stderr, "vt: headless build; pass --headless\n");
  return 1;
#else
  if (!renderer_init()) {
    VTFATAL("Failed to initalize renderer!");
    renderer_destroy();
    return 1;
  }

  {
    u32 cols, rows;
    bool dirty;

    vt_peak_pump(NULL);
    if (!running) {
      renderer_destroy();
      return 0;
    }

    renderer_get_grid(&renderer, &cols, &rows);
    if (cols == 0)
      cols = 80;
    if (rows == 0)
      rows = 24;
    if (!vt_init(cols, rows)) {
      VTFATAL("Failed to initalize terminal!");
      vt_destroy();
      renderer_destroy();
      return 1;
    }

    dirty = true;
    while (running) {
      vt_wait(dirty);
      vt_peak_pump(&dirty);
      vt_ctl_pump();
      if (vt_sh_read())
        dirty = true;
      if (!running)
        break;
      if (dirty) {
        vt_present();
        dirty = false;
      }
    }
  }

  vt_destroy();
  renderer_destroy();
  VTINFO("Quit successfully!");
  return 0;
#endif
}
