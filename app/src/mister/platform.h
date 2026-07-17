/*
 * platform.h — MiSTer hardware I/O for the app: framebuffer presentation and
 * input events. Isolates all device access so screens deal only in surfaces
 * and nav events.  GPL-3.0.
 */
#ifndef PLATFORM_H
#define PLATFORM_H
#include "../ui/gfx.h"

/* --- framebuffer --- */
/* Maps the 240MP core's fabric framebuffer (0x22000000, 640x480 XRGB8888).
 * Returns 0 on success. */
int  plat_fb_open(int w, int h);
void plat_fb_close(void);
/* Wait for the core's vsync (best-effort), then blit surface -> fb. */
void plat_fb_present(const surface_t *s);

/* --- input --- */
typedef enum {
    NAV_NONE = 0,
    NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT,
    NAV_ACCEPT, NAV_BACK, NAV_MENU, NAV_QUIT
} nav_t;

int  plat_input_open(void);
void plat_input_close(void);
/* Non-blocking: returns the next nav event or NAV_NONE. */
nav_t plat_input_poll(void);

/* monotonic milliseconds */
uint64_t plat_now_ms(void);

/* Return control to the MiSTer menu by re-launching stock Main_MiSTer.
 * Does not return on success (execs). Call after cleanup. */
void plat_return_to_menu(void);

#endif /* PLATFORM_H */
