#version 430 core

layout(std140, binding = 0) uniform ConstBuffer {
  uvec2 CellSize;
  uvec2 TermSize;
  uvec2 TopLeftMargin;
  uint  BlinkModulate;
  uint  MarginColor;

  uint StrikeMin;
  uint StrikeMax;
  uint UnderlineMin;
  uint UnderlineMax;
};

struct TerminalCell {
  uint GlyphIndex;
  uint Foreground;
  uint Background;
};

layout(std430, binding = 1) buffer CellBuffer {
  TerminalCell Cells[];
};


layout(binding = 2) uniform sampler2D GlyphTexture;           // Font atlas as normalized texture
layout(binding = 3, rgba32f) writeonly uniform image2D Output; // Render target

// ---- Color Unpacking ----nvec3 UnpackColor(uint packed) {
float r = float(packed & 0xffu) / 255.0;
float g = float((packed >> 8u) & 0xffu) / 255.0;
float b = float((packed >> 16u) & 0xffu) / 255.0;
return vec3(r, g, b);
}

// ---- Glyph Position Unpacking ----nuvec2 UnpackGlyphXY(uint glyphIndex) {
uint x = glyphIndex & 0xffffu;
uint y = glyphIndex >> 16u;
return uvec2(x, y);
}

// ---- Compute Terminal Logic ----nvec4 ComputeOutputColor(uvec2 ScreenPos) {
// Derive cell and intra-cell coordinates
bvec2 inMargin = bvec2(ScreenPos.x >= TopLeftMargin.x,
    ScreenPos.y >= TopLeftMargin.y);
uvec2 localPos = ScreenPos - TopLeftMargin;
uvec2 CellIndex = localPos / CellSize;
uvec2 CellPos   = localPos % CellSize;

vec3 Result;
if (inMargin.x && inMargin.y &&
    CellIndex.x < TermSize.x && CellIndex.y < TermSize.y) {
  // Fetch cell
  uint idx = CellIndex.y * TermSize.x + CellIndex.x;
  TerminalCell cell = Cells[idx];

  // Glyph texcoords in pixels
  uvec2 glyphXY = UnpackGlyphXY(cell.GlyphIndex) * CellSize;
  ivec2 texelPos = ivec2(glyphXY + CellPos);

  // Sample atlas directly with texelFetch
  vec4 glyphTexel = texelFetch(GlyphTexture, texelPos, 0);

  // Unpack colors
  vec3 bg = UnpackColor(cell.Background);
  vec3 fg = UnpackColor(cell.Foreground);
  vec3 blink = UnpackColor(BlinkModulate);

  // Blink and dim flags (bitfields)
  if (((cell.Foreground >> 28u) & 1u) != 0u) fg *= blink;
  if (((cell.Foreground >> 25u) & 1u) != 0u) fg *= 0.5;

  // Alpha blending
  Result = mix(bg, fg, glyphTexel.a);

  // Underline
  if (((cell.Foreground >> 27u) & 1u) != 0u &&
      CellPos.y >= UnderlineMin && CellPos.y < UnderlineMax) {
    Result = fg;
  }
  // Strikethrough
  if (((cell.Foreground >> 31u) & 1u) != 0u &&
      CellPos.y >= StrikeMin && CellPos.y < StrikeMax) {
    Result = fg;
  }
} else {
  // Margin fill
  Result = UnpackColor(MarginColor);
}

return vec4(Result, 1.0);
}

// ---- Compute Shader Entry Point ----
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main() {
  uvec2 gid = gl_GlobalInvocationID.xy;
  imageStore(Output, ivec2(gid), ComputeOutputColor(gid));
}



RWTexture2D
