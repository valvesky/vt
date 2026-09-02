#define VT_HEADLESS
#include "vt.c"

int
main(int argc, char **argv)
{
	const char *path;
	const char *shot;
	FILE *in;
	TermScreen *scr;
	char *end;
	u32 cols;
	u32 rows;
	int dump_runs;
	int rc;
	int i;
	unsigned long v;

	path = NULL;
	shot = NULL;
	cols = 80;
	rows = 24;
	dump_runs = 0;
	rc = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--screenshot") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "vt-headless: --screenshot needs a path\n");
				return 1;
			}
			shot = argv[++i];
		} else if (strcmp(argv[i], "--dump-runs") == 0) {
			dump_runs = 1;
		} else if (strcmp(argv[i], "--cols") == 0
				|| strcmp(argv[i], "--rows") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "vt-headless: %s needs a number\n", argv[i]);
				return 1;
			}
			v = strtoul(argv[i + 1], &end, 10);
			if (!argv[i + 1][0] || *end || v < 2 || v > 400) {
				fprintf(stderr, "vt-headless: bad %s\n", argv[i]);
				return 1;
			}
			if (argv[i][2] == 'c')
				cols = (u32)v;
			else
				rows = (u32)v;
			i++;
		} else if (!path) {
			path = argv[i];
		} else {
			fprintf(stderr, "vt-headless: extra arg %s\n", argv[i]);
			return 1;
		}
	}

	if (!vt_init_term(cols, rows))
		return 1;

	in = path ? fopen(path, "rb") : stdin;
	if (!in) {
		fprintf(stderr, "vt-headless: cannot open %s\n", path);
		vt_destroy();
		return 1;
	}

	vt_in = in;
	peak_stdout_silence();
	if (dump_runs)
		(void)vt_feed_stdin_to_ringbuffer();
	else
		while (vt_ingest())
			;
	vt_in = NULL;
	if (path)
		fclose(in);
	peak_stdout_restore();
	if (dump_runs) {
		vt_feed_ringbuffer_to_runs();
		vt_dump_runs(stdout);
	} else {
		vt_feed_ring_drain();
		vt_dump_screen(stdout);
	}

	if (shot) {
		if (!glyph_table_init(font_path, (float)font_size_px)) {
			fprintf(stderr, "vt-headless: cannot load font %s\n", font_path);
			rc = 1;
		} else {
			TermStyle cs;

			scr = term_screen(vt_term_p);
			cs = term_cursor_style(vt_term_p);
			if (!renderer_screenshot_ppm(vt_term_p, scr, vt_term.cursor.x, vt_term.cursor.y,
					cs.fg, cs.bg, shot)) {
				fprintf(stderr, "vt-headless: cannot write %s\n", shot);
				rc = 1;
			}
			glyph_table_destroy();
		}
	}

	vt_destroy();
	return rc;
}
