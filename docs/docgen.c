#include "../godstack/Cool/cool.h"
#include "../godstack/Cool/cool.c"
#include "view.cool.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *docgen_file_read(const char *path, size_t *out_len);
static void docgen_md(char *path);

char *
docgen_file_read(const char *path, size_t *out_len)
{
	FILE *f;
	char *buf;
	long sz;
	size_t n;

	f = fopen(path, "rb");
	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	sz = ftell(f);
	if (sz < 0) {
		fclose(f);
		return NULL;
	}
	rewind(f);
	buf = malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	n = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	buf[n] = 0;
	if (out_len)
		*out_len = n;
	return buf;
}

void
docgen_md(char *path)
{
	size_t n;
	char *s;

	s = docgen_file_read(path, &n);
	if (!s) {
		fprintf(stderr, "docgen: %s\n", path);
		return;
	}
	cool_md(s, n);
	free(s);
}

int
main(void)
{
	char path[4096];
	char *nl;

	if (!fgets(path, sizeof path, stdin))
		return 1;
	nl = strchr(path, '\n');
	if (nl)
		*nl = 0;
	if (!path[0])
		return 1;
	page(path, docgen_md, path);
	return 0;
}
