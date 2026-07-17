/*
 * fbtest — 240-MP: MiSTer Edition, Phase 0 hardware-interface validation.
 *
 * Exercises every MiSTer HPS interface the media app will depend on:
 *   1. /dev/MiSTer_cmd  — ask Main_MiSTer to set the scaler framebuffer size/format
 *   2. /dev/fb0         — mmap and draw (MiSTer_fb.c fbdev driver → ascal scaler)
 *   3. FBIO_WAITFORVSYNC — frame pacing from the core's VSync IRQ
 *   4. /dev/MrAudio     — raw S16LE 48 kHz stereo → alsa.sv HPS→FPGA audio DMA
 *   5. /dev/input/event*— evdev keyboard/gamepad events
 *
 * Draws SMPTE-style bars, a grayscale ramp, R/G/B channel-order swatches, an
 * overscan border, live fb info text, and a bouncing box (tearing check),
 * while playing a 440 Hz tone (pitch check: if the rate is wrong you'll hear it).
 *
 * Usage: fbtest [WxH] [--fmt 8888|565] [--rb 0|1] [--no-audio] [--devmem]
 *   defaults: 640x480 --fmt 8888 --rb 1  (matches Main's Linux-console fb setup)
 *   --devmem: write the 240MP core's fabric-owned framebuffer directly
 *             (mmap /dev/mem at 0x20000000, fixed 640x480 XRGB8888) instead of
 *             /dev/fb0.  No Main_MiSTer fb-terminal state needed; /dev/fb0 is
 *             opened only for the FBIO_WAITFORVSYNC ioctl.
 * Exit: ESC / q on keyboard, or SELECT+START style BTN_MODE on a pad; SIGINT ok.
 *
 * Static-linked, no dependencies. GPL-3.0.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <poll.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, uint32_t)
#endif

/* ------------------------------------------------------------------ font --
 * 8x8 bitmap font, glyphs for ASCII 0x20..0x5F (space .. underscore).
 * Derived from font8x8 by Daniel Hepper (public domain). LSB = leftmost px.
 */
static const uint8_t FONT8X8[96][8] = {
    {0,0,0,0,0,0,0,0},                                              /* ' ' */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},                      /* ! */
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},                      /* " */
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},                      /* # */
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},                      /* $ */
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},                      /* % */
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},                      /* & */
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},                      /* ' */
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},                      /* ( */
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},                      /* ) */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},                      /* * */
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},                      /* + */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},                      /* , */
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},                      /* - */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},                      /* . */
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},                      /* / */
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},                      /* 0 */
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},                      /* 1 */
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},                      /* 2 */
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},                      /* 3 */
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},                      /* 4 */
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},                      /* 5 */
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},                      /* 6 */
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},                      /* 7 */
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},                      /* 8 */
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},                      /* 9 */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},                      /* : */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},                      /* ; */
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},                      /* < */
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},                      /* = */
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},                      /* > */
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},                      /* ? */
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},                      /* @ */
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},                      /* A */
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},                      /* B */
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},                      /* C */
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},                      /* D */
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},                      /* E */
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},                      /* F */
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},                      /* G */
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},                      /* H */
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},                      /* I */
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},                      /* J */
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},                      /* K */
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},                      /* L */
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},                      /* M */
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},                      /* N */
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},                      /* O */
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},                      /* P */
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},                      /* Q */
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},                      /* R */
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},                      /* S */
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},                      /* T */
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},                      /* U */
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},                      /* V */
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},                      /* W */
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},                      /* X */
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},                      /* Y */
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},                      /* Z */
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},                      /* [ */
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},                      /* \ */
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},                      /* ] */
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00},                      /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},                      /* _ */
};

/* --------------------------------------------------------------- globals -- */
static volatile sig_atomic_t g_running = 1;
static int g_audio_enabled = 1;

static void on_sigint(int sig) { (void)sig; g_running = 0; }

/* --------------------------------------------------------------- helpers -- */
static int mister_cmd(const char *cmd)
{
    int fd = open("/dev/MiSTer_cmd", O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "warn: cannot open /dev/MiSTer_cmd: %s\n", strerror(errno));
        return -1;
    }
    ssize_t n = write(fd, cmd, strlen(cmd));
    close(fd);
    if (n < 0) {
        fprintf(stderr, "warn: write to /dev/MiSTer_cmd failed: %s\n", strerror(errno));
        return -1;
    }
    printf("MiSTer_cmd: %s\n", cmd);
    return 0;
}

