#version 450

layout(location = 0) in VS_OUT {
  vec2 uv;
  vec2 cell;
  flat uint fg;
  flat uint bg;
  float alpha;
} fs_in;

layout(set = 0, binding = 1) uniform sampler2D tex;
layout(location = 0) out vec4 color;

vec3 unpack_rgb(uint packed) {
  float r = float((packed >> 24u) & 0xffu) / 255.0;
  float g = float((packed >> 16u) & 0xffu) / 255.0;
  float b = float((packed >> 8u) & 0xffu) / 255.0;
  return vec3(r, g, b);
}

void main() {
  uint attr = fs_in.fg & 0xffu;
  vec3 fg = unpack_rgb(fs_in.fg);
  vec3 bg = unpack_rgb(fs_in.bg);
  float cover;

  if ((attr & 32u) != 0u) {
    vec3 tmp = fg;
    fg = bg;
    bg = tmp;
  }
  if ((attr & 64u) != 0u)
    fg = bg;
  if ((attr & 1u) != 0u)
    fg = min(fg + vec3(0.12), vec3(1.0));
  if ((attr & 2u) != 0u)
    fg *= 0.6;

  cover = texture(tex, fs_in.uv).r;
  if ((attr & 8u) != 0u && fs_in.cell.y > 0.86)
    cover = 1.0;
  if ((attr & 128u) != 0u && abs(fs_in.cell.y - 0.5) < 0.05)
    cover = 1.0;
  color = vec4(mix(bg, fg, cover), mix(fs_in.alpha, 1.0, cover));
}
