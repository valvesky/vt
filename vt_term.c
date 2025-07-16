/* === Terminal Interface === */
typedef struct {
  Screen sc;
  char cmd_buf[CMD_BUFSIZE];  // 512 bytes
  Shell sh;                   // 12 bytes (4 byte alignment)
  UTF8Decoder decoder;        // 8 bytes  (4 byte alignment)
  Glyth draw_state;
  uint16_t cursor_x;      // 2 bytes
  uint16_t cursor_y;      // 2 bytes
  uint16_t nlines;
} Term;