static void print_fb_mode_param(void)
{
    FILE *f = fopen("/sys/module/MiSTer_fb/parameters/mode", "r");
    if (!f) return;
    char buf[128] = {0};
    if (fgets(buf, sizeof buf, f))
        printf("MiSTer_fb mode param: %s", buf);
    fclose(f);
}

/* ----------------------------------------------------------------- audio --
 * 440 Hz sine, S16LE interleaved stereo 48 kHz, written straight to
 * /dev/MrAudio (the ALSA `default` device routes here anyway; writing the
 * device directly validates the actual FPGA path with zero dependencies).
 * Ring buffer on the kernel side is 512 KiB (~2.6 s); we feed 100 ms chunks
 * and sleep 90 ms so it never starves or overruns.
 */
static void *audio_thread(void *arg)
{
    (void)arg;
    int fd = open("/dev/MrAudio", O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "warn: cannot open /dev/MrAudio: %s (no tone)\n", strerror(errno));
        return NULL;
    }
    enum { RATE = 48000, CHUNK_FRAMES = 4800 };       /* 100 ms */
    static int16_t chunk[CHUNK_FRAMES * 2];
    double phase = 0.0, step = 2.0 * M_PI * 440.0 / RATE;

    while (g_running) {
        for (int i = 0; i < CHUNK_FRAMES; i++) {
            int16_t s = (int16_t)(sin(phase) * 8000.0);  /* ~-12 dBFS */
            chunk[i * 2 + 0] = s;
            chunk[i * 2 + 1] = s;
            phase += step;
            if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
        }
        /* 4-byte-aligned writes required by the driver; whole chunk is. */
        if (write(fd, chunk, sizeof chunk) < 0) {
            fprintf(stderr, "warn: /dev/MrAudio write failed: %s\n", strerror(errno));
            break;
        }
        usleep(90 * 1000);
    }
    close(fd);
    return NULL;
}

/* ----------------------------------------------------------------- input -- */
#define MAX_EVDEV 16
static int open_evdev_all(struct pollfd *pfds)
{
    int n = 0;
    DIR *d = opendir("/dev/input");
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < MAX_EVDEV) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[64];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        pfds[n].fd = fd;
        pfds[n].events = POLLIN;
        n++;
    }
    closedir(d);
    printf("input: opened %d evdev device(s)\n", n);
    return n;
}

/* returns 1 if an exit key was pressed */
static int drain_input(struct pollfd *pfds, int n)
{
    if (poll(pfds, n, 0) <= 0) return 0;
    int want_exit = 0;
    for (int i = 0; i < n; i++) {
        if (!(pfds[i].revents & POLLIN)) continue;
        struct input_event ev[8];
        ssize_t r = read(pfds[i].fd, ev, sizeof ev);
        for (size_t k = 0; k < r / sizeof ev[0]; k++) {
            if (ev[k].type != EV_KEY || ev[k].value != 1) continue;
            printf("input: dev%d key/btn code %u down\n", i, ev[k].code);
            if (ev[k].code == KEY_ESC || ev[k].code == KEY_Q ||
                ev[k].code == BTN_MODE)
                want_exit = 1;
        }
    }
    return want_exit;
}

/* --------------------------------------------------------------- drawing -- */
struct surface {
    uint32_t *px;      /* XRGB8888 backbuffer */
    int w, h;
};

