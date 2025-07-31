#version 330 core

#define RENDER_PASS_BG 0
#define RENDER_PASS_GLYTHS 1

in vec2  TexCoords;
in vec4  FragBg;
in vec4  FragFg;

uniform sampler2D atlas;
uniform int renderingPass;

out vec4 color;


void main() {
  /*background pass */
  if (renderingPass == RENDER_PASS_BG) {
    color = FragBg;
  } else {
    float mask = texture(atlas, TexCoords).r;
    color = vec4(FragFg.rgb, FragFg.a * mask);
  }
}
