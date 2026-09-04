#ifndef _CONFIG_H_
#define _CONFIG_H_

static const char font_path[]          = "fonts/iosevka-mono.ttf";
static const char font_fallback_path[] = "/usr/share/fonts/noto/NotoSans-Regular.ttf";
static const char font_emoji_path[]    = "/usr/share/fonts/noto/NotoColorEmoji.ttf";
static const int font_size_px          = 20; 
static const float alpha        = 0.9; /* opacity */
static const bool vsync         = false;
static const bool vt_shell_fast_pipe = false;

/* PeakKeyCode / PeakKeyMod. CAPS ignored. Rebuild. */
static const PeakKeyCode mux_prefix_key = PEAK_KEY_B;
static const PeakKeyMod  mux_prefix_mod = PEAK_KEYMOD_CTRL;

/* After prefix. Letters/digits match the Peak key too. Arrows always focus. */
static const char mux_split_v[] = "%|5";
static const char mux_split_h[] = "\"-";
static const char mux_left[]    = "h";
static const char mux_right[]   = "l";
static const char mux_up[]      = "k";
static const char mux_down[]    = "j";
static const char mux_next[]    = "o";
static const char mux_kill[]    = "x";

static const PeakKeyCode clip_copy_key  = PEAK_KEY_C;
static const PeakKeyMod  clip_copy_mod  = PEAK_KEYMOD_CTRL | PEAK_KEYMOD_SHIFT;
static const PeakKeyCode clip_paste_key = PEAK_KEY_V;
static const PeakKeyMod  clip_paste_mod = PEAK_KEYMOD_CTRL | PEAK_KEYMOD_SHIFT;

typedef enum { BLACK = 0, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE = 7 } AnsiColor;

static const uint32_t ansi_fg[] = {
  [0] = 0x1d2021, /* hard contrast: #1d2021 / soft contrast: #32302f */
  [1] = 0xea6962, /* red     */
  [2] = 0xa9b665, /* green   */
  [3] = 0xd8a657, /* yellow  */
  [4] = 0x7daea3, /* blue    */
  [5] = 0xd3869b, /* magenta */
  [6] = 0x89b482, /* cyan    */
  [7] = 0xd4be98, /* white   */

  /* 8 bright colors */
  [8]  = 0x928374, /* black   */
  [9]  = 0xef938e, /* red     */
  [10] = 0xbbc585, /* green   */
  [11] = 0xe1bb7e, /* yellow  */
  [12] = 0x9dc2ba, /* blue    */
  [13] = 0xe1acbb, /* magenta */
  [14] = 0xa7c7a2, /* cyan    */
  [15] = 0xe2d3ba, /* white   */
};

static const uint32_t ansi_bg[] = {
  0x1D2021, // 40: Black
  0x800000, // 41: Red
  0x008000, // 42: Green
  0x808000, // 43: Yellow
  0x000080, // 44: Blue
  0x800080, // 45: Magenta
  0x008080, // 46: Cyan
  0xC8C8C8  // 47: White
};

const AnsiColor bg_color = BLACK;
const AnsiColor fg_color = WHITE;

#endif
