#pragma once

#include <immintrin.h>
#include <string.h>

static u32 vt_ring_buffer_internal_utf8_atom(const char *data, u32 n, codepoint_t *cp);
static u32 vt_ring_buffer_internal_esc_atom(const char *data, u32 n, int *kitty);
static void vt_ring_buffer_internal_line_push(VtRingBuffer *b, vt_buffer_idx off, vt_buffer_idx n);

const char *const vt_ring_buffer_run_name[] = {
	"PRINTABLE",
	"ESCAPE",
	"UTF8",
	"KITTY",
};

void
vt_ringbuffer_destroy(VtRingBuffer *b)
{
	if (b && b->base)
		peak_mirror_unmap(b->base, b->size);
}

size_t
vt_ringbuffer_size(VtRingBufferArgs args)
{
	return sizeof (VtRingBuffer)
		+ (size_t)args.line_max * sizeof (VtLine)
		+ (size_t)args.run_max * sizeof (VtRun);
}

VtRingBuffer *
vt_ringbuffer_create(VtRingBufferArgs args, void *memory)
{
	VtRingBuffer *b;
	size_t size;

	if (!memory || !args.pages)
		return NULL;
	memset(memory, 0, vt_ringbuffer_size(args));
	b = (VtRingBuffer *)memory;
	b->line_max = args.line_max;
	b->run_max = args.run_max;
	b->line = (VtLine *)(b + 1);
	b->run = (VtRun *)(b->line + args.line_max);
	size = args.pages * peak_page_size();
	b->base = peak_mirror_map(size);
	if (!b->base)
		return NULL;
	b->size = size;
	return b;
}

u32
vt_ring_buffer_internal_utf8_atom(const char *data, u32 n, codepoint_t *cp)
{
	unsigned char ch;
	u32 need;
	u32 i;
	codepoint_t u;

	if (!n)
		return 0;
	ch = (unsigned char)data[0];
	if (ch < 0x80)
		return 0;
	if ((ch & 0xE0) == 0xC0)
		need = 2;
	else if ((ch & 0xF0) == 0xE0)
		need = 3;
	else if ((ch & 0xF8) == 0xF0)
		need = 4;
	else {
		if (cp)
			*cp = UTF_INVALID;
		return 1;
	}
	if (n < need) {
		for (i = 1; i < n; i++) {
			if (((unsigned char)data[i] & 0xC0) != 0x80) {
				if (cp)
					*cp = UTF_INVALID;
				return 1;
			}
		}
		return 0;
	}
	for (i = 1; i < need; i++) {
		if (((unsigned char)data[i] & 0xC0) != 0x80) {
			if (cp)
				*cp = UTF_INVALID;
			return 1;
		}
	}
	if (need == 2)
		u = (codepoint_t)(((ch & 0x1F) << 6) | ((unsigned char)data[1] & 0x3F));
	else if (need == 3)
		u = (codepoint_t)(((ch & 0x0F) << 12) | (((unsigned char)data[1] & 0x3F) << 6) | ((unsigned char)data[2] & 0x3F));
	else
		u = (codepoint_t)(((ch & 0x07) << 18) | (((unsigned char)data[1] & 0x3F) << 12) | (((unsigned char)data[2] & 0x3F) << 6) | ((unsigned char)data[3] & 0x3F));
	if (cp)
		*cp = u;
	return need;
}

