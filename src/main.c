#include "vt.c"

int
main(int argc, char **argv)
{
	u32 cols, rows;
	u64 next_present;
	u64 now;
	u64 left;
	u64 frame_ns;
	int timeout;

	(void)argc;
	(void)argv;

	if (!renderer_init()) {
		VTFATAL("Failed to initalize renderer!");
		VTASSERT(0, "Failed to initalize renderer!");
		renderer_destroy();
		return 1;
	}

	vt_events(&redraw);
	if (!running) {
		renderer_destroy();
		return 0;
	}
	renderer_get_grid(&renderer, &cols, &rows);
	if (!vt_init(cols, rows)) {
		VTFATAL("Failed to initalize terminal!");
		VTASSERT(0, "Failed to initalize terminal!");
		vt_destroy();
		renderer_destroy();
		return 1;
	}

    if (hz < 1) {
        VTERROR("");
        return 1;
    }

	frame_ns = 1000000000ull / (u64)hz;
	next_present = 0;

	while (running) {
		int full;

		now = peak_get_time();
		// if (!redraw)
		// 	timeout = -1;
		// else if (now >= next_present)
		// 	timeout = 0;
		// else {
		// 	left = next_present - now;
		// 	timeout = (int)(left / 1000000ull);
		// 	if (timeout < 1)
		// 		timeout = 0;
		// }
		// vt_wait(timeout);
		vt_events(&redraw);
		vt_ctl_pump();
		for (;;) {
			full = vt_ingest();
			now = peak_get_time();
			if (redraw && now >= next_present) {
				vt_present();
				redraw = false;
				next_present = peak_get_time() + frame_ns;
				break;
			}
			if (!full)
				break;
		}
	}

	vt_destroy();
	renderer_destroy();
#ifdef DEBUG
	vt_stage_report();
#endif
	VTINFO("Quit successfully!");
	return 0;
}
