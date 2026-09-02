#include "godstack/Poof/poof.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#if defined(__linux__)
#include <sys/utsname.h>
#endif

enum {
	APP_VT = 0,
	APP_HEADLESS,
	APP_LIVE,
	APP_CTL
};

static uint32_t vt_simd;

static const char *glslang_bin(void);
static int file_ok(const char *path);
static int dir_has_json(const char *path);
static int vulkan_ok(void);
static int queue_shaders(Poof_Batch *batch);
static void queue_app(Poof_Batch *batch, int release, int app, int cpu);
static void queue_test(Poof_Batch *batch);
static int queue_docs(void);
static void queue_install_unix(Poof_Batch *batch);
static int pkg_build(void);

static const char *
glslang_bin(void)
{
	if (poof_has_cmd("glslangValidator"))
		return "glslangValidator";
	if (poof_has_cmd("glslang"))
		return "glslang";
	return NULL;
}

static int
file_ok(const char *path)
{
	return access(path, R_OK) == 0;
}

static int
dir_has_json(const char *path)
{
	DIR *d;
	struct dirent *e;
	size_t n;

	d = opendir(path);
	if (!d)
		return 0;
	while ((e = readdir(d))) {
		n = strlen(e->d_name);
		if (n >= 5 && strcmp(e->d_name + n - 5, ".json") == 0) {
			closedir(d);
			return 1;
		}
	}
	closedir(d);
	return 0;
}

static int
vulkan_ok(void)
{
	const char *env;
	const char *home;
	char buf[512];

	if (!file_ok("vulkan/vt.vert.spv") && !glslang_bin())
		return 0;
	env = getenv("VK_DRIVER_FILES");
	if (env && env[0])
		return 1;
	env = getenv("VK_ICD_FILENAMES");
	if (env && env[0])
		return 1;
	if (dir_has_json("/usr/share/vulkan/icd.d")
	 || dir_has_json("/etc/vulkan/icd.d")
	 || dir_has_json("/usr/local/share/vulkan/icd.d")
	 || dir_has_json("/opt/homebrew/share/vulkan/icd.d"))
		return 1;
	home = getenv("XDG_DATA_HOME");
	if (home && home[0]) {
		snprintf(buf, sizeof buf, "%s/vulkan/icd.d", home);
		if (dir_has_json(buf))
			return 1;
	}
	home = getenv("HOME");
	if (home && home[0]) {
		snprintf(buf, sizeof buf, "%s/.local/share/vulkan/icd.d", home);
		if (dir_has_json(buf))
			return 1;
	}
	return 0;
}

static int
queue_shaders(Poof_Batch *batch)
{
	const char *bin;
	const char *vert_src[1];
	const char *frag_src[1];
	int vert_stale;
	int frag_stale;

	vert_src[0] = "vulkan/vt.vert";
	frag_src[0] = "vulkan/vt.frag";
	vert_stale = poof_needs_rebuild("vulkan/vt.vert.spv", vert_src, 1);
	frag_stale = poof_needs_rebuild("vulkan/vt.frag.spv", frag_src, 1);
	if (!vert_stale && !frag_stale)
		return 1;
	bin = glslang_bin();
	if (!bin) {
		fprintf(stderr, "vt: GLSL newer than SPIR-V; install glslang\n");
		return 0;
	}
	if (vert_stale) {
		Poof_Cmd cmd = {0};

		poof_cmd_append(&cmd, bin, "-V", "-o", "vulkan/vt.vert.spv", "vulkan/vt.vert");
		poof_batch_append_cmd(batch, cmd);
	}
	if (frag_stale) {
		Poof_Cmd cmd = {0};

		poof_cmd_append(&cmd, bin, "-V", "-o", "vulkan/vt.frag.spv", "vulkan/vt.frag");
		poof_batch_append_cmd(batch, cmd);
	}
	return 1;
}

