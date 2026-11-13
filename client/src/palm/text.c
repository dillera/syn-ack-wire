/* Text, drawn by the game itself: each glyph of the game's fonts is a few filled rectangles (tools/palm_fonts.py,
 * build/palm/fonts/glyphs<N>.bin), drawn in the text's colour in the screen's own pixels. The Palm's font manager
 * is not used (on the Clie TG50 it drew custom font families in its system font), so the Palm and the PC preview
 * draw exactly the same letters. gfx_font() gives a font's data: a resource on the Palm, a file on the PC. */
#include "palm.h"

void gfx_text(short x, short y, const char *s, unsigned char n, unsigned char font, rgb_t c)
{
    const unsigned char *f = gfx_font(font), *g;
    unsigned char k, ch, r, nr;
    unsigned off;
    if (!f || !n)
        return;
    gfx_color(c);
    for (k = 0; k < n; ++k) {
        ch = (unsigned char)s[k];
        if (ch < f[2] || ch >= f[2] + f[3])
            continue;
        off = (unsigned)f[4 + 2 * (ch - f[2])] << 8 | f[5 + 2 * (ch - f[2])];
        g = f + off;
        nr = g[1];
        for (r = 0; r < nr; ++r)
            gfx_fill(x + g[2 + 4 * r], y + g[3 + 4 * r], g[4 + 4 * r], g[5 + 4 * r]);
        x += g[0];
    }
}