u32
vt_ring_buffer_internal_esc_atom(const char *data, u32 n, int *kitty)
{
	unsigned char ch;
	unsigned char nch;
	u32 ei;

	if (kitty)
		*kitty = 0;
	if (!n)
		return 0;
	ch = (unsigned char)data[0];
	if (ch != 0x1B) {
		if (ch >= 0x20)
			return 0;
		return 1;
	}
	if (n < 2)
		return 0;
	nch = (unsigned char)data[1];
	if (nch == '[') {
		for (ei = 2; ei < n; ei++) {
			unsigned char bc;

			bc = (unsigned char)data[ei];
			if (bc >= 0x40 && bc <= 0x7E)
				return ei + 1;
		}
		return 0;
	}
	if (nch == ']' || nch == 'P' || nch == '_' || nch == '^' || nch == 'k') {
		u32 k;

		k = 0;
		for (ei = 2; ei < n; ei++) {
			unsigned char bc;

			bc = (unsigned char)data[ei];
			if (bc == 0x07) {
				k = ei + 1;
				break;
			}
			if (bc == 0x1B && ei + 1 < n && data[ei + 1] == '\\') {
				k = ei + 2;
				break;
			}
		}
		if (!k)
			return 0;
		if (nch == '_' && n >= 3 && data[2] == 'G' && kitty)
			*kitty = 1;
		return k;
	}
	if (nch >= 0x20 && nch <= 0x2F)
		return n >= 3 ? 3 : 0;
	return 2;
}

void
vt_ring_buffer_internal_line_push(VtRingBuffer *b, vt_buffer_idx off, vt_buffer_idx n)
{
	VtLine *line;
	u32 slot;

	if (!b->line_max || !n)
		return;
	if (b->line_n) {
		line = &b->line[(b->line_i + b->line_n - 1) % b->line_max];
		if (line->off + line->n == off && line->n) {
			char last;

			last = b->base[(line->off % b->size) + (size_t)line->n - 1];
			if (last != '\n') {
				line->n += n;
				return;
			}
		}
	}
	if (b->line_n == b->line_max) {
		b->line_i++;
		if (b->line_i == b->line_max)
			b->line_i = 0;
		b->line_n--;
	}
	slot = (b->line_i + b->line_n) % b->line_max;
	line = &b->line[slot];
	line->off = off;
	line->n = n;
	line->runs = NULL;
	line->run_n = 0;
	b->line_n++;
}

void
vt_ringbuffer_produce(VtRingBuffer *b, size_t n)
{
	if (!b || !n)
		return;
	if (b->write - b->read + n > b->size)
		b->read = b->write + n - b->size;
	if (b->parsed < b->read)
		b->parsed = b->read;
	b->write += n;
}

