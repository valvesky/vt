#version 330 core

layout (location = 0) in uint glyth; // x,y
layout (location = 1) in uint foreground;
layout (location = 2) in uint background;

layout(std140) uniform const_block {
  float grid_x; // gives us the padding
  float grid_y; 
  uint atlas_cell_width;
  uint atlas_cell_height;
  uint atlas_width;
  uint atlas_height;
} info;

out VS_OUT {
  vec2 uv;    
  flat uint fg;
  flat uint bg;
} vs_out;

vec2 unpack_xy(uint glyphIndex) {
  uint x = glyphIndex >> 16u;
  uint y = glyphIndex & 0xffffu;
  return vec2(x, y);
}

vec2 normalize_grid(vec2 grid_pos) {
  vec2 n = grid_pos / vec2(info.grid_x, info.grid_y);
  vec2 c = n*2.0 - 1.0;
  c.y = -c.y;
  return c;
}

void main() {
  vec2 corner = vec2(gl_VertexID&1, (gl_VertexID>>1)&1);

  int cols = int(info.grid_x); 

  vec2 term_cell  = vec2(gl_InstanceID % cols, gl_InstanceID / cols) + corner;

  vec2 term_ndc = normalize_grid(term_cell);
  gl_Position = vec4(term_ndc, 0.0, 1.0);

  vec2 atlas_cell = unpack_xy(glyth)  + corner;
  vec2 atlas_pos = atlas_cell * vec2(info.atlas_cell_width, info.atlas_cell_height);
  vs_out.uv = atlas_pos / vec2(info.atlas_width, info.atlas_height) ;
  vs_out.fg = foreground;
  vs_out.bg = background;
}
