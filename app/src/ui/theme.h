/*
 * theme.h — 240-MP color schemes, mirrored from the parent project's
 * Main.qml so the MiSTer edition looks identical. Colors are 0xRRGGBB.
 */
#ifndef THEME_H
#define THEME_H
#include "gfx.h"

typedef struct {
    const char *name;
    rgb_t primary;    /* main text / focused */
    rgb_t secondary;  /* subtext */
    rgb_t tertiary;   /* dim / borders */
    rgb_t surface;    /* background */
    rgb_t accent;     /* highlight / selection */
} theme_t;

/* Order matches the parent's themes object. "Video 1" is the default. */
static const theme_t THEMES[] = {
    { "Video 1",   0xFFFFFF, 0xC2BFE4, 0x8480C9, 0x0A0094, 0xAECFFF },
    { "Late Night",0xFFFFFF, 0xA1A1A1, 0x444444, 0x000000, 0xFFD900 },
    { "Synthwave", 0xFFFFFF, 0xD48BFF, 0x7836B5, 0x12012B, 0x00E5FF },
    { "Terminal",  0x4AF626, 0x32A81B, 0x1A590E, 0x000000, 0x4AF626 },
    { "T-120",     0x000000, 0x818181, 0xDF9C27, 0xFAF5E8, 0xEE442F },
    { "Amber",     0xFFB000, 0xB37B00, 0xB37B00, 0x000000, 0xFFEE11 },
    { "Kinescope", 0xFFFFFF, 0x9E9E9E, 0x424242, 0x121212, 0xFFFFFF },
};
#define THEME_COUNT ((int)(sizeof(THEMES)/sizeof(THEMES[0])))

#endif /* THEME_H */
