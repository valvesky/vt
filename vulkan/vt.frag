#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    uvec2 cellSize;
    uvec2 termSize;
    vec2 uvScale;
    float alpha;
    uint defBg;
} pc;

struct Cell {
    uint pos;
    uint fg;
    uint bg;
};

layout(set = 0, binding = 1) readonly buffer CellBuf {
    Cell cells[];
};

layout(set = 0, binding = 0) uniform sampler2D atlas;

vec3 unpackRgb(uint packed) {
    return vec3(
        float((packed >> 24u) & 255u),
        float((packed >> 16u) & 255u),
        float((packed >> 8u) & 255u)) / 255.0;
}

void main() {
    uvec2 sp = uvec2(gl_FragCoord.xy);
    uvec2 cell = sp / pc.cellSize;
    uvec2 frac = sp % pc.cellSize;
    vec3 bg;

    if (cell.x >= pc.termSize.x || cell.y >= pc.termSize.y) {
        outColor = vec4(unpackRgb(pc.defBg), pc.alpha);
        return;
    }

    Cell c = cells[cell.y * pc.termSize.x + cell.x];
    bg = unpackRgb(c.bg != 0u ? c.bg : pc.defBg);
    if (c.fg == 0u && c.bg == 0u) {
        outColor = vec4(bg, pc.alpha);
        return;
    }

    {
        uint col = (c.fg >> 2u) & 31u;
        uint row = c.bg & 127u;
        uint colorGlyph = (c.fg >> 7u) & 1u;
        uint slotHalf = c.pos & 1u;
        vec2 uv = vec2(
            float(col) + (float(slotHalf) + float(frac.x) / float(pc.cellSize.x)) * 0.5,
            float(row) + float(frac.y) / float(pc.cellSize.y)) * pc.uvScale;
        vec4 texel = texture(atlas, uv);
        float cover = colorGlyph != 0u ? texel.a : texel.r;
        vec3 rgb = colorGlyph != 0u ? texel.rgb : unpackRgb(c.fg);
        float cellY = float(frac.y) / float(pc.cellSize.y);
        uint attr = c.fg & 3u;

        if ((attr & 1u) != 0u && cellY > 0.86) {
            cover = 1.0;
            rgb = unpackRgb(c.fg);
        }
        if ((attr & 2u) != 0u && abs(cellY - 0.5) < 0.05) {
            cover = 1.0;
            rgb = unpackRgb(c.fg);
        }
        outColor = vec4(mix(bg, rgb, cover), mix(pc.alpha, 1.0, cover));
    }
}
