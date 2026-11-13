/* The instructions: help/palm/screen's pages (build/palm/help_tab.h), over the whole screen. Text in braces is drawn
 * in reverse; a single syndicate letter in braces is that syndicate's chip. A tap on the right half turns to the next
 * page, the left half back, the bar at the bottom leaves (display.c's palm_tap). */
#include <string.h>
#include "../config.h"
#include "../platform.h"
#include "../common/game.h"
#include "palm.h"
#ifdef PALM_HOST
#include "help_tab.h"                   /* the PC preview: the pages compiled in */
#define PAGE(n) help_text[(n) - 1]
#define DONE(n)
#else
#include <PalmOS.h>                     /* the Palm: 'tSTR' 2001.. (build/palm/help.rcp), out of the code */
#define HELP_PAGES 10
static MemHandle page_h;
static const char *PAGE(unsigned char n)
{
    page_h = DmGetResource('tSTR', 2000 + n);
    return page_h ? (const char *)MemHandleLock(page_h) : "";
}
#define DONE(n) do { if (page_h) { MemHandleUnlock(page_h); DmReleaseResource(page_h); } page_h = 0; } while (0)
#endif

#define LEFT  20
#define PITCH 12

unsigned char help_pages(void)
{
    return HELP_PAGES;
}

static unsigned char syn_index(char c)
{
    unsigned char j;
    for (j = 0; j < NSYN; ++j)
        if (syn_letter[j] == c)
            return j;
    return NONE;
}

static void line(short y, const char *s, unsigned char n, unsigned char first)
{
    short x = LEFT;
    unsigned char i = 0, rev = 0, k, j;
    while (i < n) {
        if (s[i] == '{' || s[i] == '}') {
            rev = s[i] == '{';
            ++i;
            continue;
        }
        if (rev && s[i - 1] == '{' && i + 1 < n && s[i + 1] == '}' && (j = syn_index(s[i])) != NONE) {
            gfx_vgrad(x, y, F_SMALL_W, PITCH - 1, mix(syn_rgb[j], C_WHITE, 4), syn_dim[j]);
            gfx_text(x, y, s + i, 1, F_SMALL, C_BG0);
            x += F_SMALL_W;
            ++i;
            continue;
        }
        for (k = i; k < n && s[k] != '{' && s[k] != '}'; ++k)
            ;
        if (rev) {
            gfx_rect(x, y, (k - i) * F_SMALL_W, PITCH - 1, first ? C_CYAN : mix(C_CYAN, C_BG0, 6));
            gfx_text(x, y, s + i, (unsigned char)(k - i), F_SMALL, first ? C_BG0 : C_WHITE);
        } else
            gfx_text(x, y, s + i, (unsigned char)(k - i), F_SMALL, C_TEXT);
        x += (k - i) * F_SMALL_W;
        i = k;
    }
}

void help_draw(unsigned char page)
{
    const char *p = PAGE(page), *e;
    short y = 4;
    unsigned char first = 1;
    gfx_begin(1);
    gfx_vgrad(0, 0, SCREEN_W, SCREEN_H - 22, C_BG1, C_BG0);
    while (*p) {
        for (e = p; *e && *e != '\n'; ++e)
            ;
        line(y, p, (unsigned char)(e - p), first);
        if (first)
            y += 4;
        first = 0;
        y += PITCH;
        p = *e ? e + 1 : e;
    }
    gfx_vgrad(0, SCREEN_H - 22, SCREEN_W, 22, C_BG2, C_BG0);
    gfx_rect(0, SCREEN_H - 22, SCREEN_W, 1, C_CYAN);
    gfx_text(8, SCREEN_H - 18, "< PREV", 6, F_SMALL, page > 1 ? C_CYAN : C_EDGE);
    gfx_text(gfx_center(0, SCREEN_W, "EXIT", 4, F_SMALL), SCREEN_H - 18, "EXIT", 4, F_SMALL, C_AMBER);
    gfx_text(SCREEN_W - 8 - gfx_width("NEXT >", 6, F_SMALL), SCREEN_H - 18, "NEXT >", 6, F_SMALL,
             page < HELP_PAGES ? C_CYAN : C_EDGE);
    gfx_end();
    DONE(page);
}