static void
queue_app(Poof_Batch *batch, int release, int app, int cpu)
{
	Poof_CC cc;
	const char *input;

	poof_cc_init(&cc, POOF_CC_GCC | POOF_CC_CLANG, POOF_TARGET_HOST);
	poof_cmd_append(&cc.libs, "m");
	poof_cmd_append(&cc.extra_flags, "-std=c99", "-Wall", "-Wextra", "-Wmissing-declarations", "-Wno-implicit-fallthrough");

	if (app == APP_HEADLESS) {
		cc.output = "vt-headless";
		input = "src/main_headless.c";
	} else if (app == APP_LIVE) {
		cc.output = "vt-live";
		input = "src/main_headless_live.c";
	} else if (app == APP_CTL) {
		cc.output = "vtctl";
		input = "src/main_ctl.c";
	} else {
		cc.output = "vt";
		input = "src/main.c";
	}
	poof_cmd_append(&cc.inputs, input);
	poof_cmd_append(&cc.includes, ".", "godstack/Peak");
	if (app != APP_CTL)
		poof_cmd_append(&cc.includes, "godstack/Term");
	if (app == APP_VT)
		poof_cmd_append(&cc.includes, "godstack/Rend");
	poof_cc_append_linux(&cc, "-lutil", "-ldl", "-DPEAK_NO_AUDIO");
	poof_cc_append_macos(&cc, "-lutil");
	if (app == APP_VT) {
		if (!cpu) {
			poof_cmd_append(&cc.defines, "PEAK_VULKAN");
			poof_cc_append_linux(&cc, "-lvulkan");
			poof_cc_append_macos(&cc, "-lvulkan");
		}
		poof_cc_append_macos(&cc, "-framework", "AppKit", "-framework", "QuartzCore",
				"-framework", "AudioToolbox", "-framework", "CoreGraphics",
				"-framework", "CoreFoundation");
	}

	if (release) {
		cc.debug_mode = false;
		cc.optimization = POOF_O2 | vt_simd;
	} else {
		cc.debug_mode = true;
		cc.optimization = POOF_O0 | vt_simd;
		poof_cmd_append(&cc.defines, "DEBUG");
		poof_cmd_append(&cc.extra_flags, "-p");
	}

	poof_batch_append_cc(batch, &cc);
}

static void
queue_test(Poof_Batch *batch)
{
	Poof_Cmd cmd = {0};
	poof_cmd_append(&cmd, "sh", "tests/check");
	poof_batch_append_cmd(batch, cmd);
}

static int
queue_docs(void)
{
	Poof_Batch t = {0};
	Poof_Cmd cmd = {0};
	Poof_CC cc;
	size_t i;
	static const char *const md[] = {
		"docs/index.md",
		"docs/ctl.md",
		"docs/features.md",
		"docs/patches.md",
		"docs/renderer.md",
		"docs/term.md",
		"docs/tests.md",
	};

	poof_cmd_append(&cmd, "godstack/bin/cool_transpiler", "docs/views.cool", "-o", "docs/view.cool.c");
	poof_batch_append_cmd(&t, cmd);
	if (!poof_batch_run(&t, "vt docs transpile"))
		return 0;

	poof_cc_init(&cc, POOF_CC_GCC | POOF_CC_CLANG, POOF_TARGET_HOST);
	cc.output = "docs/docgen";
	poof_cmd_append(&cc.inputs, "docs/docgen.c");
	poof_cmd_append(&cc.extra_flags, "-std=c99", "-Wall", "-Wextra");
	if (!poof_cc_run(&cc)) {
		poof_cc_free(&cc);
		return 0;
	}
	poof_cc_free(&cc);

	for (i = 0; i < sizeof md / sizeof md[0]; i++) {
		Poof_Cmd g = {0};
		char html[256];
		char script[512];
		const char *base;
		size_t n;

		base = strrchr(md[i], '/');
		base = base ? base + 1 : md[i];
		n = strlen(base);
		if (n < 3 || 5 + (n - 3) + 5 >= sizeof html)
			return 0;
		memcpy(html, "docs/", 5);
		memcpy(html + 5, base, n - 3);
		memcpy(html + 5 + n - 3, ".html", 6);
		snprintf(script, sizeof script,
			"printf '%%s\\n' '%s' | docs/docgen > '%s'", md[i], html);
		poof_cmd_append(&g, "sh", "-c", script);
		if (!poof_cmd_run(&g))
			return 0;
	}
	return 1;
}

#define INSTALL_FOLDER "/usr/bin"
#define SHARE_FOLDER "/usr/share"

static void
queue_install_unix(Poof_Batch *batch)
{
	Poof_Cmd bin = {0};
	Poof_Cmd ctl = {0};
	Poof_Cmd vert = {0};
	Poof_Cmd frag = {0};
	Poof_Cmd font = {0};

	poof_cmd_append(&bin, "install", "-D", "-m", "755", "vt", INSTALL_FOLDER"/vt");
	poof_cmd_append(&ctl, "install", "-D", "-m", "755", "vtctl", INSTALL_FOLDER"/vtctl");
	poof_cmd_append(&vert, "install", "-D", "-m", "644", "vulkan/vt.vert.spv", SHARE_FOLDER"/vt/vulkan/vt.vert.spv");
	poof_cmd_append(&frag, "install", "-D", "-m", "644", "vulkan/vt.frag.spv", SHARE_FOLDER"/vt/vulkan/vt.frag.spv");
	poof_cmd_append(&font, "install", "-D", "-m", "644", "fonts/iosevka-mono.ttf", SHARE_FOLDER"/vt/fonts/iosevka-mono.ttf");
	poof_batch_append_cmd(batch, bin);
	poof_batch_append_cmd(batch, ctl);
	poof_batch_append_cmd(batch, vert);
	poof_batch_append_cmd(batch, frag);
	poof_batch_append_cmd(batch, font);
}