static void fill_rect(struct surface *s, int x, int y, int w, int h, uint32_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    for (int j = 0; j < h; j++) {
        uint32_t *row = s->px + (size_t)(y + j) * s->w + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

static void draw_text(struct surface *s, int x, int y, int scale,
                      uint32_t c, const char *str)
{
    for (; *str; str++, x += 8 * scale) {
        unsigned ch = (unsigned char)*str;
        if (ch >= 'a' && ch <= 'z') ch -= 32;          /* upper-case only font */
        if (ch < 0x20 || ch > 0x5F) ch = '?';
        const uint8_t *g = FONT8X8[ch - 0x20];
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++)
                if (g[row] & (1u << col))
                    fill_rect(s, x + col * scale, y + row * scale, scale, scale, c);
    }
}

static void draw_static_scene(struct surface *s)
{
    const int W = s->w, H = s->h;

    /* SMPTE-ish bars over the top ~55% */
    static const uint32_t bars[7] = {
        0xFFFFFFu, 0xFFFF00u, 0x00FFFFu, 0x00FF00u,
        0xFF00FFu, 0xFF0000u, 0x0000FFu };
    int bar_h = H * 55 / 100;
    for (int i = 0; i < 7; i++)
        fill_rect(s, W * i / 7, 0, W / 7 + 1, bar_h, bars[i]);

    /* grayscale ramp */
    int ramp_y = bar_h, ramp_h = H * 12 / 100;
    for (int x = 0; x < W; x++) {
        uint32_t v = (uint32_t)(x * 255 / (W - 1));
        fill_rect(s, x, ramp_y, 1, ramp_h, v << 16 | v << 8 | v);
    }

    /* channel-order swatches: labeled pure R / G / B */
    int sw_y = ramp_y + ramp_h + H * 2 / 100, sw = H * 10 / 100;
    static const uint32_t rgb[3] = { 0xFF0000u, 0x00FF00u, 0x0000FFu };
    static const char *lbl[3] = { "R", "G", "B" };
    for (int i = 0; i < 3; i++) {
        int x = W * 8 / 100 + i * (sw + W * 4 / 100);
        fill_rect(s, x, sw_y, sw, sw, rgb[i]);
        draw_text(s, x + sw / 2 - 8, sw_y + sw / 2 - 8, 2, 0xFFFFFFu, lbl[i]);
    }

    /* overscan border: 2px white frame at the extreme edge + inset ticks */
    fill_rect(s, 0, 0, W, 2, 0xFFFFFFu);
    fill_rect(s, 0, H - 2, W, 2, 0xFFFFFFu);
    fill_rect(s, 0, 0, 2, H, 0xFFFFFFu);
    fill_rect(s, W - 2, 0, 2, H, 0xFFFFFFu);
    for (int inset = 8; inset <= 24; inset += 8) {     /* 8/16/24 px ticks */
        fill_rect(s, inset, inset, 12, 2, 0xFFFF00u);
        fill_rect(s, inset, inset, 2, 12, 0xFFFF00u);
        fill_rect(s, W - inset - 12, H - inset - 2, 12, 2, 0xFFFF00u);
        fill_rect(s, W - inset - 2, H - inset - 12, 2, 12, 0xFFFF00u);
    }

    draw_text(s, W * 8 / 100, H * 84 / 100, 2, 0xFFFFFFu,
              "240-MP MISTER EDITION");
}

/* ------------------------------------------------------------------ main -- */
#define DEVMEM_FB_BASE 0x20000000u   /* 240MP core's fabric framebuffer */
#define DEVMEM_FB_W    640
#define DEVMEM_FB_H    480

int main(int argc, char **argv)
{
    int req_w = 640, req_h = 480, rb = 1, devmem = 0;
    const char *fmt = "8888";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-audio")) g_audio_enabled = 0;
        else if (!strcmp(argv[i], "--devmem")) devmem = 1;
        else if (!strcmp(argv[i], "--fmt") && i + 1 < argc) fmt = argv[++i];
        else if (!strcmp(argv[i], "--rb") && i + 1 < argc) rb = atoi(argv[++i]);
        else if (sscanf(argv[i], "%dx%d", &req_w, &req_h) == 2) { /* ok */ }
        else {
            fprintf(stderr,
                "usage: %s [WxH] [--fmt 8888|565] [--rb 0|1] [--no-audio] [--devmem]\n",
                argv[0]);
            return 2;
        }
    }
    if (strcmp(fmt, "8888") != 0) {
        fprintf(stderr, "note: this test only draws 8888; use default fmt\n");
        return 2;
    }

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    int fb = -1;                        /* fd used for FBIO_WAITFORVSYNC */
    uint8_t *fbmem;
    size_t map_len;
    struct fb_var_screeninfo vi = {0};
    struct fb_fix_screeninfo fi = {0};

    if (devmem) {
        /* Fabric-owned framebuffer: fixed geometry, mapped via /dev/mem. */
        vi.xres = DEVMEM_FB_W; vi.yres = DEVMEM_FB_H; vi.bits_per_pixel = 32;
        fi.line_length = DEVMEM_FB_W * 4;
        map_len = (size_t)fi.line_length * vi.yres;

        int dm = open("/dev/mem", O_RDWR | O_SYNC);
        if (dm < 0) { perror("open /dev/mem"); return 1; }
        fbmem = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED,
                     dm, DEVMEM_FB_BASE);
        close(dm);
        if (fbmem == MAP_FAILED) { perror("mmap /dev/mem"); return 1; }
        printf("devmem fb: %ux%u @ 0x%08x stride=%u\n",
               vi.xres, vi.yres, DEVMEM_FB_BASE, fi.line_length);

        fb = open("/dev/fb0", O_RDWR);  /* vsync ioctl only; ok if absent */
    } else {
        /* 1. ask Main to set the scaler framebuffer */
        char cmd[64];
        snprintf(cmd, sizeof cmd, "fb_cmd1 %s %d %d %d", fmt, rb, req_w, req_h);
        mister_cmd(cmd);
        usleep(200 * 1000);             /* let Main + kernel module settle */
        print_fb_mode_param();

        /* 2. open + map /dev/fb0 with whatever geometry actually resulted */
        fb = open("/dev/fb0", O_RDWR);
        if (fb < 0) { perror("open /dev/fb0"); return 1; }
        if (ioctl(fb, FBIOGET_VSCREENINFO, &vi) || ioctl(fb, FBIOGET_FSCREENINFO, &fi)) {
            perror("fb ioctl"); return 1;
        }
        printf("fb0: %ux%u bpp=%u stride=%u smem=%u\n",
               vi.xres, vi.yres, vi.bits_per_pixel, fi.line_length, fi.smem_len);
        if (vi.bits_per_pixel != 32) {
            fprintf(stderr, "error: expected 32bpp, got %u\n", vi.bits_per_pixel);
            return 1;
        }
        map_len = (size_t)fi.line_length * vi.yres;
        fbmem = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
        if (fbmem == MAP_FAILED) { perror("mmap fb0"); return 1; }
    }

    /* 3. backbuffer + static scene */
    struct surface s = { .w = (int)vi.xres, .h = (int)vi.yres };
    s.px = calloc((size_t)s.w * s.h, 4);
    if (!s.px) { perror("calloc"); return 1; }
    draw_static_scene(&s);

    /* 4. audio + input */
    pthread_t ath;
    if (g_audio_enabled) pthread_create(&ath, NULL, audio_thread, NULL);
    struct pollfd pfds[MAX_EVDEV];
    int nev = open_evdev_all(pfds);

    printf("running: 440Hz tone %s, ESC/Q/BTN_MODE exits\n",
           g_audio_enabled ? "on" : "off");

    /* 5. animation loop */
    int bx = s.w / 3, by = s.h * 70 / 100, dx = 3, dy = 2;
    const int bs = s.h / 24;
    uint32_t frame = 0, vsync_arg = 0;
    int vsync_ok = 1;
    char info[96];

    while (g_running) {
        if (drain_input(pfds, nev)) break;

        /* erase dynamic areas, then redraw */
        fill_rect(&s, 0, s.h * 68 / 100, s.w, s.h * 14 / 100, 0x101010u);
        bx += dx; by += dy;
        if (bx < 4 || bx + bs > s.w - 4) { dx = -dx; bx += dx; }
        if (by < s.h * 68 / 100 || by + bs > s.h * 82 / 100) { dy = -dy; by += dy; }
        fill_rect(&s, bx, by, bs, bs, 0xFF8000u);

        fill_rect(&s, 0, s.h * 90 / 100, s.w, 20, 0x000000u);
        snprintf(info, sizeof info, "FRAME %06u  %ux%u  VSYNC %s",
                 frame, vi.xres, vi.yres, vsync_ok ? "OK" : "N/A");
        draw_text(&s, s.w * 8 / 100, s.h * 90 / 100, 2, 0x00FF00u, info);

        /* present: wait for the core's vsync, then blit */
        if (vsync_ok && ioctl(fb, FBIO_WAITFORVSYNC, &vsync_arg) < 0) {
            if (frame == 0)
                fprintf(stderr, "warn: FBIO_WAITFORVSYNC unsupported: %s\n",
                        strerror(errno));
            vsync_ok = 0;
        }
        if (!vsync_ok) usleep(16667);

        if (fi.line_length == (unsigned)s.w * 4) {
            memcpy(fbmem, s.px, (size_t)s.w * s.h * 4);
        } else {
            for (int y = 0; y < s.h; y++)
                memcpy(fbmem + (size_t)y * fi.line_length,
                       s.px + (size_t)y * s.w, (size_t)s.w * 4);
        }
        frame++;
    }

    g_running = 0;
    if (g_audio_enabled) pthread_join(ath, NULL);
    for (int i = 0; i < nev; i++) close(pfds[i].fd);
    munmap(fbmem, map_len);
    close(fb);
    free(s.px);
    printf("exited after %u frames\n", frame);
    return 0;
}