void
vt_ringbuffer_consume(VtRingBuffer *b)
{
	const char *head;
	vt_buffer_idx base;
	u32 remaining;
	u32 off;
#ifdef __AVX2__
	__m256i v_esc;
	__m256i v_nl;
	__m256i v_space;
	__m256i v_del;
#endif

	if (!b || !b->base || !b->size || !b->run_max)
		return;
	while (b->line_n) {
		VtLine *line;

		line = &b->line[b->line_i];
		if (line->off + line->n > (vt_buffer_idx)b->read)
			break;
		b->line_i++;
		if (b->line_i == b->line_max)
			b->line_i = 0;
		b->line_n--;
	}
	if (b->parsed >= b->write || b->run_n >= b->run_max)
		return;
	VTTRACE("Consuming: %lu bytes", (unsigned long)(b->write - b->read));
	base = (vt_buffer_idx)b->parsed;
	head = b->base + (base % b->size);
	remaining = (u32)(b->write - b->parsed);
	off = 0;
#ifdef __AVX2__
	v_esc = _mm256_set1_epi8(0x1B);
	v_nl = _mm256_set1_epi8('\n');
	v_space = _mm256_set1_epi8(0x20);
	v_del = _mm256_set1_epi8(0x7F);
#endif
	while (off < remaining && b->run_n < b->run_max) {
		unsigned char ch;
		u32 left;
		u32 m;
		u32 i;
		u32 seg;
		VtRunType type;

		left = remaining - off;
		ch = (unsigned char)head[off];
		m = 0;
		if (ch >= 0x20 && ch < 0x7F) {
			type = VT_RUN_PRINTABLE;
#ifdef __AVX2__
			while (left - m >= 32) {
				__m256i batch;
				__m256i hit;
				int mask;

				batch = _mm256_loadu_si256((const __m256i *)(head + off + m));
				hit = _mm256_or_si256(_mm256_cmpeq_epi8(batch, v_esc), _mm256_cmpeq_epi8(batch, v_nl));
				hit = _mm256_or_si256(hit, _mm256_or_si256(_mm256_cmpgt_epi8(v_space, batch), _mm256_cmpeq_epi8(batch, v_del)));
				mask = _mm256_movemask_epi8(hit);
				if (mask) {
					m += (u32)__tzcnt_u32((unsigned int)mask);
					break;
				}
				m += 32;
			}
#endif
			while (m < left) {
				ch = (unsigned char)head[off + m];
				if (ch < 0x20 || ch >= 0x7F)
					break;
				m++;
			}
		} else if (ch >= 0x80) {
			type = VT_RUN_UTF8;
			while (m < left) {
				u32 k;

				ch = (unsigned char)head[off + m];
				if (ch < 0x80)
					break;
				k = vt_ring_buffer_internal_utf8_atom(head + off + m, left - m, NULL);
				if (!k)
					break;
				m += k;
			}
		} else {
			int kitty;

			kitty = (ch == 0x1B && left >= 3
				&& head[off + 1] == '_' && head[off + 2] == 'G');
			type = kitty ? VT_RUN_KITTY : VT_RUN_ESCAPE;
			while (m < left) {
				u32 k;
				int atom_kitty;

				ch = (unsigned char)head[off + m];
				if ((ch >= 0x20 && ch < 0x7F) || ch >= 0x80)
					break;
				k = vt_ring_buffer_internal_esc_atom(head + off + m, left - m, &atom_kitty);
				if (!k)
					break;
				if (atom_kitty != kitty)
					break;
				m += k;
			}
		}
		if (!m)
			break;
		b->run[b->run_n].off = base + off;
		b->run[b->run_n].n = m;
		b->run[b->run_n].type = type;
		b->run_n++;
		seg = 0;
		for (i = 0; i < m; i++) {
			if (head[off + i] != '\n')
				continue;
			vt_ring_buffer_internal_line_push(b, base + off + seg, i - seg + 1);
			seg = i + 1;
		}
		if (seg < m)
			vt_ring_buffer_internal_line_push(b, base + off + seg, m - seg);
		off += m;
	}
	b->parsed = (size_t)base + off;
}

