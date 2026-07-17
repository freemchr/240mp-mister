/* platform.c — MiSTer framebuffer + input. See platform.h. GPL-3.0. */
#define _GNU_SOURCE
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, uint32_t)
#endif

#define FB_PHYS_BASE 0x22000000u   /* must match MP240.sv FB_BASE */

static uint32_t *g_fb;
static int       g_fbw, g_fbh, g_stride_px;
static int       g_fb0 = -1;       /* /dev/fb0, WAITFORVSYNC only */

int plat_fb_open(int w, int h)
{
    int dm = open("/dev/mem", O_RDWR | O_SYNC);
    if (dm < 0) { perror("open /dev/mem"); return -1; }
    size_t len = (size_t)w * h * 4;
    g_fb = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, dm, FB_PHYS_BASE);
    close(dm);
    if (g_fb == MAP_FAILED) { perror("mmap fb"); g_fb = NULL; return -1; }
    memset(g_fb, 0, len);          /* clear power-up DDR garbage */
    g_fbw = w; g_fbh = h; g_stride_px = w;
    g_fb0 = open("/dev/fb0", O_RDWR);   /* ok if it fails; vsync degrades to sleep */
    return 0;
}

void plat_fb_close(void)
{
    if (g_fb) munmap(g_fb, (size_t)g_fbw * g_fbh * 4);
    if (g_fb0 >= 0) close(g_fb0);
    g_fb = NULL; g_fb0 = -1;
}

void plat_fb_present(const surface_t *s)
{
    static int vsync_ok = 1;
    uint32_t arg = 0;
    if (vsync_ok && g_fb0 >= 0 && ioctl(g_fb0, FBIO_WAITFORVSYNC, &arg) < 0)
        vsync_ok = 0;
    if (!vsync_ok) { struct timespec ts = {0, 16666667}; nanosleep(&ts, NULL); }

    int w = s->w < g_fbw ? s->w : g_fbw;
    int h = s->h < g_fbh ? s->h : g_fbh;
    for (int y = 0; y < h; y++)
        memcpy(g_fb + (size_t)y * g_stride_px, s->px + (size_t)y * s->w, (size_t)w * 4);
}

/* ------------------------------- input --------------------------------- */

#define MAX_EVDEV 16
static struct pollfd g_ev[MAX_EVDEV];
static int g_nev;

#ifndef EVIOCGRAB
#define EVIOCGRAB _IOW('E', 0x90, int)
#endif

int plat_input_open(void)
{
    g_nev = 0;
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d)) && g_nev < MAX_EVDEV) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char path[64];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        /* We are the core's HPS binary (via main=), so no other process
         * should hold these. Grab so keystrokes don't leak to the Linux
         * console; ignore failure (device may be a non-input node). */
        ioctl(fd, EVIOCGRAB, (void *)1);
        g_ev[g_nev].fd = fd;
        g_ev[g_nev].events = POLLIN;
        g_nev++;
    }
    closedir(d);
    return g_nev;
}

void plat_input_close(void)
{
    for (int i = 0; i < g_nev; i++) close(g_ev[i].fd);
    g_nev = 0;
}

/* Map a key/button code to a nav event. Covers keyboard arrows/enter/esc and
 * common gamepad buttons + D-pad (BTN_DPAD_*) and hat/abs handled separately. */
static nav_t map_key(uint16_t code)
{
    switch (code) {
        case KEY_UP:                          return NAV_UP;
        case KEY_DOWN:                        return NAV_DOWN;
        case KEY_LEFT:                        return NAV_LEFT;
        case KEY_RIGHT:                       return NAV_RIGHT;
        case KEY_ENTER: case KEY_SPACE:
        case BTN_SOUTH:                       return NAV_ACCEPT;  /* == BTN_A */
        case KEY_ESC: case KEY_BACKSPACE:
        case BTN_EAST:                        return NAV_BACK;    /* == BTN_B */
        case BTN_START: case KEY_TAB:         return NAV_MENU;
        case KEY_Q:                           return NAV_QUIT;
        case BTN_DPAD_UP:                     return NAV_UP;
        case BTN_DPAD_DOWN:                   return NAV_DOWN;
        case BTN_DPAD_LEFT:                   return NAV_LEFT;
        case BTN_DPAD_RIGHT:                  return NAV_RIGHT;
        default:                              return NAV_NONE;
    }
}

nav_t plat_input_poll(void)
{
    if (g_nev <= 0) return NAV_NONE;
    if (poll(g_ev, g_nev, 0) <= 0) return NAV_NONE;

    for (int i = 0; i < g_nev; i++) {
        if (!(g_ev[i].revents & POLLIN)) continue;
        struct input_event ev[16];
        ssize_t r = read(g_ev[i].fd, ev, sizeof ev);
        for (size_t k = 0; r > 0 && k < r / sizeof ev[0]; k++) {
            if (ev[k].type == EV_KEY && ev[k].value == 1) {
                nav_t n = map_key(ev[k].code);
                if (n != NAV_NONE) return n;
            }
            /* Analog D-pad reported as HAT axes on many pads. */
            if (ev[k].type == EV_ABS) {
                if (ev[k].code == ABS_HAT0Y) {
                    if (ev[k].value < 0) return NAV_UP;
                    if (ev[k].value > 0) return NAV_DOWN;
                } else if (ev[k].code == ABS_HAT0X) {
                    if (ev[k].value < 0) return NAV_LEFT;
                    if (ev[k].value > 0) return NAV_RIGHT;
                }
            }
        }
    }
    return NAV_NONE;
}

uint64_t plat_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Stock Main_MiSTer path. Re-launching it loads the menu core and restores
 * the normal MiSTer UI + input handling. */
#define STOCK_MAIN "/media/fat/MiSTer"

void plat_return_to_menu(void)
{
    /* release input + fb so the successor starts clean */
    plat_input_close();
    plat_fb_close();
    execl(STOCK_MAIN, "MiSTer", (char *)NULL);
    /* If exec fails, fall back to loading the menu core over the cmd FIFO so
     * the user isn't stranded on a blank core. */
    int fd = open("/dev/MiSTer_cmd", O_WRONLY);
    if (fd >= 0) { dprintf(fd, "load_core /media/fat/menu.rbf"); close(fd); }
    perror("exec stock Main");
}
