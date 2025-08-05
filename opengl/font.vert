#version 330 core

layout (location = 0) in uint pos;
layout (location = 1) in uint glyth;
layout (location = 2) in uint foreground;
layout (location = 3) in uint background;

layout(std140) uniform const_block {
  uint offset;
  uint grid;
};

out VS_OUT {
  flat uint uv;    
  flat uint fg;
  flat uint bg;
} vs_out;

uvec2 unpack_xy(uint glyphIndex) {
  uint x = glyphIndex & 0xffffu;
  uint y = glyphIndex >> 16u;
  return uvec2(x, y);
}

// uniform block glyth table uv

void main() {
  vec2 corner = vec2(gl_VertexID&1, (gl_VertexID>>1)&1);

  gl_Position = vec4(corner, 0.0, 1.0);
  // vs_out.uv = fg;
  // vs_out.fg = fg;
  // vs_out.bg = bg;
}
