struct VSInput {
    [[vk::location(0)]] uint pos;
    [[vk::location(1)]] uint foreground;
    [[vk::location(2)]] uint background;
};

struct VSOutput {
    float4 pos : SV_Position;
    float2 uv;
    float2 screenUV;
    float cellY;
    nointerpolation uint fg;
    nointerpolation uint bg;
};

struct PushConstants {
    float2 ndcScale;
    float2 uvScale;
    float alpha;
};

[[vk::push_constant]]
PushConstants pc;

[[vk::binding(0, 0)]]
Sampler2D atlas;

float3 unpackRgb(uint packed) {
    return float3(
        float((packed >> 24u) & 255u),
        float((packed >> 16u) & 255u),
        float((packed >> 8u) & 255u)) / 255.0;
}

[shader("vertex")]
VSOutput vertMain(VSInput input, uint vid : SV_VertexID) {
    VSOutput o;
    float2 corner = float2(float(vid & 1), float(vid >> 1));

    float wide = ((input.background >> 7u) & 1u) != 0u ? 2.0 : 1.0;
    o.pos = float4((float2(float(input.pos >> 16u), float(input.pos & 0xffffu)) + float2(corner.x * wide, corner.y)) * pc.ndcScale - 1.0, 0.0, 1.0);
    o.uv = (float2(float((input.foreground >> 2u) & 31u) + corner.x * wide * 0.5, float(input.background & 127u) + corner.y) * pc.uvScale);
    o.cellY = corner.y;
    o.fg = input.foreground;
    o.bg = input.background;
    o.screenUV = o.screenUV = o.pos.xy * 0.5 + 0.5;
    return o;
}

[shader("fragment")]
float4 fragMain(VSOutput fs) : SV_Target {
    float4 texel = atlas.Sample(fs.uv);
    uint attr = fs.fg & 3u;
    uint colorGlyph = (fs.fg >> 7u) & 1u;
    float cover = colorGlyph != 0u ? texel.a : texel.r;
    float3 rgb = colorGlyph != 0u ? texel.rgb : unpackRgb(fs.fg);

    if ((attr & 1u) != 0u && fs.cellY > 0.86) {
        cover = 1.0;
        rgb = unpackRgb(fs.fg);
    }
    if ((attr & 2u) != 0u && abs(fs.cellY - 0.5) < 0.05) {
        cover = 1.0;
        rgb = unpackRgb(fs.fg);
    }
    return float4(lerp(unpackRgb(fs.bg), rgb, cover), lerp(pc.alpha, 1.0, cover));
} 
