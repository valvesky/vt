#pragma  once

int
osc52(const char *p, u32 n)
{
	u32 i;
	u32 pd;
	u32 pdn;
	size_t dn;

	i = 0;
	while (i + 5 < n
			&& !((unsigned char)p[i] == 0x1b && p[i + 1] == ']'
				&& p[i + 2] == '5' && p[i + 3] == '2' && p[i + 4] == ';'))
		i++;
	if (i + 5 >= n)
		return 0;
	p += i;
	n -= i;
	i = 2;
	if (i + 3 > n || p[i] != '5' || p[i + 1] != '2' || p[i + 2] != ';')
		return 0;
	i += 3;
	while (i < n && p[i] != ';')
		i++;
	if (i >= n || p[i] != ';')
		return 0;
	i++;
	pd = i;
	while (i < n && (unsigned char)p[i] != 0x07
			&& !((unsigned char)p[i] == 0x1b && i + 1 < n && p[i + 1] == '\\'))
		i++;
	if (i >= n)
		return 0;
	pdn = i - pd;
	if (pdn == 0 || (pdn == 1 && p[pd] == '?'))
		return 1;
	dn = base64_decode(p + pd, pdn, clip_buf, CLIP_MAX);
	peak_clip_set(renderer ? &renderer->win : NULL, PEAK_CLIP_CLIPBOARD, clip_buf, dn);
	return 1;
}

