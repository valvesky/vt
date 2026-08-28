/* Host vert/frag for Rend CPU. Same math as vulkan/vt.slang.
 * slangc -target c does not emit vertex/fragment on current slang. */

void
vt_cpu_vert(RendCpuVarying *out, const RendCpuVertArgs *in)
{
  const uint32_t *a;
  const VtPush *pc;
  uint32_t pos;
  uint32_t fg;
  uint32_t bg;
  uint32_t vid;
  float cx;
  float cy;
  float wide;

  a = in->attributes;
  pos = a[0];
  fg = a[4];
  bg = a[8];
  vid = in->vertex_id;
  pc = in->push;
  cx = (float)(vid & 1u);
  cy = (float)(vid >> 1u);
  wide = ((bg >> 7u) & 1u) != 0u ? 2.f : 1.f;
  out->position[0] = ((float)(pos >> 16u) + cx * wide) * pc->ndc_x - 1.f;
  out->position[1] = ((float)(pos & 0xffffu) + cy) * pc->ndc_y - 1.f;
  out->position[2] = 0.f;
  out->position[3] = 1.f;
  out->data[0] = ((float)((fg >> 2u) & 31u) + cx * wide * 0.5f) * pc->uv_x;
  out->data[1] = ((float)(bg & 127u) + cy) * pc->uv_y;
  out->data[2] = cy;
  out->flat[0] = fg;
  out->flat[1] = bg;
}

void
vt_cpu_frag(float rgba[4], const RendCpuFragArgs *in)
{
  const VtPush *pc;
  float cover;
  float cell_y;
  uint32_t fg;
  uint32_t bg;
  uint32_t attr;
  float fr, fg8, fb, br, bg8, bb;
  float dy;

  pc = in->push;
  cover = in->sample(0, in->v->data[0], in->v->data[1], in->sample_ctx);
  cell_y = in->v->data[2];
  fg = in->v->flat[0];
  bg = in->v->flat[1];
  attr = fg & 3u;
  if ((attr & 1u) != 0u && cell_y > 0.86f)
    cover = 1.f;
  dy = cell_y - 0.5f;
  if (dy < 0.f)
    dy = -dy;
  if ((attr & 2u) != 0u && dy < 0.05f)
    cover = 1.f;
  fr = (float)((fg >> 24u) & 255u) / 255.f;
  fg8 = (float)((fg >> 16u) & 255u) / 255.f;
  fb = (float)((fg >> 8u) & 255u) / 255.f;
  br = (float)((bg >> 24u) & 255u) / 255.f;
  bg8 = (float)((bg >> 16u) & 255u) / 255.f;
  bb = (float)((bg >> 8u) & 255u) / 255.f;
  rgba[0] = br + (fr - br) * cover;
  rgba[1] = bg8 + (fg8 - bg8) * cover;
  rgba[2] = bb + (fb - bb) * cover;
  rgba[3] = pc->alpha + (1.f - pc->alpha) * cover;
}
