#version 330 core

out vec4 color;

uniform sampler2D atlas;

in vec2 TexCoords;
in vec4 FragBg;
in vec4 FragFg;

void main() {
  float mask = texture(atlas, TexCoords).r;
  color = mix(FragFg, FragBg, mask);
}  
