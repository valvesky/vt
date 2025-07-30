#version 330 core

layout (location = 0) in vec4 pos; // vec2 grid xy, vec2 
// layout (location = 1) in vec4 fg;  // rgb a+flags
// layout (location = 2) in vec4 fg;  // rgb a+flags

uniform vec2 grid;
out vec2 TexCoords;

vec2 ndc(vec2 v) {
  return  v*2.0 - 1.0;
}

void main() {
  vec2 uv = ndc(pos.xy / grid);
  gl_Position = vec4(uv, 0.0, 1.0);
  TexCoords = pos.zw;
}  
