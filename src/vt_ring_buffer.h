#pragma once

/* NOTE(vasco):
 *
 * The ring is a tail window. New bytes overwrite the oldest when full.
 * A 1GB dump becomes ~size bytes, then last N logical lines.
 *
 * 1) Line prepass: split on NL. Offscreen lines are not parsed.
 * 2) Run prepass: those lines into printable / escape / utf-8 / kitty.
 *    Wrap without NL is later.
 */

/* NOTE(vasco):
 * Indexes into the buffer are 64 bit so we can easily check if a line
 * has been scrolled off!
 * (buffer_idx < buffer->read) -> ignore
 */

enum {
	VT_LINE_MAX = 256,
	VT_RUN_MAX = 65536,
};

typedef u64 vt_buffer_idx;

typedef enum {
	VT_RUN_PRINTABLE,
	VT_RUN_ESCAPE,
	VT_RUN_UTF8,
	VT_RUN_KITTY,
} VtRunType;

typedef struct VtRun {
	vt_buffer_idx off;
	vt_buffer_idx n;
	VtRunType type;
} VtRun;

typedef struct VtLine {
	vt_buffer_idx off;
	vt_buffer_idx n;
	VtRun *runs;
	u32 run_n;
} VtLine;

/* NOTE(vasco):
 * Our ring buffer struct contains the actual ringbuffer
 * and all the information we gathered via preparsing.
 *
 * This will allow us to parse it incrementally if the user
 * needs scrollback.
 */
typedef struct VtRingBuffer {
	char *base;
	size_t size; /* multiple of peak_page_size() */
	size_t read;
	size_t write;
	size_t parsed;
	VtLine *line;
	VtRun *run;
	u32 line_max;
	u32 line_n;
	u32 line_i;
	u32 run_max;
	u32 run_n;
} VtRingBuffer;

typedef struct {
	u32 line_max;
	u32 run_max;
	size_t pages;
} VtRingBufferArgs;

extern const char *const vt_ring_buffer_run_name[];

static size_t vt_ringbuffer_size(VtRingBufferArgs args);
static VtRingBuffer *vt_ringbuffer_create(VtRingBufferArgs args, void *memory);
static void vt_ringbuffer_destroy(VtRingBuffer *b);

static void vt_ringbuffer_produce(VtRingBuffer *b, size_t n);
static void vt_ringbuffer_consume(VtRingBuffer *b); // will check
static VtRun *vt_ringbuffer_runs_from_last_n_lines(VtRingBuffer *b, u32 n_lines);
