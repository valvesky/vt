#include "godstack/Poof/poof.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
	APP_VT = 0,
	APP_HEADLESS,
	APP_LIVE
};

static void slangc_entry(Poof_Batch *batch, const char *src, const char *entry, const char *stage, const char *out);
static void queue_shaders(Poof_Batch *batch);
static void queue_app(Poof_Batch *batch, int release, int app, int cpu);
static void queue_test(Poof_Batch *batch);
static void queue_install_unix(Poof_Batch *batch);

static void
slangc_entry(Poof_Batch *batch, const char *src, const char *entry, const char *stage, const char *out)
{
	Poof_Cmd cmd = {0};
	poof_cmd_append(&cmd, "slangc", src, "-target", "spirv", "-entry", entry, "-stage", stage, "-o", out);
	poof_batch_append_cmd(batch, cmd);
}

static void
queue_shaders(Poof_Batch *batch)
{
	slangc_entry(batch, "vulkan/vt.slang", "vertMain", "vertex", "vulkan/vt.vert.spv");
	slangc_entry(batch, "vulkan/vt.slang", "fragMain", "fragment", "vulkan/vt.frag.spv");
}

static void
queue_app(Poof_Batch *batch, int release, int app, int cpu)
{
	Poof_CC cc;
	const char *input;

	poof_cc_init(&cc, POOF_CC_GCC | POOF_CC_CLANG, POOF_TARGET_HOST);
	poof_cmd_append(&cc.libs, "m");
	poof_cmd_append(&cc.extra_flags, "-std=c99", "-Wall", "-Wextra", "-Wmissing-declarations", "-Wno-implicit-fallthrough");
#if defined(__x86_64__) || defined(__i386__)
	poof_cmd_append(&cc.extra_flags, "-mbmi");
#endif

	if (app == APP_HEADLESS) {
		cc.output = "vt-headless";
		input = "src/main_headless.c";
	} else if (app == APP_LIVE) {
		cc.output = "vt-live";
		input = "src/main_headless_live.c";
	} else {
		cc.output = "vt";
		input = "src/main.c";
	}
	poof_cmd_append(&cc.inputs, input);
	poof_cmd_append(&cc.includes, ".", "godstack/Peak", "godstack/Term");
	if (app == APP_VT)
		poof_cmd_append(&cc.includes, "godstack/Rend");
	poof_cc_append_linux(&cc, "-lutil");
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
#if defined(__x86_64__) || defined(__i386__)
		cc.optimization = POOF_O2 | POOF_MSSE2;
#else
		cc.optimization = POOF_O2;
#endif
	} else {
		cc.debug_mode = true;
		cc.optimization = POOF_O0;
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

#define INSTALL_FOLDER "/usr/bin"
#define SHARE_FOLDER "/usr/share"

static void
queue_install_unix(Poof_Batch *batch)
{
	Poof_Cmd bin = {0};
	Poof_Cmd vert = {0};
	Poof_Cmd frag = {0};
	Poof_Cmd font = {0};

	poof_cmd_append(&bin, "install", "-D", "-m", "755", "vt", INSTALL_FOLDER"/vt");
	poof_cmd_append(&vert, "install", "-D", "-m", "644", "vulkan/vt.vert.spv", SHARE_FOLDER"/vt/vulkan/vt.vert.spv");
	poof_cmd_append(&frag, "install", "-D", "-m", "644", "vulkan/vt.frag.spv", SHARE_FOLDER"/vt/vulkan/vt.frag.spv");
	poof_cmd_append(&font, "install", "-D", "-m", "644", "fonts/iosevka-mono.ttf", SHARE_FOLDER"/vt/fonts/iosevka-mono.ttf");
	poof_batch_append_cmd(batch, bin);
	poof_batch_append_cmd(batch, vert);
	poof_batch_append_cmd(batch, frag);
	poof_batch_append_cmd(batch, font);
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
		}
	}

	if (headless) {
		queue_app(&batch, release, APP_HEADLESS, 0);
		queue_app(&batch, release, APP_LIVE, 0);
		label = release ? "vt headless" : "vt headless debug";
	} else {
		if (!cpu)
			queue_shaders(&batch);
		queue_app(&batch, release, APP_VT, cpu);
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
