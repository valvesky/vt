#version 330 core

out vec4 color;

in VS_OUT {
  vec2 uv;    
  flat uint fg;
  flat uint bg;
} fs_in;

uniform sampler2D tex;

vec4 unpack_color(uint color) {
  float r = float(color & 0xffu) / 255.0;
  float g = float((color >> 8u) & 0xffu) / 255.0;
  float b = float((color >> 16u) & 0xffu) / 255.0;
  return vec4(r, g, b, 1.0);
}


void main() {
  color = vec4(1.0, 1.0, 1.0, texture(tex, fs_in.uv).r) * unpack_color(fs_in.fg);  
  // color = vec4(1.0, 0.0, 0.0, 1.0);  
}
