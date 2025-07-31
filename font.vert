#version 330 core

#define RENDER_PASS_BG 0
#define RENDER_PASS_GLYTHS 1

/*  Glyth position    Atlas uv coords
 *     h
 *     |              +--------uv_max
 *     |              |           |
 *   (x,y) - - w      uv_min -----+
 */

layout (location = 0) in vec4 pos; // xy, w, h
layout (location = 1) in vec4 uv;  // uv_max, uv_min
layout (location = 2) in vec4 fg;  // rgb, a
layout (location = 3) in vec4 bg;  // rgb, a

uniform vec2 grid;
uniform int renderingPass;

out vec2 TexCoords;
out vec4 FragBg;
out vec4 FragFg;

vec2 grid_to_ndc(vec2 v) {
  return  (v/grid)*2.0 - 1.0;
}

void main() {

  /* We want the corner not the center
     |    so we can skip the usual -vec2(0.5)
     o-- */

  vec2 position = pos.xy;
  vec2 size = pos.zw;
  if (renderingPass == RENDER_PASS_BG) {
    position = floor(pos.xy);
    size = vec2(1);
  }

  vec2 corner = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
  vec2 corner_grid = corner * size + position + vec2(0.0, 0.5);

  gl_Position = vec4(grid_to_ndc(corner_grid), 0.0, 1.0);

  vec2 flip = corner;
  flip.x = 1.0 - corner.x;

  TexCoords = mix(uv.xy, uv.zw, flip);

  FragBg = fg;
  FragFg = bg;
}  
