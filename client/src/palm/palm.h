/* The Palm client (Palm OS 5, a double-density colour screen such as the Sony Clie TG50's): what its files share.
 *
 * Everything is drawn in the screen's own pixels, 320 x 320, through gfx_*: gfx_palm.c on the Palm, and
 * test/palm_host.c on a PC, which records the calls for tools/palm_render.py to turn into pictures. display.c, the
 * whole look of the game, is the same on both. Colours are 0xRRGGBB. */
#ifndef PALM_H
#define PALM_H

#include "palm_fonts.h"                 /* generated: build/palm/fonts/ (tools/palm_fonts.py) */

#define SCREEN_W 320
#define SCREEN_H 320

typedef unsigned long rgb_t;

/* ---- the palette: a night city, a neon colour for each syndicate (the Amiga client's, src/amiga/palette.h) */
#define C_BG0     0x05060FUL            /* the deepest background */
#define C_BG1     0x0A1424UL            /* panels */
#define C_BG2     0x13233AUL            /* bars, raised things */
#define C_EDGE    0x2E4466UL
#define C_EDGEHI  0x5C8FB0UL
#define C_TEXT    0xD0DCEEUL
#define C_DIM     0x6F7E99UL
#define C_WHITE   0xFFFFFFUL
#define C_CYAN    0x33DDFFUL
#define C_AMBER   0xFFB822UL
#define C_RED     0xFF3344UL
#define C_GREEN   0x33FF88UL
#define C_MAG     0xFF33CCUL
#define C_STEEL   0x99AABBUL
#define C_STEEL2  0x556677UL
#define C_TRACE   0x123A52UL            /* the grid's circuit traces */

extern const rgb_t syn_rgb[7];          /* each syndicate's colour, in id order (GRHIBLO) */
extern const rgb_t syn_dim[7];          /* the same, dark */
rgb_t mix(rgb_t a, rgb_t b, unsigned char t);   /* a to b, t of 16 */

/* ---- drawing (gfx_palm.c; test/palm_host.c) */
void gfx_init(void);                    /* fonts, colour depth, native coordinates */
void gfx_done(void);                    /* the screen as it was */
void gfx_begin(unsigned char whole);    /* around every batch of drawing; whole = a big change, shown at once */
void gfx_end(void);
void gfx_rect(short x, short y, short w, short h, rgb_t c);
void gfx_frame(short x, short y, short w, short h, rgb_t c);   /* a 1-pixel outline, inside the rectangle */
void gfx_line(short x0, short y0, short x1, short y1, rgb_t c);
void gfx_pixel(short x, short y, rgb_t c);
void gfx_color(rgb_t c);                /* the colour gfx_fill uses */
void gfx_fill(short x, short y, short w, short h);
const unsigned char *gfx_font(unsigned char font);  /* the font's glyphs (text.c reads them), or 0 */
void gfx_text(short x, short y, const char *s, unsigned char n, unsigned char font, rgb_t c);  /* text.c; y = top */
short gfx_width(const char *s, unsigned char n, unsigned char font);   /* display.c, from palm_fonts.h */
void gfx_clip(short x, short y, short w, short h);   /* drawing outside is dropped; gfx_unclip ends it */
void gfx_unclip(void);

/* ---- shapes made of those (display.c) */
void gfx_vgrad(short x, short y, short w, short h, rgb_t top, rgb_t bottom);
void gfx_bevel(short x, short y, short w, short h, rgb_t fill, rgb_t hi, rgb_t lo);
void gfx_glow_text(short x, short y, const char *s, unsigned char n, unsigned char font, rgb_t c, rgb_t glow);
short gfx_center(short x, short w, const char *s, unsigned char n, unsigned char font);

/* ---- input: a tap or a key from the Palm, turned into the game's keys (input.c) */
#define K_TAP_MAX 24
void key_push(unsigned char k);         /* queued for in_key() */
void keys_push(const char *s);          /* a string of them */
unsigned char palm_tap(short x, short y);       /* display.c: what a tap at (x, y) means; queues its keys */
unsigned char palm_handle_dialog(void); /* input.c: the handle typed in a Palm dialog, as keys; 0 = cancelled */

/* ---- sound (sound.c) */
extern unsigned char sound_on;
void snd_service(void);                 /* the next note of a chord, when it is due */
void snd_click(void);                   /* a tap on something that takes it */
void snd_off(void);

/* ---- the rest of the program */
void display_repaint(void);             /* display.c: everything again, after the Palm drew over it */
void palm_save_handle(void);            /* input.c: the handle in the hello, kept for next time */
unsigned char palm_now(void);           /* ticks(), for the display's animation */
void help_draw(unsigned char page);     /* help.c */
unsigned char help_pages(void);
extern unsigned char palm_region;      /* the region picked (index into the region table) */

#endif
