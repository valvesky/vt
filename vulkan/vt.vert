#version 450

layout(location = 0) in uint inPos;
layout(location = 1) in uint inFg;
layout(location = 2) in uint inBg;

layout(location = 0) out vec2 vUV;
layout(location = 1) out float vCellY;
layout(location = 2) flat out uint vFg;
layout(location = 3) flat out uint vBg;

layout(push_constant) uniform PushConstants {
    vec2 ndcScale;
    vec2 uvScale;
    float alpha;
} pc;

void main() {
    vec2 corner = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));
    float wide = ((inBg >> 7u) & 1u) != 0u ? 2.0 : 1.0;
    gl_Position = vec4(
        (vec2(float(inPos >> 16u), float(inPos & 0xffffu)) + vec2(corner.x * wide, corner.y))
            * pc.ndcScale - 1.0,
        0.0, 1.0);
    vUV = vec2(float((inFg >> 2u) & 31u) + corner.x * wide * 0.5,
               float(inBg & 127u) + corner.y) * pc.uvScale;
    vCellY = corner.y;
    vFg = inFg;
    vBg = inBg;
}
