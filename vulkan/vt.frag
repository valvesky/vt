#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in float vCellY;
layout(location = 2) flat in uint vFg;
layout(location = 3) flat in uint vBg;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2 ndcScale;
    vec2 uvScale;
    float alpha;
} pc;

layout(set = 0, binding = 0) uniform sampler2D atlas;

vec3 unpackRgb(uint packed) {
    return vec3(
        float((packed >> 24u) & 255u),
        float((packed >> 16u) & 255u),
        float((packed >> 8u) & 255u)) / 255.0;
}

void main() {
    vec4 texel = texture(atlas, vUV);
    uint attr = vFg & 3u;
    uint colorGlyph = (vFg >> 7u) & 1u;
    float cover = colorGlyph != 0u ? texel.a : texel.r;
    vec3 rgb = colorGlyph != 0u ? texel.rgb : unpackRgb(vFg);

    if ((attr & 1u) != 0u && vCellY > 0.86) {
        cover = 1.0;
        rgb = unpackRgb(vFg);
    }
    if ((attr & 2u) != 0u && abs(vCellY - 0.5) < 0.05) {
        cover = 1.0;
        rgb = unpackRgb(vFg);
    }
    outColor = vec4(mix(unpackRgb(vBg), rgb, cover), mix(pc.alpha, 1.0, cover));
}