#if defined(__linux__)
static int
vt_ver(char *out, size_t n)
{
	FILE *f;
	char line[128];
	int maj = -1, min = -1, pat = -1;

	f = fopen("src/vt.h", "r");
	if (!f)
		return 1;
	while (fgets(line, sizeof line, f)) {
		sscanf(line, "#define VT_MAJOR %d", &maj);
		sscanf(line, "#define VT_MINOR %d", &min);
		sscanf(line, "#define VT_PATCH %d", &pat);
		if (maj >= 0 && min >= 0 && pat >= 0)
			break;
	}
	fclose(f);
	if (maj < 0 || min < 0 || pat < 0)
		return 1;
	snprintf(out, n, "%d.%d.%d", maj, min, pat);
	return 0;
}

static int
pkg_install(const char *mode, const char *src, const char *dir, const char *rel)
{
	Poof_Cmd cmd = {0};
	char dst[512];

	snprintf(dst, sizeof dst, "%s/%s", dir, rel);
	poof_cmd_append(&cmd, "install", "-D", "-m", mode, src, dst);
	if (!poof_cmd_run(&cmd))
		return 1;
	return 0;
}

static int
pkg_stage(const char *dir, int cpu)
{
	if (pkg_install("755", "vt", dir, "vt"))
		return 1;
	if (pkg_install("755", "vtctl", dir, "vtctl"))
		return 1;
	if (pkg_install("644", "fonts/iosevka-mono.ttf", dir, "fonts/iosevka-mono.ttf"))
		return 1;
	if (!cpu) {
		if (pkg_install("644", "vulkan/vt.vert.spv", dir, "vulkan/vt.vert.spv"))
			return 1;
		if (pkg_install("644", "vulkan/vt.frag.spv", dir, "vulkan/vt.frag.spv"))
			return 1;
	}
	if (pkg_install("644", "LICENSE", dir, "LICENSE"))
		return 1;
	if (pkg_install("644", "README.md", dir, "README.md"))
		return 1;
	if (poof_has_cmd("strip")) {
		Poof_Cmd bin = {0};
		Poof_Cmd ctl = {0};
		char vt_path[512];
		char ctl_path[512];

		snprintf(vt_path, sizeof vt_path, "%s/vt", dir);
		snprintf(ctl_path, sizeof ctl_path, "%s/vtctl", dir);
		poof_cmd_append(&bin, "strip", vt_path);
		poof_cmd_append(&ctl, "strip", ctl_path);
		if (!poof_cmd_run(&bin) || !poof_cmd_run(&ctl))
			return 1;
	}
	return 0;
}

static int
pkg_tar(const char *name)
{
	Poof_Cmd cmd = {0};
	char tar[512];

	snprintf(tar, sizeof tar, "packages/%s.tar.gz", name);
	poof_cmd_append(&cmd, "tar", "-C", "packages", "-czf", tar, name);
	if (!poof_cmd_run(&cmd))
		return 1;
	printf("vt: %s\n", tar);
	return 0;
}

static int
pkg_linux(void)
{
	Poof_Batch batch;
	struct utsname u;
	char ver[32];
	char name[128];
	char dir[160];

	if (vt_ver(ver, sizeof ver)) {
		fprintf(stderr, "vt: src/vt.h version\n");
		return 1;
	}
	if (!file_ok("fonts/iosevka-mono.ttf")) {
		fprintf(stderr, "vt: fonts/iosevka-mono.ttf missing\n");
		return 1;
	}
	if (uname(&u) != 0) {
		fprintf(stderr, "vt: uname\n");
		return 1;
	}
	poof_mkdir("packages");
	vt_simd = poof_support("vt package",
		"vulkan", 1,
		"glslang", glslang_bin() != NULL);

	batch = (Poof_Batch){0};
	if (!queue_shaders(&batch))
		return 1;
	queue_app(&batch, 1, APP_VT, 0);
	queue_app(&batch, 1, APP_CTL, 0);
	if (!poof_batch_run(&batch, "vt package"))
		return 1;
	snprintf(name, sizeof name, "vt-%s-linux-%s", ver, u.machine);
	snprintf(dir, sizeof dir, "packages/%s", name);
	poof_rm_recursive(dir);
	if (pkg_stage(dir, 0) || pkg_tar(name))
		return 1;

	batch = (Poof_Batch){0};
	queue_app(&batch, 1, APP_VT, 1);
	queue_app(&batch, 1, APP_CTL, 0);
	if (!poof_batch_run(&batch, "vt cpu"))
		return 1;
	snprintf(name, sizeof name, "vt-%s-linux-cpu-%s", ver, u.machine);
	snprintf(dir, sizeof dir, "packages/%s", name);
	poof_rm_recursive(dir);
	if (pkg_stage(dir, 1) || pkg_tar(name))
		return 1;
	return 0;
}
#endif

