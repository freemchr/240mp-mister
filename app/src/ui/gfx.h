/*
 * gfx.h — software rendering surface + primitives for 240-MP: MiSTer Edition.
 *
 * Everything renders into an in-RAM 32bpp XRGB8888 surface; the app blits the
 * finished surface to the fabric framebuffer (0x22000000) once per frame.
 * Colors are 0x00RRGGBB (matching the core's BGR framebuffer format, which the
 * hardware swizzles — validated in Phase 0/1).
 *
 * Text uses freetype (system libfreetype on the MiSTer image) when available,
 * falling back to the built-in 8x8 bitmap font.
 *
 * GPL-3.0.
 */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include <stddef.h>

typedef uint32_t rgb_t;   /* 0x00RRGGBB */

typedef struct {
    rgb_t  *px;
    int     w, h;
} surface_t;

/* --- surface lifecycle --- */
surface_t *gfx_surface_create(int w, int h);
void       gfx_surface_free(surface_t *s);

/* --- primitives --- */
void gfx_clear(surface_t *s, rgb_t c);
void gfx_rect(surface_t *s, int x, int y, int w, int h, rgb_t c);
void gfx_rect_outline(surface_t *s, int x, int y, int w, int h, int thick, rgb_t c);
/* vertical gradient top->bottom */
void gfx_vgradient(surface_t *s, int x, int y, int w, int h, rgb_t top, rgb_t bot);
/* alpha in 0..255; blends c over existing pixels */
void gfx_rect_a(surface_t *s, int x, int y, int w, int h, rgb_t c, uint8_t a);
/* darken alternate scanlines for a CRT feel (subtle) */
void gfx_scanlines(surface_t *s, uint8_t strength);

/* --- color helpers --- */
rgb_t gfx_hex(const char *hex);            /* "#RRGGBB" or "#RGB" -> rgb_t */
rgb_t gfx_mix(rgb_t a, rgb_t b, uint8_t t);/* t=0 -> a, 255 -> b */

/* --- text (freetype if built with it, else 8x8 bitmap) --- */
typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } text_align_t;

int  gfx_text_init(const char *font_path);   /* returns 0 ok; ok to ignore */
void gfx_text_shutdown(void);
/* draw UTF-8 (ASCII subset) at pixel size px; returns advance width */
int  gfx_text(surface_t *s, int x, int y, int px, rgb_t c, const char *str);
int  gfx_text_width(int px, const char *str);
void gfx_text_aligned(surface_t *s, int x, int y, int w, int px, rgb_t c,
                      text_align_t align, const char *str);

#endif /* GFX_H */
