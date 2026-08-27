struct VSInput {
    [[vk::location(0)]] uint pos;
    [[vk::location(1)]] uint foreground;
    [[vk::location(2)]] uint background;
};

struct VSOutput {
    float4 pos : SV_Position;
    float2 uv;
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

    o.pos = float4((float2(float(input.pos >> 16u), float(input.pos & 0xffffu)) + corner) * pc.ndcScale - 1.0, 0.0, 1.0);
    o.uv = (float2(float((input.foreground >> 2u) & 63u), float(input.background & 255u)) + corner) * pc.uvScale;
    o.cellY = corner.y;
    o.fg = input.foreground;
    o.bg = input.background;
    return o;
}

[shader("fragment")]
float4 fragMain(VSOutput fs) : SV_Target {
    float cover = atlas.Sample(fs.uv).r;
    uint attr = fs.fg & 3u;

    if ((attr & 1u) != 0u && fs.cellY > 0.86)
        cover = 1.0;
    if ((attr & 2u) != 0u && abs(fs.cellY - 0.5) < 0.05)
        cover = 1.0;
    return float4(lerp(unpackRgb(fs.bg), unpackRgb(fs.fg), cover), lerp(pc.alpha, 1.0, cover));
}
