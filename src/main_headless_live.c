#define VT_HEADLESS
#include "vt.c"

int
main(int argc, char **argv)
{
	char *end;
	u32 cols;
	u32 rows;
	int i;
	unsigned long v;

	cols = 80;
	rows = 24;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--cols") == 0
				|| strcmp(argv[i], "--rows") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "vt-live: %s needs a number\n", argv[i]);
				return 1;
			}
			v = strtoul(argv[i + 1], &end, 10);
			if (!argv[i + 1][0] || *end || v < 2 || v > 400) {
				fprintf(stderr, "vt-live: bad %s\n", argv[i]);
				return 1;
			}
			if (argv[i][2] == 'c')
				cols = (u32)v;
			else
				rows = (u32)v;
			i++;
		} else {
			fprintf(stderr, "vt-live: extra arg %s\n", argv[i]);
			return 1;
		}
	}

	if (!glyph_table_init(font_path, (float)font_size_px)) {
		fprintf(stderr, "vt-live: cannot load font %s\n", font_path);
		return 1;
	}
	if (!vt_init(cols, rows)) {
		fprintf(stderr, "vt-live: init failed\n");
		glyph_table_destroy();
		vt_destroy();
		return 1;
	}
	while (running) {
		vt_wait(-1);
		vt_ctl_pump();
		vt_ingest();
	}
	glyph_table_destroy();
	vt_destroy();
	return 0;
}
