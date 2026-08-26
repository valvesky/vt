struct TermCell {
    uint fg;
    uint bg;
    uint codepoint;
    uint pad;
};

struct PushConstants {
    TermCell *cells;
    uint *glyphCp;
    uint *glyphSlot;
    uint8_t *atlas;
    uint cols;
    uint rows;
    uint cellW;
    uint cellH;
    uint atlasW;
    uint atlasH;
    uint cursorX;
    uint cursorY;
    uint cursorOn;
    uint clearBg;
    float alpha;
    uint pad;
    uint *dest;
    uint fbW;
    uint fbH;
};

[[vk::push_constant]]
PushConstants pc;

uint glyphLookup(uint cp) {
    uint h;
    uint i;

    if (cp == 0)
        cp = 32;
    h = (cp * 2654435761u) & 2047u;
    for (i = 0; i < 2048u; i++) {
        uint s = (h + i) & 2047u;
        if (pc.glyphCp[s] == cp)
            return pc.glyphSlot[s];
        if (pc.glyphCp[s] == 0)
            break;
    }
    return 0;
}

uint glyphSlotFor(uint cp) {
    uint slot;

    slot = glyphLookup(cp);
    if (slot == 0 && cp != 0 && cp != 32u)
        slot = glyphLookup(0xFFFDu);
    return slot;
}

uint atlasCover(uint ax, uint ay) {
    uint i;

    if (ax >= pc.atlasW || ay >= pc.atlasH)
        return 0;
    i = (ay * pc.atlasW + ax) * 4u;
    return pc.atlas[i];
}

float3 unpackRgb(uint packed) {
    return float3(
        float((packed >> 24u) & 255u),
        float((packed >> 16u) & 255u),
        float((packed >> 8u) & 255u)) / 255.0;
}

uint packRgba(float3 rgb, float a) {
    uint r = (uint)(rgb.x * 255.0 + 0.5);
    uint g = (uint)(rgb.y * 255.0 + 0.5);
    uint b = (uint)(rgb.z * 255.0 + 0.5);
    uint aa = (uint)(a * 255.0 + 0.5);

    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    if (aa > 255u) aa = 255u;
    return r | (g << 8u) | (b << 16u) | (aa << 24u);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void cellMain(uint3 id : SV_DispatchThreadID) {
    uint cx = id.x;
    uint cy = id.y;
    uint px;
    uint py;
    uint idx;
    TermCell cell;
    uint cp;
    uint attr;
    float3 fg;
    float3 bg;
    uint slot;
    uint col;
    uint row;
    uint cover;
    float t;
    float fy;
    float a;

    if (pc.cellW == 0 || pc.cellH == 0 || pc.cols == 0 || pc.rows == 0)
        return;
    if (cx >= pc.cols || cy >= pc.rows)
        return;
    if (!pc.dest)
        return;

    idx = cy * pc.cols + cx;
    cell = pc.cells[idx];
    cp = cell.codepoint;
    if (cp == 0 && cell.fg == 0 && cell.bg == 0) {
        fg = unpackRgb(pc.clearBg);
        bg = fg;
        cp = 32;
    } else {
        if (cp == 0)
            cp = 32;
        fg = unpackRgb(cell.fg);
        bg = unpackRgb(cell.bg);
    }

    if (pc.cursorOn != 0 && cx == pc.cursorX && cy == pc.cursorY) {
        float3 tmp = fg;
        fg = bg;
        bg = tmp;
    }

    attr = cell.fg & 255u;
    if ((attr & 32u) != 0u) {
        float3 tmp = fg;
        fg = bg;
        bg = tmp;
    }
    if ((attr & 64u) != 0u)
        fg = bg;
    if ((attr & 1u) != 0u)
        fg = min(fg + float3(0.12, 0.12, 0.12), float3(1.0, 1.0, 1.0));
    if ((attr & 2u) != 0u)
        fg *= 0.6;

    slot = glyphSlotFor(cp);
    col = slot >> 16u;
    row = slot & 65535u;

    for (py = 0; py < pc.cellH; py++) {
        fy = pc.cellH > 1u ? float(py) / float(pc.cellH - 1u) : 0.0;
        for (px = 0; px < pc.cellW; px++) {
            uint x = cx * pc.cellW + px;
            uint y = cy * pc.cellH + py;
            uint outi;

            if (x >= pc.fbW || y >= pc.fbH)
                continue;
            cover = atlasCover(col * pc.cellW + px, row * pc.cellH + py);
            if ((attr & 8u) != 0u && fy > 0.86)
                cover = 255u;
            if ((attr & 128u) != 0u && abs(fy - 0.5) < 0.05)
                cover = 255u;
            t = float(cover) / 255.0;
            a = lerp(pc.alpha, 1.0, t);
            outi = y * pc.fbW + x;
            pc.dest[outi] = packRgba(lerp(bg, fg, t), a);
        }
    }
}
