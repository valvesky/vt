#version 330 core

out vec4 color;

in VS_OUT {
  vec2 uv;    
  flat uint fg;
  flat uint bg;
} fs_in;

uniform sampler2D tex;

// vec3 unpack_color(uint packed) {
//   float r = float(packed & 0xffu) / 255.0;
//   float g = float((packed >> 8u) & 0xffu) / 255.0;
//   float b = float((packed >> 16u) & 0xffu) / 255.0;
//   return vec3(r, g, b);
// }


void main() {
  color = vec4(1.0, 1.0, 1.0, texture(tex, fs_in.uv).r);  
  // color = vec4(1.0, 0.0, 0.0, 1.0);  
}
