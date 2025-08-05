#ifndef _VT_RENDERER_
#define _VT_RENDERER_

typedef struct Renderer Renderer;

typedef struct {
  uint32_t cell_size[2];
  uint32_t term_size[2];
  uint32_t top_left_margin[2];
  uint32_t blink_modulate;
  uint32_t margin_color;
  uint32_t strike_min;
  uint32_t strike_max;
  uint32_t underline_min;
  uint32_t underline_max;
} Renderer_Const_Buffer;

#define RENDERER_CELL_BLINK 0x80000000

typedef struct {
  uint32_t glyth_index;
  uint32_t foreground; 
  uint32_t background;  
} Renderer_Cell;

bool renderer_create(Renderer* r);
void renderer_draw_term(Renderer *r);
void renderer_destroy(Renderer *r);

#endif
