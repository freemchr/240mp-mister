/*
 * app.c — 240-MP: MiSTer Edition main application.
 *
 * VHS-style UI rendered in software into the core's framebuffer. Phase 2:
 * home module rail, a browse screen (placeholder data until the backends
 * land), and a working Settings screen (theme switcher persisted to config).
 *
 * GPL-3.0.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "ui/gfx.h"
#include "ui/theme.h"
#include "mister/platform.h"
#include "config.h"

#define SW 640
#define SH 480

/* ------------------------------------------------------------ app state -- */
typedef enum { SCREEN_HOME, SCREEN_BROWSE, SCREEN_SETTINGS } screen_t;

typedef struct {
    const char *name;    /* display */
    const char *tag;     /* short corner tag */
    int is_settings;
} module_t;

static const module_t MODULES[] = {
    { "Jellyfin",  "JF",  0 },
    { "Emby",      "EM",  0 },
    { "Plex",      "PLX", 0 },
    { "Live TV",   "TV",  0 },
    { "Settings",  "CFG", 1 },
};
#define MODULE_COUNT ((int)(sizeof(MODULES)/sizeof(MODULES[0])))

/* placeholder browse items (Phase 4 replaces with server data) */
static const char *DEMO_ITEMS[] = {
    "The Twilight Zone", "Night Gallery", "Tales from the Crypt",
    "Amazing Stories", "Outer Limits", "Ripping Yarns",
    "Monster Movie Matinee", "Creature Double Feature",
};
#define DEMO_COUNT ((int)(sizeof(DEMO_ITEMS)/sizeof(DEMO_ITEMS[0])))

static volatile sig_atomic_t g_run = 1;
static void on_sig(int s) { (void)s; g_run = 0; }

static const theme_t *g_theme;

/* ------------------------------------------------------------- chrome ---- */
static void draw_header(surface_t *s, const char *title)
{
    const theme_t *t = g_theme;
    gfx_rect(s, 0, 0, SW, 34, gfx_mix(t->surface, 0x000000, 60));
    gfx_rect(s, 0, 34, SW, 2, t->tertiary);
    gfx_text(s, 20, 10, 16, t->primary, "240-MP");
    gfx_text(s, 140, 14, 8, t->secondary, "MISTER EDITION");
    gfx_text_aligned(s, SW - 260, 12, 240, 12, t->accent, ALIGN_RIGHT, title);
}

static void draw_footer(surface_t *s, const char *hints)
{
    const theme_t *t = g_theme;
    gfx_rect(s, 0, SH - 26, SW, 26, gfx_mix(t->surface, 0x000000, 60));
    gfx_rect(s, 0, SH - 28, SW, 2, t->tertiary);
    gfx_text(s, 20, SH - 19, 8, t->secondary, hints);
}

static void draw_background(surface_t *s)
{
    const theme_t *t = g_theme;
    gfx_vgradient(s, 0, 0, SW, SH, t->surface, gfx_mix(t->surface, 0x000000, 90));
}

/* ------------------------------------------------------------- screens --- */
static void draw_home(surface_t *s, int sel)
{
    const theme_t *t = g_theme;
    draw_background(s);
    draw_header(s, "HOME");

    /* big VHS wordmark */
    gfx_text_aligned(s, 0, 70, SW, 8, t->tertiary, ALIGN_CENTER,
                     "- CHANNEL SELECT -");

    /* module rail: horizontal cards */
    int n = MODULE_COUNT;
    int cardw = 108, cardh = 132, gap = 12;
    int total = n * cardw + (n - 1) * gap;
    int x0 = (SW - total) / 2, y0 = 150;

    for (int i = 0; i < n; i++) {
        int x = x0 + i * (cardw + gap);
        int focused = (i == sel);
        rgb_t border = focused ? t->accent : t->tertiary;
        rgb_t fill   = focused ? gfx_mix(t->surface, t->accent, 40)
                               : gfx_mix(t->surface, 0x000000, 40);
        gfx_rect(s, x, y0, cardw, cardh, fill);
        gfx_rect_outline(s, x, y0, cardw, cardh, focused ? 3 : 1, border);

        /* corner tag */
        gfx_text(s, x + 8, y0 + 8, 8, t->secondary, MODULES[i].tag);
        /* centered name */
        gfx_text_aligned(s, x, y0 + cardh / 2 - 6, cardw, 12,
                         focused ? t->primary : t->secondary,
                         ALIGN_CENTER, MODULES[i].name);
        if (focused)
            gfx_text_aligned(s, x, y0 + cardh - 22, cardw, 8, t->accent,
                             ALIGN_CENTER, "> PLAY");
    }

    /* selected description strip */
    gfx_rect(s, 60, y0 + cardh + 26, SW - 120, 2, t->tertiary);
    gfx_text_aligned(s, 0, y0 + cardh + 40, SW, 8, t->secondary, ALIGN_CENTER,
                     MODULES[sel].is_settings ? "Adjust appearance and options"
                                              : "Sign in to browse and play");

    draw_footer(s, "[<>] SELECT   [ENTER] OPEN   [Q] EXIT TO MENU");
}

