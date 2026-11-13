/* palm.h's drawing calls on Palm OS 5: the screen in 16-bit colour, every call in its own (native) pixels, the
 * game's fonts kept locked from the program's 'Glyf' resources for text.c, and a batch that changes much of the
 * screen drawn behind a locked screen, so it appears at once. */
#include <PalmOS.h>
#include "palm.h"

#define FONT_RES 1000                   /* 'Glyf' 1000 + n is font n (synack.rcp) */

static UInt32 old_depth;
static UInt16 old_coords;
static unsigned char depth_set, level, locked;
static MemHandle font_h[PALM_FONTS];
static const unsigned char *font_p[PALM_FONTS];

void gfx_init(void)
{
    UInt32 depth = 16;
    unsigned char i;
    MemHandle h;

    if (WinScreenMode(winScreenModeGet, NULL, NULL, &old_depth, NULL) == errNone &&
        WinScreenMode(winScreenModeSet, NULL, NULL, &depth, NULL) == errNone)
        depth_set = 1;
    for (i = 0; i < PALM_FONTS; ++i) {
        h = DmGetResource('Glyf', FONT_RES + i);
        if (h) {
            font_h[i] = h;
            font_p[i] = (const unsigned char *)MemHandleLock(h);
        }
    }
}

void gfx_done(void)
{
    unsigned char i;
    for (i = 0; i < PALM_FONTS; ++i)
        if (font_h[i]) {
            MemHandleUnlock(font_h[i]);
            DmReleaseResource(font_h[i]);
            font_h[i] = 0;
            font_p[i] = 0;
        }
    if (depth_set)
        WinScreenMode(winScreenModeSet, NULL, NULL, &old_depth, NULL);
    depth_set = 0;
}

void gfx_begin(unsigned char whole)
{
    if (level++)
        return;
    WinSetDrawWindow(WinGetDisplayWindow());
    old_coords = WinSetCoordinateSystem(kCoordinatesNative);
    locked = whole && WinScreenLock(winLockCopy) != NULL;
}

void gfx_end(void)
{
    if (!level || --level)
        return;
    if (locked)
        WinScreenUnlock();
    locked = 0;
    WinSetCoordinateSystem(old_coords);
}

static void color(rgb_t c)
{
    RGBColorType rgb;
    rgb.index = 0;
    rgb.r = (UInt8)(c >> 16);
    rgb.g = (UInt8)(c >> 8);
    rgb.b = (UInt8)c;
    WinSetForeColorRGB(&rgb, NULL);
}

void gfx_rect(short x, short y, short w, short h, rgb_t c)
{
    RectangleType r;
    if (w <= 0 || h <= 0)
        return;
    color(c);
    RctSetRectangle(&r, x, y, w, h);
    WinDrawRectangle(&r, 0);
}

void gfx_frame(short x, short y, short w, short h, rgb_t c)
{
    gfx_rect(x, y, w, 1, c);
    gfx_rect(x, y + h - 1, w, 1, c);
    gfx_rect(x, y + 1, 1, h - 2, c);
    gfx_rect(x + w - 1, y + 1, 1, h - 2, c);
}

void gfx_line(short x0, short y0, short x1, short y1, rgb_t c)
{
    color(c);
    WinDrawLine(x0, y0, x1, y1);
}

void gfx_pixel(short x, short y, rgb_t c)
{
    color(c);
    WinDrawPixel(x, y);
}

const unsigned char *gfx_font(unsigned char font)
{
    return font < PALM_FONTS ? font_p[font] : 0;
}

void gfx_color(rgb_t c)
{
    color(c);
}

void gfx_fill(short x, short y, short w, short h)
{
    RectangleType r;
    RctSetRectangle(&r, x, y, w, h);
    WinDrawRectangle(&r, 0);
}

void gfx_clip(short x, short y, short w, short h)
{
    RectangleType r;
    RctSetRectangle(&r, x, y, w, h);
    WinSetClip(&r);
}

void gfx_unclip(void)
{
    WinResetClip();
}
