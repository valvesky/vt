#ifndef _CONFIG_H_
#define _CONFIG_H_

static const float alpha = 0.7;

typedef enum { BLACK = 0, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE } AnsiColor;

static const uint32_t ansi_fg[] = {
  0x000000FF, // 30: Black
  0xDC3232FF, // 31: Red
  0x32B932FF, // 32: Green
  0xC8B932FF, // 33: Yellow
  0x3232DCFF, // 34: Blue
  0xC832C8FF, // 35: Magenta
  0x32B9B9FF, // 36: Cyan
  0xFFFFFFFF  // 37: White
};

static const uint32_t ansi_bg[] = {
  0x000000C8, // 40: Black
  0x800000C8, // 41: Red
  0x008000C8, // 42: Green
  0x808000C8, // 43: Yellow
  0x000080C8, // 44: Blue
  0x800080C8, // 45: Magenta
  0x008080C8, // 46: Cyan
  0xC8C8C8C8  // 47: White
};

#endif