static void draw_browse(surface_t *s, int mod, int sel)
{
    const theme_t *t = g_theme;
    draw_background(s);
    draw_header(s, MODULES[mod].name);

    gfx_text(s, 24, 52, 8, t->tertiary, "LIBRARY  (demo data - server sign-in arrives in a later build)");

    int y0 = 78, rowh = 40;
    for (int i = 0; i < DEMO_COUNT; i++) {
        int y = y0 + i * rowh;
        int focused = (i == sel);
        if (focused) {
            gfx_rect(s, 20, y, SW - 40, rowh - 6, gfx_mix(t->surface, t->accent, 45));
            gfx_rect(s, 20, y, 4, rowh - 6, t->accent);
        }
        gfx_rect_outline(s, 20, y, SW - 40, rowh - 6, 1, t->tertiary);
        gfx_text(s, 40, y + 12, 12, focused ? t->primary : t->secondary,
                 DEMO_ITEMS[i]);
        gfx_text_aligned(s, SW - 140, y + 14, 100, 8,
                         focused ? t->accent : t->tertiary, ALIGN_RIGHT,
                         "RSUM >");   /* retro VCR-style resume label */
    }
    draw_footer(s, "[^v] SELECT   [ENTER] PLAY   [ESC] BACK");
}

static void draw_settings(surface_t *s, int sel, int theme_idx)
{
    const theme_t *t = g_theme;
    draw_background(s);
    draw_header(s, "SETTINGS");

    struct { const char *label, *value; } rows[] = {
        { "Color Scheme", THEMES[theme_idx].name },
        { "Version",      "0.2 (Phase 2)" },
        { "Framebuffer",  "0x22000000 640x480" },
    };
    int nrows = 3;
    int y0 = 90, rowh = 46;
    for (int i = 0; i < nrows; i++) {
        int y = y0 + i * rowh;
        int focused = (i == sel);
        if (focused) gfx_rect(s, 40, y, SW - 80, rowh - 8,
                              gfx_mix(t->surface, t->accent, 40));
        gfx_rect_outline(s, 40, y, SW - 80, rowh - 8, focused ? 2 : 1,
                         focused ? t->accent : t->tertiary);
        gfx_text(s, 60, y + 14, 12, focused ? t->primary : t->secondary,
                 rows[i].label);
        gfx_text_aligned(s, SW - 300, y + 14, 240, 12,
                         focused ? t->accent : t->secondary, ALIGN_RIGHT,
                         rows[i].value);
        if (i == 0 && focused)
            gfx_text_aligned(s, SW - 300, y + 14, 258, 12, t->accent,
                             ALIGN_RIGHT, "< >               ");
    }

    /* live theme preview swatches */
    int py = y0 + nrows * rowh + 16;
    gfx_text(s, 60, py, 8, t->tertiary, "PREVIEW");
    rgb_t sw[5] = { t->primary, t->secondary, t->tertiary, t->surface, t->accent };
    for (int i = 0; i < 5; i++) {
        gfx_rect(s, 60 + i * 44, py + 16, 36, 24, sw[i]);
        gfx_rect_outline(s, 60 + i * 44, py + 16, 36, 24, 1, t->tertiary);
    }

    draw_footer(s, "[^v] ROW   [<>] CHANGE   [ESC] BACK");
}