VtRun *
vt_ringbuffer_runs_from_last_n_lines(VtRingBuffer *b, u32 n_lines)
{
	/* NOTE(vasco):
	 *
	 * Input can contain escape sequences and/or UTF-8.
	 *
	 * Term accepts 4 types of input:
	 * 1. printable (> SPC & < DEL)
	 * 2. escape (includes printable) (< DEL)
	 * 3. utf8 (> SPC)
	 * 4. ALL
	 *
	 * We can avoid type 4, because all text fits
	 * into type 2. and 3. necessarily.
	 *
	 * The optimal preparser maximizes use of type 1.
	 *
	 * Let's say that we recieve:
	 * <90 bytes of ANSI SEQUENCE>
	 * <1 kilobyte plain text>
	 * <8 bytes utf-8>
	 * <1 kilobyte plain text>
	 *
	 * We want that reflected into exactly that:
	 * RUN 1: OFFSET=HEAD LEN=90 TYPE=ESCAPE
	 * RUN 2: OFFSET=HEAD+90 LEN=1024 TYPE=PRINTABLE
	 * RUN 3: OFFSET=HEAD+90+1024 LEN=8 TYPE=UTF8
	 * RUN 4: OFFSET=HEAD+90+1024+8 LEN=1024 TYPE=PRINTABLE
	 *
	 * This should give us optimal parsing speeds.
	 */

    VTTRACE("Requesting %u lines", n_lines);

	u32 li;
	u32 i0;
#ifdef __AVX2__
	__m256i space;
	__m256i del;
#endif

	if (!b)
		return NULL;
	b->run_n = 0;
	if (!b->base || !b->size || !b->line_n || !b->run_max)
		return b->run;
	if (!n_lines || n_lines > b->line_n)
		n_lines = b->line_n;
	i0 = b->line_n - n_lines;
#ifdef __AVX2__
	space = _mm256_set1_epi8(0x20);
	del = _mm256_set1_epi8(0x7F);
#endif
	for (li = i0; li < b->line_n && b->run_n < b->run_max; li++) {
		VtLine *line;
		const char *head;
		vt_buffer_idx base;
		u32 n;
		u32 off;
		u32 first;

		line = &b->line[(b->line_i + li) % b->line_max];
		if (line->off < (vt_buffer_idx)b->read)
			continue;
		n = (u32)line->n;
		head = b->base + (line->off % b->size);
		base = line->off;
		first = b->run_n;
		off = 0;
		while (off < n && b->run_n < b->run_max) {
			unsigned char ch;
			u32 remaining;
			u32 m;
			VtRunType type;

			remaining = n - off;
			ch = (unsigned char)head[off];
			m = 0;
			if (ch >= 0x20 && ch < 0x7F) {
				type = VT_RUN_PRINTABLE;
#ifdef __AVX2__
				while (remaining - m >= 32) {
					__m256i batch;
					__m256i bad;
					int mask;

					batch = _mm256_loadu_si256((const __m256i *)(head + off + m));
					bad = _mm256_or_si256(_mm256_cmpgt_epi8(space, batch), _mm256_cmpeq_epi8(batch, del));
					mask = _mm256_movemask_epi8(bad);
					if (mask) {
						m += (u32)__tzcnt_u32((unsigned int)mask);
						break;
					}
					m += 32;
				}
#endif
				while (m < remaining) {
					ch = (unsigned char)head[off + m];
					if (ch < 0x20 || ch >= 0x7F)
						break;
					m++;
				}
			} else if (ch >= 0x80) {
				type = VT_RUN_UTF8;
				while (m < remaining) {
					u32 k;

					ch = (unsigned char)head[off + m];
					if (ch < 0x80)
						break;
					k = vt_ring_buffer_internal_utf8_atom(head + off + m, remaining - m, NULL);
					if (!k)
						break;
					m += k;
				}
			} else {
				int kitty;

				kitty = (ch == 0x1B && remaining >= 3
					&& head[off + 1] == '_' && head[off + 2] == 'G');
				type = kitty ? VT_RUN_KITTY : VT_RUN_ESCAPE;
				while (m < remaining) {
					u32 k;
					u32 left;
					int atom_kitty;

					ch = (unsigned char)head[off + m];
					if ((ch >= 0x20 && ch < 0x7F) || ch >= 0x80)
						break;
					left = remaining - m;
					k = 1;
					atom_kitty = 0;
					if (ch == 0x1B) {
						unsigned char nch;
						u32 ei;

						if (left == 1) {
							k = 0;
						} else {
							nch = (unsigned char)head[off + m + 1];
							if (nch == '[') {
								k = 0;
								for (ei = 2; ei < left; ei++) {
									unsigned char bc = (unsigned char)head[off + m + ei];

									if (bc >= 0x40 && bc <= 0x7E) {
										k = ei + 1;
										break;
									}
								}
							} else if (nch == ']' || nch == 'P' || nch == '_' || nch == '^' || nch == 'k') {
								k = 0;
								for (ei = 2; ei < left; ei++) {
									unsigned char bc = (unsigned char)head[off + m + ei];

									if (bc == 0x07) {
										k = ei + 1;
										break;
									}
									if (bc == 0x1B && ei + 1 < left && head[off + m + ei + 1] == '\\') {
										k = ei + 2;
										break;
									}
								}
								if (k && nch == '_' && left >= 3
									&& head[off + m + 2] == 'G')
									atom_kitty = 1;
							} else if (nch >= 0x20 && nch <= 0x2F) {
								k = left >= 3 ? 3 : 0;
							} else {
								k = 2;
							}
						}
					}
					if (!k)
						break;
					if (atom_kitty != kitty)
						break;
					m += k;
				}
			}
			if (!m)
				break;
			b->run[b->run_n].off = base + off;
			b->run[b->run_n].n = m;
			b->run[b->run_n].type = type;
			b->run_n++;
			off += m;
		}
		line->runs = b->run + first;
		line->run_n = b->run_n - first;
	}
	return b->run;
}
