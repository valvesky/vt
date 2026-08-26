#include "godstack/Poof/poof.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void glslc_entry(Poof_Batch *batch, const char *src, const char *out);
static void queue_shaders(Poof_Batch *batch);
static void queue_vt(Poof_Batch *batch, int release, int headless);
static void queue_test(Poof_Batch *batch);
static void queue_install_unix(Poof_Batch *batch);

static void
glslc_entry(Poof_Batch *batch, const char *src, const char *out)
{
    Poof_Cmd cmd = {0};
    poof_cmd_append(&cmd, "glslc", src, "-o", out);
    poof_batch_append_cmd(batch, cmd);
}

static void
queue_shaders(Poof_Batch *batch)
{
    glslc_entry(batch, "vulkan/vt.vert", "vulkan/vt.vert.spv");
    glslc_entry(batch, "vulkan/vt.frag", "vulkan/vt.frag.spv");
}

static void
queue_vt(Poof_Batch *batch, int release, int headless)
{
    Poof_CC cc;

    poof_cc_init(&cc, POOF_CC_GCC | POOF_CC_CLANG, POOF_TARGET_HOST);
    cc.output = "vt";
    poof_cmd_append(&cc.inputs, "src/vt.c");
    poof_cmd_append(&cc.libs, "m");
    poof_cmd_append(&cc.extra_flags, "-std=c99", "-Wall", "-Wextra", "-Wmissing-declarations", "-Wno-implicit-fallthrough");
#if defined(__x86_64__) || defined(__i386__)
    poof_cmd_append(&cc.extra_flags, "-mbmi");
#endif
    if (headless) {
        poof_cmd_append(&cc.includes, ".", "godstack/Peak", "godstack/Term");
        poof_cmd_append(&cc.defines, "VT_HEADLESS");
        poof_cc_append_linux(&cc, "-lutil");
        poof_cc_append_macos(&cc, "-lutil");
    } else {
        poof_cmd_append(&cc.includes, ".", "godstack/Peak", "godstack/Rend", "godstack/Term");
        poof_cmd_append(&cc.defines, "PEAK_VULKAN");
        poof_cc_append_linux(&cc, "-lvulkan", "-lutil");
        poof_cc_append_macos(&cc, "-lutil", "-lvulkan", "-framework", "AppKit", "-framework", "QuartzCore", "-framework", "AudioToolbox", "-framework", "CoreGraphics", "-framework", "CoreFoundation");
    }

    if (release) {
        cc.debug_mode = false;
#if defined(__x86_64__) || defined(__i386__)
        cc.optimization = POOF_O2 | POOF_MSSE2;
#else
        cc.optimization = POOF_O2;
#endif
        poof_cmd_append(&cc.extra_flags, "--fast-math");
    } else {
        cc.debug_mode = true;
        cc.optimization = POOF_O0;
        poof_cmd_append(&cc.defines, "DEBUG");
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
        }
    }

    if (!headless)
        queue_shaders(&batch);
    queue_vt(&batch, release, headless);
    if (headless)
        label = release ? "vt headless" : "vt headless debug";
    else
        label = release ? "vt release" : "vt debug";
    if (!poof_batch_run(&batch, label)) {
        return 1;
    }
    if (test) {
        Poof_Batch tbatch = {0};
        queue_test(&tbatch);
        if (!poof_batch_run(&tbatch, "vt test"))
            return 1;
    }
    if (do_install) {
        Poof_Batch ibatch = {0};

#if defined(_WIN32)
        fprintf(stderr, "TODO: ./build install on win32");
        return 1;
        // queue_install_win32(&ibatch);
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