/* --------------------------------------------------------------- main ---- */
int main(void)
{
    /* We run as the core's Main (via MiSTer.ini main=), so stderr has no
     * terminal — route diagnostics to a file we can inspect over SSH. */
    freopen("/media/fat/240mp/app.log", "w", stderr);

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    config_load();
    int theme_idx = 0;
    const char *saved = config_get("color_scheme", "Video 1");
    for (int i = 0; i < THEME_COUNT; i++)
        if (!strcmp(THEMES[i].name, saved)) theme_idx = i;
    g_theme = &THEMES[theme_idx];

    surface_t *s = gfx_surface_create(SW, SH);
    if (!s) { fprintf(stderr, "surface alloc failed\n"); return 1; }
    if (plat_fb_open(SW, SH) != 0) { fprintf(stderr, "fb open failed\n"); return 1; }
    plat_input_open();

    screen_t screen = SCREEN_HOME;
    int home_sel = 0, browse_sel = 0, set_sel = 0, browse_mod = 0;

    fprintf(stderr, "240mp: app start, input devices ready\n");
    fflush(stderr);
    uint64_t last_hb = plat_now_ms();
    uint64_t frame = 0;

    while (g_run) {
        /* process a bounded number of input events per frame, then always
         * render — never let input volume starve the draw/present path. */
        nav_t ev;
        for (int guard = 0; guard < 8 && (ev = plat_input_poll()) != NAV_NONE; guard++) {
            fprintf(stderr, "240mp: nav=%d screen=%d\n", ev, screen);
            fflush(stderr);
            if (ev == NAV_QUIT) { g_run = 0; break; }

            if (screen == SCREEN_HOME) {
                if (ev == NAV_LEFT)  home_sel = (home_sel + MODULE_COUNT - 1) % MODULE_COUNT;
                if (ev == NAV_RIGHT) home_sel = (home_sel + 1) % MODULE_COUNT;
                if (ev == NAV_ACCEPT) {
                    if (MODULES[home_sel].is_settings) { screen = SCREEN_SETTINGS; set_sel = 0; }
                    else { screen = SCREEN_BROWSE; browse_mod = home_sel; browse_sel = 0; }
                }
                if (ev == NAV_BACK) g_run = 0;
            }
            else if (screen == SCREEN_BROWSE) {
                if (ev == NAV_UP)   browse_sel = (browse_sel + DEMO_COUNT - 1) % DEMO_COUNT;
                if (ev == NAV_DOWN) browse_sel = (browse_sel + 1) % DEMO_COUNT;
                if (ev == NAV_BACK) screen = SCREEN_HOME;
            }
            else if (screen == SCREEN_SETTINGS) {
                if (ev == NAV_UP)   set_sel = (set_sel + 3 - 1) % 3;
                if (ev == NAV_DOWN) set_sel = (set_sel + 1) % 3;
                if (set_sel == 0 && (ev == NAV_LEFT || ev == NAV_RIGHT)) {
                    theme_idx = (ev == NAV_RIGHT)
                              ? (theme_idx + 1) % THEME_COUNT
                              : (theme_idx + THEME_COUNT - 1) % THEME_COUNT;
                    g_theme = &THEMES[theme_idx];
                    config_set("color_scheme", THEMES[theme_idx].name);
                    config_save();
                }
                if (ev == NAV_BACK) screen = SCREEN_HOME;
            }
        }

        switch (screen) {
            case SCREEN_HOME:     draw_home(s, home_sel); break;
            case SCREEN_BROWSE:   draw_browse(s, browse_mod, browse_sel); break;
            case SCREEN_SETTINGS: draw_settings(s, set_sel, theme_idx); break;
        }
        gfx_scanlines(s, 24);           /* subtle CRT feel */
        plat_fb_present(s);

        /* frame limiter: cap ~60fps even if vsync ioctl doesn't block */
        uint64_t now = plat_now_ms();
        if (++frame % 120 == 0 || now - last_hb >= 2000) {
            fprintf(stderr, "240mp: heartbeat frame=%llu screen=%d\n",
                    (unsigned long long)frame, screen);
            fflush(stderr);
            last_hb = now;
        }
        struct timespec ts = { 0, 8 * 1000000L };  /* ~min 8ms/frame floor */
        nanosleep(&ts, NULL);
    }

    gfx_surface_free(s);

    /* We are the core's HPS binary (launched via MiSTer.ini main=). Exiting
     * must hand control back to the MiSTer menu by re-launching stock Main —
     * otherwise the system is left with no Main and needs a reboot.
     * plat_return_to_menu() closes input/fb then execs and does not return. */
    fprintf(stderr, "240mp: exiting to MiSTer menu\n");
    fflush(stderr);
    plat_return_to_menu();
    return 0;   /* only reached if exec failed */
}
