/* gfx.c — software rendering primitives. See gfx.h. GPL-3.0. */
#include "gfx.h"
#include "font8x8.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

surface_t *gfx_surface_create(int w, int h)
{
    surface_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->px = calloc((size_t)w * h, sizeof(rgb_t));
    if (!s->px) { free(s); return NULL; }
    s->w = w; s->h = h;
    return s;
}

void gfx_surface_free(surface_t *s)
{
    if (!s) return;
    free(s->px);
    free(s);
}

void gfx_clear(surface_t *s, rgb_t c)
{
    size_t n = (size_t)s->w * s->h;
    for (size_t i = 0; i < n; i++) s->px[i] = c;
}

static inline void clip(surface_t *s, int *x, int *y, int *w, int *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > s->w) *w = s->w - *x;
    if (*y + *h > s->h) *h = s->h - *y;
}

void gfx_rect(surface_t *s, int x, int y, int w, int h, rgb_t c)
{
    clip(s, &x, &y, &w, &h);
    for (int j = 0; j < h; j++) {
        rgb_t *row = s->px + (size_t)(y + j) * s->w + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

void gfx_rect_outline(surface_t *s, int x, int y, int w, int h, int t, rgb_t c)
{
    gfx_rect(s, x, y, w, t, c);              /* top */
    gfx_rect(s, x, y + h - t, w, t, c);      /* bottom */
    gfx_rect(s, x, y, t, h, c);              /* left */
    gfx_rect(s, x + w - t, y, t, h, c);      /* right */
}

static inline uint8_t chan(rgb_t c, int shift) { return (c >> shift) & 0xFF; }

rgb_t gfx_mix(rgb_t a, rgb_t b, uint8_t t)
{
    int inv = 255 - t;
    uint8_t r = (chan(a,16) * inv + chan(b,16) * t) / 255;
    uint8_t g = (chan(a,8)  * inv + chan(b,8)  * t) / 255;
    uint8_t bl= (chan(a,0)  * inv + chan(b,0)  * t) / 255;
    return (r << 16) | (g << 8) | bl;
}

void gfx_vgradient(surface_t *s, int x, int y, int w, int h, rgb_t top, rgb_t bot)
{
    int y0 = y, h0 = h;
    for (int j = 0; j < h0; j++) {
        uint8_t t = (uint8_t)(h0 > 1 ? j * 255 / (h0 - 1) : 0);
        gfx_rect(s, x, y0 + j, w, 1, gfx_mix(top, bot, t));
    }
}

void gfx_rect_a(surface_t *s, int x, int y, int w, int h, rgb_t c, uint8_t a)
{
    clip(s, &x, &y, &w, &h);
    for (int j = 0; j < h; j++) {
        rgb_t *row = s->px + (size_t)(y + j) * s->w + x;
        for (int i = 0; i < w; i++) row[i] = gfx_mix(row[i], c, a);
    }
}

void gfx_scanlines(surface_t *s, uint8_t strength)
{
    for (int y = 1; y < s->h; y += 2) {
        rgb_t *row = s->px + (size_t)y * s->w;
        for (int x = 0; x < s->w; x++) row[x] = gfx_mix(row[x], 0x000000, strength);
    }
}

rgb_t gfx_hex(const char *hex)
{
    if (!hex || *hex != '#') return 0;
    hex++;
    unsigned v = 0;
    int len = 0;
    for (const char *p = hex; *p && len < 6; p++, len++) {
        char ch = *p;
        int d = (ch >= '0' && ch <= '9') ? ch - '0'
              : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10
              : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : 0;
        v = (v << 4) | d;
    }
    if (len == 3) {  /* #RGB -> #RRGGBB */
        unsigned r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
        return (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
    }
    return v & 0xFFFFFF;
}

/* --------------------------- text (bitmap 8x8) ------------------------- */

int gfx_text_init(const char *font_path) { (void)font_path; return 0; }
void gfx_text_shutdown(void) {}

/* pixel size px -> integer scale (glyph cell is 8px). Minimum 1. */
static inline int scale_for(int px) { int sc = px / 8; return sc < 1 ? 1 : sc; }

int gfx_text_width(int px, const char *str)
{
    int sc = scale_for(px);
    return (int)strlen(str) * 8 * sc;
}

int gfx_text(surface_t *s, int x, int y, int px, rgb_t c, const char *str)
{
    int sc = scale_for(px);
    int x0 = x;
    for (; *str; str++) {
        unsigned ch = (unsigned char)*str;
        if (ch < 0x20 || ch > 0x7E) ch = '?';
        const uint8_t *g = FONT8X8[ch - 0x20];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = g[row];
            if (!bits) continue;
            for (int col = 0; col < 8; col++)
                if (bits & (1u << col))
                    gfx_rect(s, x + col * sc, y + row * sc, sc, sc, c);
        }
        x += 8 * sc;
    }
    return x - x0;
}

void gfx_text_aligned(surface_t *s, int x, int y, int w, int px, rgb_t c,
                      text_align_t align, const char *str)
{
    int tw = gfx_text_width(px, str);
    int tx = x;
    if (align == ALIGN_CENTER) tx = x + (w - tw) / 2;
    else if (align == ALIGN_RIGHT) tx = x + w - tw;
    gfx_text(s, tx, y, px, c, str);
}
