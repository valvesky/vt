#version 330 core
in vec2 TexCoords;
out vec4 color;

uniform sampler2D text;
uniform vec4 textColor;

// in vec4 fg;
// in vec4 bg;

void main() {    
    vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoords).r);
    color = sampled * vec4(1.0,0.0,0.0,1.0);
}  
