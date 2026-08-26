#version 450

layout (location = 0) in uint pos;
layout (location = 1) in uint glyth;
layout (location = 2) in uint foreground;
layout (location = 3) in uint background;

layout(set = 0, binding = 0, std140) uniform ConstBlock {
  float grid_x;
  float grid_y;
  uint atlas_cell_width;
  uint atlas_cell_height;
  uint atlas_width;
  uint atlas_height;
  float alpha;
  uint _pad2;
} info;

layout(location = 0) out VS_OUT {
  vec2 uv;
  vec2 cell;
  flat uint fg;
  flat uint bg;
  float alpha;
} vs_out;

vec2 unpack_xy(uint packed) {
  uint x = packed >> 16u;
  uint y = packed & 0xffffu;
  return vec2(x, y);
}

vec2 normalize_grid(vec2 grid_pos) {
  vec2 n = grid_pos / vec2(info.grid_x, info.grid_y);
  return n * 2.0 - 1.0;
}

void main() {
  vec2 corner = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);
  vec2 term_ndc = normalize_grid(unpack_xy(pos) + corner);
  gl_Position = vec4(term_ndc, 0.0, 1.0);

  vec2 atlas_pos = (unpack_xy(glyth) + corner)
    * vec2(info.atlas_cell_width, info.atlas_cell_height);
  vs_out.uv = atlas_pos / vec2(info.atlas_width, info.atlas_height);
  vs_out.cell = corner;
  vs_out.fg = foreground;
  vs_out.bg = background;
  vs_out.alpha = info.alpha;
}