static int
pkg_build(void)
{
#if defined(__linux__)
	return pkg_linux();
#else
	fprintf(stderr, "vt: ./build package is linux-only\n");
	return 1;
#endif
}

int
main(int argc, char **argv)
{
	Poof_Batch batch = {0};
	int release = 1;
	int test = 0;
	int do_install = 0;
	int headless = 0;
	int cpu = 0;
	int deps = 0;
	int do_package = 0;
	int do_docs = 0;
	int have_vk;
	int i;
	const char *label;

	POOF_GO_REBUILD_URSELF(argc, argv);

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "debug") == 0) {
			release = 0;
		} else if (strcmp(argv[i], "release") == 0) {
			release = 1;
		} else if (strcmp(argv[i], "install") == 0) {
			release = 1;
			do_install = 1;
		} else if (strcmp(argv[i], "test") == 0) {
			test = 1;
		} else if (strcmp(argv[i], "headless") == 0) {
			headless = 1;
		} else if (strcmp(argv[i], "cpu") == 0) {
			cpu = 1;
		} else if (strcmp(argv[i], "deps") == 0) {
			deps = 1;
		} else if (strcmp(argv[i], "package") == 0) {
			release = 1;
			do_package = 1;
		} else if (strcmp(argv[i], "docs") == 0) {
			do_docs = 1;
		}
	}

	if (do_install)
		deps = 1;

	if (deps) {
		Poof_Batch d = {0};
		Poof_Cmd cmd = {0};

		poof_cmd_append(&cmd, "sh", "deps");
		poof_batch_append_cmd(&d, cmd);
		if (!poof_batch_run(&d, "vt deps"))
			return 1;
		if (!do_install)
			return 0;
	}

	if (do_package)
		return pkg_build();

	if (do_docs)
		return queue_docs() ? 0 : 1;

	have_vk = vulkan_ok();
	if (!cpu && !headless && !have_vk)
		cpu = 1;
	vt_simd = poof_support("vt",
		"vulkan", !cpu && !headless && have_vk,
		"glslang", glslang_bin() != NULL);

	if (headless) {
		queue_app(&batch, release, APP_HEADLESS, 0);
		queue_app(&batch, release, APP_LIVE, 0);
		queue_app(&batch, release, APP_CTL, 0);
		label = release ? "vt headless" : "vt headless debug";
	} else {
		if (!cpu && !queue_shaders(&batch))
			return 1;
		queue_app(&batch, release, APP_VT, cpu);
		queue_app(&batch, release, APP_CTL, 0);
		label = cpu
			? (release ? "vt cpu" : "vt cpu debug")
			: (release ? "vt release" : "vt debug");
	}
	if (!poof_batch_run(&batch, label))
		return 1;

	if (test) {
		Poof_Batch tbatch = {0};

		if (!headless) {
			queue_app(&tbatch, release, APP_HEADLESS, 0);
			queue_app(&tbatch, release, APP_LIVE, 0);
			if (!poof_batch_run(&tbatch, release ? "vt headless" : "vt headless debug"))
				return 1;
			tbatch = (Poof_Batch){0};
		}
		queue_test(&tbatch);
		if (!poof_batch_run(&tbatch, "vt test"))
			return 1;
	}
	if (do_install) {
		Poof_Batch ibatch = {0};

#if defined(_WIN32)
		fprintf(stderr, "TODO: ./build install on win32");
		return 1;
#else
		if (geteuid() != 0) {
			fprintf(stderr, "vt: sudo ./build install\n");
			return 1;
		}
		queue_install_unix(&ibatch);
#endif
		if (!poof_batch_run(&ibatch, "vt install"))
			return 1;
	}
	return 0;
}
