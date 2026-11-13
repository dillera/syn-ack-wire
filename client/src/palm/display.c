/* The Palm's display: the whole game on a 320 x 320 colour screen, drawn with gfx_* in the screen's own pixels, and
 * what a tap on each part of it means (palm_tap). The same file draws the PC preview (test/palm_host.c).
 *
 * The display_* calls only note what changed; display_idle paints it once per pass of the game loop, after every
 * frame that came has been applied, and runs the animations: the last move's ring, nodes that just changed, the
 * clock, the picks. The names and the questions come from the theme, as on the other clients.
 *
 *    0 +---------------------------------------------+ header: name, region and link, the tool buttons
 *   23 | Grid, 12 x 9 nodes of 19 px  | hives (7)    |
 *      |                              |--------------|
 *  144 |                              | kit (6)      |
 *  197 | the netrunners: a row each, account and crypto |
 *  265 | the log, two lines                          |
 *  293 | the clock                                   |
 *  297 | the prompt, and its buttons                 |
 *  320 +---------------------------------------------+
 */
#include <string.h>
#include "../config.h"
#include "../platform.h"
#include "../display.h"
#include "../common/proto.h"
#include "../common/proto_tab.h"
#include "../common/game.h"
#define PALM_FONT_WIDTHS                /* palm_fonts.h's glyph widths: here, once */
#include "palm.h"
#include "regions_tab.h"                /* generated: tools/gen_regions.py h */

/* ---------------------------------------------------------------- layout */
#define HDR_H     21
#define CELL      19
#define GX        2
#define GY        23
#define GW        (RULE_COLS * CELL)
#define GH        (RULE_ROWS * CELL)
#define PX        233                   /* the hives and the kit, right of the Grid */
#define PW        (SCREEN_W - PX - 1)
#define HIVE_Y    GY
#define HIVE_H    17
#define KIT_Y     (HIVE_Y + NSYN * HIVE_H + 2)
#define CHIP_W    28
#define CHIP_H    23
#define SEAT_Y    197
#define SEAT_H    11
#define COIN_X    128                   /* the first syndicate's column in a seat row */
#define COIN_W    27
#define LOG_END   291                   /* the log fills from below the netrunners down to here */
#define LOG_H     12
#define LOG_MAX   4
#define CLOCK_Y   293
#define PROMPT_Y  297
#define PROMPT_H  (SCREEN_H - PROMPT_Y)
#define TOOL_W    20                    /* the header's buttons, from the right: exit, sound, hints, help */

#define FLASH     8                     /* steps a changed node glows for */
#define BLINK_SECS 4                    /* the last move's ring blinks this long */

const rgb_t syn_rgb[7] = { 0x44D4E8UL, 0xE0974FUL, 0xB46CFFUL, 0x9CD43CUL, 0xFF4560UL, 0x2EE59AUL, 0xFFD93AUL };
const rgb_t syn_dim[7] = { 0x14505CUL, 0x5A3418UL, 0x3E1F6EUL, 0x34521AUL, 0x6A1424UL, 0x0E5838UL, 0x6A5410UL };
static const char *const syn_short[NSYN] = SYN_SHORTS;

static unsigned char field;             /* the playfield is up (else the title) */
unsigned char helping_now;
static unsigned char help_pg, picking_region;
static unsigned char shown[NODES], flash[NODES], flashing, shown_ok;
static unsigned char dirty_node[(NODES + 7) / 8], dirty_seats, dirty_hives, dirty_log, dirty_prompt, dirty_kit;
static unsigned char blink_node = NONE, blink_t, lit, anim_t, pulse;
static unsigned char over, over_t;      /* the game-over banner is up, and since when */
static unsigned char clock_secs, msg_up;
static unsigned char seat_rows = 6;    /* the rows the netrunners take: the log has the rest */
static short log_y;                     /* where the log starts */

/* ---------------------------------------------------------------- small helpers */

rgb_t mix(rgb_t a, rgb_t b, unsigned char t)
{
    unsigned long r, g, bl;
    if (t >= 16)
        return b;
    r = (((a >> 16) & 255) * (16 - t) + ((b >> 16) & 255) * t) / 16;
    g = (((a >> 8) & 255) * (16 - t) + ((b >> 8) & 255) * t) / 16;
    bl = ((a & 255) * (16 - t) + (b & 255) * t) / 16;
    return (r << 16) | (g << 8) | bl;
}

short gfx_width(const char *s, unsigned char n, unsigned char font)
{
    short w = 0;
    unsigned char c;
    while (n--) {
        c = (unsigned char)*s++;
        if (c >= 32 && c <= 126)
            w += palm_font_w[font][c - 32];
    }
    return w;
}

void gfx_vgrad(short x, short y, short w, short h, rgb_t top, rgb_t bottom)
{
    short i, band = h > 16 ? (h + 15) / 16 : 1;
    for (i = 0; i < h; i += band)
        gfx_rect(x, y + i, w, i + band > h ? h - i : band, mix(top, bottom, (unsigned char)(i * 16 / (h > 1 ? h : 1))));
}

void gfx_bevel(short x, short y, short w, short h, rgb_t fill, rgb_t hi, rgb_t lo)
{
    gfx_rect(x, y, w, h, fill);
    gfx_rect(x, y, w, 1, hi);
    gfx_rect(x, y, 1, h, hi);
    gfx_rect(x, y + h - 1, w, 1, lo);
    gfx_rect(x + w - 1, y, 1, h, lo);
}

void gfx_glow_text(short x, short y, const char *s, unsigned char n, unsigned char font, rgb_t c, rgb_t glow)
{
    gfx_text(x - 1, y, s, n, font, glow);
    gfx_text(x + 1, y, s, n, font, glow);
    gfx_text(x, y - 1, s, n, font, glow);
    gfx_text(x, y + 1, s, n, font, glow);
    gfx_text(x, y, s, n, font, c);
}

short gfx_center(short x, short w, const char *s, unsigned char n, unsigned char font)
{
    return x + (w - gfx_width(s, n, font)) / 2;
}

static void say(short x, short y, const char *s, unsigned char font, rgb_t c)
{
    gfx_text(x, y, s, (unsigned char)strlen(s), font, c);
}

/* value (units of 100 if money) as text; returns its length */
static char num[12];
static unsigned char number(unsigned long v, unsigned char money)
{
    char t[12];
    unsigned char n = 0, k = 0;
    if (money && v)
        v *= 100;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    if (money)
        num[k++] = '\\';                /* the font draws it as a yen sign */
    while (n)
        num[k++] = t[--n];
    num[k] = 0;
    return k;
}

static unsigned char trimmed(unsigned char seat, char *out)
{
    unsigned char n = 8;
    memcpy(out, seat_name[seat], 8);
    while (n && out[n - 1] == ' ')
        --n;
    out[n] = 0;
    return n;
}

static void mark_node(unsigned char node)
{
    dirty_node[node >> 3] |= (unsigned char)(1 << (node & 7));
}

/* ---------------------------------------------------------------- buttons
 * What the screen offers a tap on, beyond the Grid, the hives and the kit: rebuilt as each part is drawn. */
#define ACT_KEY   0                     /* push keys[] */
#define ACT_INC   1                     /* the number being typed, one more */
#define ACT_DEC   2
#define ACT_MAX   3
#define ACT_HANDLE 4                    /* the title: the handle, in a Palm dialog */
#define ACT_SOUND 5
struct button { short x, y, w, h; unsigned char act; char keys[3]; };
#define NBUTTONS 12
static struct button btn[NBUTTONS];
static unsigned char nbtn, prompt_btn0;   /* the prompt's buttons start here */

static void add_button(short x, short y, short w, short h, unsigned char act, const char *keys)
{
    struct button *b;
    if (nbtn >= NBUTTONS)
        return;
    b = &btn[nbtn++];
    b->x = x; b->y = y; b->w = w; b->h = h; b->act = act;
    strncpy(b->keys, keys, sizeof b->keys);
}

/* a button: a bevelled plate with its label in the middle; `hot` lights it */
static void plate(short x, short y, short w, short h, const char *label, unsigned char font, rgb_t c, unsigned char hot)
{
    unsigned char n = (unsigned char)strlen(label);
    rgb_t top = hot ? mix(c, C_WHITE, 4) : C_BG2, bottom = hot ? mix(c, C_BG0, 7) : C_BG1;
    gfx_vgrad(x, y, w, h, top, bottom);
    gfx_frame(x, y, w, h, hot ? mix(c, C_WHITE, 6) : C_EDGEHI);
    gfx_text(gfx_center(x, w, label, n, font), y + (h - (font == F_TEXT ? F_TEXT_H : font == F_SMALL ? F_SMALL_H
             : font == F_HEAD ? F_HEAD_H : F_TINY_H)) / 2 + 1, label, n, font, hot ? C_BG0 : c);
}

/* ---------------------------------------------------------------- the Grid */

/* the key ('1'-'6') of the exploit in my kit that can run on `node` now, or 0 */
static unsigned char target(unsigned char node)
{
    unsigned char n;
    if (!hints)
        return 0;
    for (n = 0; n < RULE_KITSIZE; ++n)
        if (kit[n] == node)             /* bit 7 set = can't run: never equal */
            return (unsigned char)('1' + n);
    return 0;
}

static unsigned char same(unsigned char node, signed char dc, signed char dr)
{
    unsigned char col = node % RULE_COLS, row = node / RULE_COLS;
    if ((dc < 0 && !col) || (dc > 0 && col == RULE_COLS - 1) || (dr < 0 && !row) || (dr > 0 && row == RULE_ROWS - 1))
        return 0;
    return board[node + dc + dr * RULE_COLS] == board[node];
}

static void paint_node(unsigned char node)
{
    short x = GX + (node % RULE_COLS) * CELL, y = GY + (node / RULE_COLS) * CELL, x0, y0, x1, y1;
    unsigned char v = board[node], k, f = flash[node];
    char ch;

    gfx_rect(x, y, CELL, CELL, C_BG0);
    gfx_rect(x, y + CELL / 2, CELL, 1, C_TRACE);
    gfx_rect(x + CELL / 2, y, 1, CELL, C_TRACE);
    if (v >= 2 && v < 2 + NSYN) {       /* a hive: its tiles run together, a gap where it meets something else */
        unsigned char s = v - 2, ice = hive_size[s] >= RULE_SAFESIZE;
        rgb_t c = ice ? C_STEEL : syn_rgb[s], d = ice ? C_STEEL2 : syn_dim[s];
        x0 = same(node, -1, 0) ? x : x + 1;
        y0 = same(node, 0, -1) ? y : y + 1;
        x1 = same(node, 1, 0) ? x + CELL : x + CELL - 1;
        y1 = same(node, 0, 1) ? y + CELL : y + CELL - 1;
        gfx_vgrad(x0, y0, x1 - x0, y1 - y0, mix(c, C_WHITE, 3), mix(c, d, 9));
        if (!same(node, 0, -1))
            gfx_rect(x0, y0, x1 - x0, 1, mix(c, C_WHITE, 9));
        if (ice) {                      /* Black ICE: frost lines, the syndicate's letter in its colour */
            for (k = 3; k < CELL * 2; k += 6)
                gfx_line(x0 + (k < CELL ? 0 : k - CELL + 1), y0 + (k < CELL ? k : CELL - 2),
                         x0 + (k < CELL ? k : CELL - 2), y0 + (k < CELL ? 0 : k - CELL + 1), mix(C_WHITE, c, 8));
            ch = syn_letter[s];
            gfx_text(gfx_center(x, CELL, &ch, 1, F_TINY) + 1, y + 4, &ch, 1, F_TINY, C_BG0);
            gfx_text(gfx_center(x, CELL, &ch, 1, F_TINY), y + 3, &ch, 1, F_TINY, syn_rgb[s]);
        } else {
            ch = syn_letter[s];
            gfx_text(gfx_center(x, CELL, &ch, 1, F_TINY), y + 4, &ch, 1, F_TINY, mix(d, C_BG0, 6));
        }
    } else if (v == CELL_SPIKE) {       /* a spike: a chip on the board, pins out to the traces */
        gfx_rect(x + 3, y + CELL / 2, CELL - 6, 1, C_EDGEHI);
        gfx_rect(x + CELL / 2, y + 3, 1, CELL - 6, C_EDGEHI);
        gfx_bevel(x + 5, y + 5, CELL - 10, CELL - 10, C_TEXT, C_WHITE, C_DIM);
        gfx_rect(x + 7, y + 7, CELL - 14, CELL - 14, C_WHITE);
    } else if ((k = target(node)) != 0) {   /* where an exploit in my kit can run */
        rgb_t c = pr_kind == 'P' ? mix(C_CYAN, C_WHITE, pulse) : C_CYAN;
        gfx_rect(x + 2, y + 2, CELL - 4, CELL - 4, mix(C_CYAN, C_BG0, 13));
        gfx_frame(x + 2, y + 2, CELL - 4, CELL - 4, c);
        ch = (char)k;
        gfx_text(gfx_center(x, CELL, &ch, 1, F_SMALL), y + 2, &ch, 1, F_SMALL, c);
    } else {                            /* an empty node: a pad */
        gfx_rect(x + CELL / 2 - 2, y + CELL / 2 - 2, 5, 5, C_EDGE);
        gfx_rect(x + CELL / 2 - 1, y + CELL / 2 - 1, 3, 3, C_BG1);
    }
    if (f)                              /* just changed: a white glow, fading */
        gfx_frame(x, y, CELL, CELL, mix(C_BG0, C_WHITE, (unsigned char)(f * 2)));
    if (node == last_node && (blink_node != node || lit)) {     /* the last move: an amber ring */
        gfx_frame(x, y, CELL, CELL, C_AMBER);
        gfx_frame(x + 1, y + 1, CELL - 2, CELL - 2, mix(C_AMBER, C_WHITE, 8));
    }
    shown[node] = v;
}

void display_node(unsigned char node)
{
    if (node >= NODES)
        return;
    if (shown_ok && shown[node] != board[node]) {
        flash[node] = FLASH;
        flashing = 1;
    }
    if (node == last_node && blink_node != node) {
        blink_node = node;
        blink_t = ticks();
    }
    mark_node(node);
}

static void paint_grid(void)
{
    unsigned char i;
    gfx_rect(GX - 2, GY - 2, GW + 4, GH + 4, C_BG0);
    gfx_frame(GX - 2, GY - 2, GW + 4, GH + 4, C_EDGE);
    for (i = 0; i < NODES; ++i)
        paint_node(i);
    shown_ok = 1;
}

/* ---------------------------------------------------------------- the hives */

static unsigned char picks(void)        /* the syndicates a pick or a buy offers, as a mask */
{
    unsigned char j, m = 0;
    if (pr_kind == 'F')
        return pr[1];
    if (pr_kind == 'K')
        return pr[2];
    if (pr_kind == 'B' && !step)
        for (j = 0; j < NSYN; ++j)
            if (pr[4 + j] && pr[4 + NSYN + j])
                m |= (unsigned char)(1 << j);
    return m;
}

static void paint_hive(unsigned char j, unsigned char want)
{
    short y = HIVE_Y + j * HIVE_H;
    unsigned char n = hive_size[j], on = (unsigned char)(want >> j & 1), ice = n >= RULE_SAFESIZE, l;
    rgb_t c = syn_rgb[j], bg = on ? mix(C_BG1, c, (unsigned char)(3 + pulse / 2)) : C_BG1, pen;
    char ch = syn_letter[j];

    gfx_rect(PX, y, PW, HIVE_H - 1, bg);
    if (on)
        gfx_frame(PX, y, PW, HIVE_H - 1, mix(c, C_WHITE, pulse));
    gfx_vgrad(PX + 1, y + 1, 14, 14, mix(c, C_WHITE, 4), syn_dim[j]);
    gfx_text(gfx_center(PX + 1, 14, &ch, 1, F_TINY), y + 2, &ch, 1, F_TINY, C_BG0);
    pen = want && !on ? C_DIM : ice ? C_STEEL : n ? C_TEXT : C_DIM;
    if (pr_kind == 'B' && !step) {      /* a buy: the price and how many are left, not the name */
        if (pr[4 + j]) {
            l = number(pr[4 + j], 1);
            gfx_text(PX + 18, y + 2, num, l, F_TINY, on ? C_AMBER : C_DIM);
            l = number(pr[4 + NSYN + j], 0);
            gfx_text(PX + PW - 2 - gfx_width(num, l, F_TINY), y + 2, num, l, F_TINY, on ? C_TEXT : C_DIM);
        } else
            gfx_text(PX + 18, y + 2, "-", 1, F_TINY, C_DIM);
        return;
    }
    say(PX + 18, y + 2, syn_short[j], F_TINY, pen);
    if (n) {
        l = number(n, 0);
        gfx_text(PX + PW - 2 - gfx_width(num, l, F_SMALL), y + 1, num, l, F_SMALL, ice ? C_STEEL : C_WHITE);
    }
}

static unsigned char ice_mask;          /* the syndicates last drawn as Black ICE: their nodes change look */

static void paint_hives(void)
{
    unsigned char j, i, want = picks(), now = 0;
    for (j = 0; j < NSYN; ++j) {
        paint_hive(j, want);
        if (hive_size[j] >= RULE_SAFESIZE)
            now |= (unsigned char)(1 << j);
    }
    if (now != ice_mask)
        for (i = 0; i < NODES; ++i)
            if (board[i] >= 2 && ((now ^ ice_mask) >> (board[i] - 2) & 1))
                mark_node(i);
    ice_mask = now;
}

void display_hives(void)
{
    dirty_hives = 1;
}

/* ---------------------------------------------------------------- the kit */

static void paint_kit(void)
{
    unsigned char j, v, play = pr_kind == 'P';
    short x, y;
    char b[2];
    gfx_rect(PX, KIT_Y, PW, 2 * (CHIP_H + 1) + 1, C_BG0);
    for (j = 0; j < RULE_KITSIZE; ++j) {
        x = PX + (j % 3) * (CHIP_W + 1);
        y = KIT_Y + 1 + (j / 3) * (CHIP_H + 1);
        v = kit[j];
        gfx_bevel(x, y, CHIP_W, CHIP_H, C_BG2, play && v != NONE && !(v & 0x80) ? mix(C_CYAN, C_WHITE, pulse) : C_EDGEHI,
                  C_BG0);
        b[0] = (char)('1' + j);
        gfx_text(x + 3, y + 1, b, 1, F_SMALL, v == NONE || (v & 0x80) ? C_DIM : C_CYAN);
        if (v == NONE) {
            gfx_text(x + 12, y + 9, "--", 2, F_TINY, C_DIM);
        } else {
            b[0] = (char)('A' + (v & 0x7F) % RULE_COLS);
            b[1] = (char)('0' + (v & 0x7F) / RULE_COLS);
            gfx_text(x + 13, y + 9, b, 2, F_TINY, v & 0x80 ? C_DIM : C_WHITE);
            if (v & 0x80)               /* can't run now: struck through */
                gfx_rect(x + 3, y + CHIP_H / 2, CHIP_W - 6, 1, C_RED);
        }
    }
}

/* ---------------------------------------------------------------- the netrunners */

static void paint_seats(void)
{
    unsigned char s, j, n, l, row = 0;
    short y;
    char nm[9], mk;
    rgb_t bg;

    for (s = 0; s < SEATS; ++s)
        row += seat_kind[s] != SEAT_EMPTY;
    if (row != seat_rows) {             /* fewer rows: the log has more room */
        seat_rows = row;
        dirty_log = 1;
    }
    row = 0;
    gfx_rect(0, SEAT_Y - 1, SCREEN_W, seat_rows * SEAT_H + 1, C_BG0);
    for (j = 0; j < NSYN; ++j)          /* a colour tick over each syndicate's column */
        gfx_rect(COIN_X + j * COIN_W + 2, SEAT_Y - 1, 12, 1, syn_rgb[j]);
    for (s = 0; s < SEATS; ++s) {
        if (seat_kind[s] == SEAT_EMPTY)
            continue;
        y = SEAT_Y + row * SEAT_H;
        bg = s == turn ? mix(C_BG0, C_AMBER, 4) : row & 1 ? C_BG1 : C_BG0;
        gfx_rect(0, y, SCREEN_W, SEAT_H, bg);
        if (s == turn)
            gfx_rect(0, y, 3, SEAT_H, C_AMBER);
        mk = s == me ? '>' : seat_kind[s] == SEAT_BOT ? '*' : seat_kind[s] == SEAT_AWAY ? '?' : ' ';
        gfx_text(5, y, &mk, 1, F_TINY, s == me ? C_CYAN : seat_kind[s] == SEAT_AWAY ? C_AMBER : C_DIM);
        n = trimmed(s, nm);
        gfx_text(13, y, nm, n, F_TINY, s == turn ? C_WHITE : s == me ? C_CYAN : C_TEXT);
        l = number(cash[s], 1);
        gfx_text(COIN_X - 8 - gfx_width(num, l, F_TINY), y, num, l, F_TINY, C_AMBER);
        for (j = 0; j < NSYN; ++j) {
            n = coins[s][j];
            if (n) {
                l = number(n, 0);
                gfx_text(COIN_X + j * COIN_W + 14 - gfx_width(num, l, F_TINY), y, num, l, F_TINY, syn_rgb[j]);
            } else
                gfx_text(COIN_X + j * COIN_W + 8, y, ".", 1, F_TINY, C_EDGE);
        }
        ++row;
    }
}

void display_seat(unsigned char seat)
{
    (void)seat;
    dirty_seats = 1;
}

/* ---------------------------------------------------------------- the log */

/* The log: the newest lines at the bottom, as many as fit under the netrunners (two to four); looking back, the
 * lines from there, and how far back. */
static void paint_log(void)
{
    const unsigned char *l;
    unsigned char rows, i;
    short y;
    log_y = SEAT_Y + seat_rows * SEAT_H + 3;
    rows = (unsigned char)((LOG_END - log_y) / LOG_H);
    if (rows > LOG_MAX)
        rows = LOG_MAX;
    gfx_rect(0, log_y - 2, SCREEN_W, LOG_END - log_y + 2, C_BG0);
    y = LOG_END - LOG_H;
    for (i = 0; i < rows; ++i, y -= LOG_H) {
        if ((l = log_line((unsigned char)(log_back + i))) == 0)
            break;
        gfx_text(4, y - 1, (const char *)l + 1, l[0], F_TINY,
                 log_back ? C_STEEL : i == 0 ? C_TEXT : i == 1 ? C_STEEL : C_DIM);
    }
    if (log_back) {
        number(log_back, 0);
        gfx_text(SCREEN_W - 30, LOG_END - LOG_H - 1, "-", 1, F_TINY, C_AMBER);
        say(SCREEN_W - 24, LOG_END - LOG_H - 1, num, F_TINY, C_AMBER);
    }
    gfx_text(SCREEN_W - 10, log_y, "^", 1, F_TINY, C_EDGEHI);  /* tap: back through the log */
}

void display_log(void)
{
    dirty_log = 1;
}

void display_log_view(void)
{
    dirty_log = 1;
}

/* ---------------------------------------------------------------- the prompt and the clock */

static void paint_clock(void)
{
    unsigned char total = pr[0] ? pr[0] : 60, shown_s = clock_secs ? clock_secs - 1 : 0;
    short w;
    rgb_t c;
    gfx_rect(0, CLOCK_Y, SCREEN_W, 3, C_BG0);
    if (!pr_kind || leaving)
        return;
    if (shown_s > total)
        shown_s = total;
    w = (short)((unsigned long)SCREEN_W * shown_s / total);
    c = shown_s > 15 ? C_GREEN : shown_s > 7 ? C_AMBER : mix(C_RED, C_WHITE, pulse);
    gfx_rect(0, CLOCK_Y, w, 3, c);
    gfx_rect(0, CLOCK_Y, w, 1, mix(c, C_WHITE, 8));
}

void display_clock(unsigned char secs)
{
    clock_secs = secs;
    if (secs == 5 && pr_kind)
        play_chord(CHORD_SPIKE);        /* five seconds left: a nudge */
}

static char sb[48];
static unsigned char sn;
static void sput(const char *s, unsigned char n)
{
    while (n-- && sn < sizeof sb - 1)
        sb[sn++] = *s++;
    sb[sn] = 0;
}
#define SLIT(s) sput(s, sizeof s - 1)

static short bx;                        /* the prompt's buttons go leftwards from here */
static void prompt_button(const char *label, unsigned char act, const char *keys, rgb_t c, unsigned char hot)
{
    short w = gfx_width(label, (unsigned char)strlen(label), F_SMALL) + 12;
    if (w < 22)
        w = 22;
    bx -= w + 2;
    plate(bx, PROMPT_Y + 2, w, PROMPT_H - 3, label, F_SMALL, c, hot);
    add_button(bx, PROMPT_Y, w + 2, PROMPT_H, act, keys);
}

static void paint_prompt(void)
{
    unsigned char mine = pr_kind || leaving, font, n;
    rgb_t pen;
    const char *ask = 0;
    char nm[9];

    nbtn = prompt_btn0;
    bx = SCREEN_W - 1;
    gfx_vgrad(0, PROMPT_Y, SCREEN_W, PROMPT_H, mine ? mix(C_BG2, C_CYAN, (unsigned char)(1 + pulse / 4)) : C_BG2, C_BG1);
    gfx_rect(0, PROMPT_Y, SCREEN_W, 1, mine ? mix(C_CYAN, C_WHITE, pulse) : C_EDGE);
    sn = 0;
    sb[0] = 0;
    if (msg_up) {                       /* a notice (dialing...) holds the line until the game speaks */
        return;
    }
    if (leaving) {
        ask = T_LOGOFF " NOW?";
        prompt_button("NO", ACT_KEY, "N", C_TEXT, 0);
        prompt_button("YES", ACT_KEY, "Y", C_RED, 1);
    } else switch (pr_kind) {
    case 0:
        if (over) {
            SLIT("GAME OVER");
        } else if (turn < SEATS && turn != me) {
            SLIT("WAITING FOR ");
            n = trimmed(turn, nm);
            sput(nm, n);
            sput("...", (unsigned char)(1 + (anim_t >> 4) % 3));
        } else if (turn == NONE) {
            SLIT("IN THE LOBBY");
        }
        break;
    case 'S':
        ask = "SEATS?";
        for (n = RULE_MAXPLAYERS; n >= RULE_MINPLAYERS; --n) {
            nm[0] = (char)('0' + n);
            nm[1] = 0;
            prompt_button(nm, ACT_KEY, nm, C_CYAN, 1);
        }
        break;
    case 'P':
        ask = "TAP A LIT NODE";
        break;
    case 'F':
        ask = "PICK A FREE SYNDICATE";
        break;
    case 'K':
        ask = pr[1] ? "TIE! ABSORB WHICH NEXT?" : "TIE! WHICH SURVIVES?";
        break;
    case 'D':
        ask = step ? T_SWAP " 2:1 " : T_CASHOUT " ";
        prompt_button("OK", ACT_KEY, "\r", C_GREEN, 1);
        prompt_button("ALL", ACT_MAX, "", C_TEXT, 0);
        prompt_button("+", ACT_INC, "", C_TEXT, 0);
        prompt_button("-", ACT_DEC, "", C_TEXT, 0);
        break;
    case 'B':
        if (step) {
            ask = T_NULLROUTE "? END IT";
            prompt_button("NO", ACT_KEY, "N", C_TEXT, 0);
            prompt_button("YES", ACT_KEY, "Y", C_RED, 1);
        } else {
            ask = "BUY ";
            prompt_button(edit_len ? "BUY" : "SKIP", ACT_KEY, "\r", C_GREEN, 1);
            if (edit_len)
                prompt_button("UNDO", ACT_KEY, "\b", C_TEXT, 0);
        }
        break;
    }
    if (complaint)
        ask = complaint;
    if (ask)
        sput(ask, (unsigned char)strlen(ask));
    pen = complaint ? C_AMBER : mine ? C_WHITE : C_TEXT;
    font = gfx_width(sb, sn, F_TEXT) + 8 <= bx ? F_TEXT : F_SMALL;
    gfx_text(5, PROMPT_Y + (font == F_TEXT ? 3 : 5), sb, sn, font, pen);
    if (complaint || leaving)
        return;
    n = (unsigned char)(5 + gfx_width(sb, sn, font) + 2);
    if (pr_kind == 'D') {               /* the number so far, and what's held */
        char t[4];
        unsigned char l = edit_len;
        memcpy(t, edit, l);
        if (!l)
            t[l++] = '0';
        gfx_text(n, PROMPT_Y + 3, t, l, F_TEXT, C_AMBER);
        l = number(pr[3], 0);
        gfx_text(n + 22, PROMPT_Y + 7, "OF", 2, F_TINY, C_DIM);
        gfx_text(n + 36, PROMPT_Y + 7, num, l, F_TINY, C_DIM);
    } else if (pr_kind == 'B' && !step) {   /* the crypto picked so far, as chips */
        unsigned char i, s;
        for (i = 0; i < edit_len; ++i)
            for (s = 0; s < NSYN; ++s)
                if (syn_letter[s] == edit[i]) {
                    gfx_vgrad(n + i * 18, PROMPT_Y + 4, 15, 15, mix(syn_rgb[s], C_WHITE, 4), syn_dim[s]);
                    gfx_text(gfx_center(n + i * 18, 15, (const char *)&edit[i], 1, F_TINY), PROMPT_Y + 5,
                             (const char *)&edit[i], 1, F_TINY, C_BG0);
                }
        if (!edit_len)
            gfx_text(n, PROMPT_Y + 7, "TAP A HIVE", 10, F_TINY, C_DIM);
    }
}

void display_prompt(void)
{
    msg_up = 0;
    dirty_prompt = dirty_kit = 1;
    if (pr_kind == 'F' || pr_kind == 'K' || pr_kind == 'B')
        dirty_hives = 1;
}

/* ---------------------------------------------------------------- the header */

static void tool_icon(unsigned char i)
{
    short x = SCREEN_W - TOOL_W * (i + 1), y = 1, cx = x + TOOL_W / 2, cy = y + 9;
    gfx_bevel(x + 1, y, TOOL_W - 2, HDR_H - 3, C_BG2, C_EDGEHI, C_BG0);
    switch (i) {
    case 0:                             /* exit: a power sign */
        gfx_frame(cx - 5, cy - 4, 11, 10, C_RED);
        gfx_rect(cx - 2, cy - 4, 5, 1, C_BG2);
        gfx_rect(cx, cy - 6, 1, 6, C_RED);
        break;
    case 1:                             /* sound: a speaker, struck out when off */
        gfx_rect(cx - 6, cy - 2, 3, 5, C_TEXT);
        gfx_line(cx - 3, cy - 2, cx, cy - 5, C_TEXT);
        gfx_line(cx - 3, cy + 2, cx, cy + 5, C_TEXT);
        gfx_rect(cx, cy - 5, 1, 11, C_TEXT);
        if (sound_on) {
            gfx_rect(cx + 3, cy - 2, 1, 5, C_CYAN);
            gfx_rect(cx + 5, cy - 4, 1, 9, C_CYAN);
        } else
            gfx_line(cx - 7, cy + 6, cx + 6, cy - 6, C_RED);
        break;
    case 2:                             /* hints: the numbers on the Grid */
        gfx_text(gfx_center(x, TOOL_W, "#", 1, F_SMALL), y + 1, "#", 1, F_SMALL, hints ? C_CYAN : C_DIM);
        break;
    case 3:
        gfx_text(gfx_center(x, TOOL_W, "?", 1, F_SMALL), y + 1, "?", 1, F_SMALL, C_AMBER);
        break;
    }
}

static void paint_link(void)
{
    short x = SCREEN_W - 4 * TOOL_W - 10, w;
    const char *r = field && region_len ? 0 : regions[palm_region].name;
    unsigned char n = r ? (unsigned char)strlen(r) : region_len;
    gfx_vgrad(140, 0, x + 10 - 140, HDR_H - 1, C_BG2, C_BG0);
    gfx_rect(x, 6, 6, 6, link_up ? C_GREEN : C_RED);
    gfx_rect(x + 1, 7, 4, 1, mix(link_up ? C_GREEN : C_RED, C_WHITE, 10));
    w = gfx_width(r ? r : (const char *)region, n, F_TINY);
    gfx_text(x - 4 - w, 4, r ? r : (const char *)region, n, F_TINY, link_up ? C_GREEN : C_DIM);
}

static void paint_header(void)
{
    unsigned char i;
    gfx_vgrad(0, 0, SCREEN_W, HDR_H - 1, C_BG2, C_BG0);
    gfx_rect(0, HDR_H - 1, SCREEN_W, 1, mix(C_MAG, C_BG0, 6));
    gfx_glow_text(5, 3, TH_TITLE, sizeof TH_TITLE - 1, F_HEAD, C_CYAN, mix(C_MAG, C_BG0, 7));
    paint_link();
    for (i = 0; i < 4; ++i)
        tool_icon(i);
}

void display_link(void)
{
    if (!field || helping_now)
        return;
    gfx_begin(0);
    paint_link();
    gfx_end();
}

/* ---------------------------------------------------------------- the whole playfield */

static void over_banner(void);

static void paint_all(void)
{
    unsigned char i;
    gfx_begin(1);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BG0);
    nbtn = 0;
    paint_header();
    prompt_btn0 = nbtn;
    paint_grid();
    gfx_rect(PX - 2, GY - 2, PW + 3, KIT_Y - GY + 1, C_BG0);
    paint_hives();
    paint_kit();
    paint_seats();
    paint_log();
    paint_clock();
    paint_prompt();
    if (over)
        over_banner();
    for (i = 0; i < sizeof dirty_node; ++i)
        dirty_node[i] = 0;
    dirty_seats = dirty_hives = dirty_log = dirty_prompt = dirty_kit = 0;
    gfx_end();
}

void display_playfield(void)
{
    unsigned char i;
    if (!field)
        blink_node = NONE;
    field = 1;
    over = 0;
    shown_ok = 0;
    for (i = 0; i < NODES; ++i)
        flash[i] = 0;
    if (!helping_now)
        paint_all();
}

void display_repaint(void)
{
    if (helping_now)
        help_draw(help_pg);
    else if (field)
        paint_all();
    else
        display_title_screen();
}

void display_message(const char *s)
{
    if (helping_now)
        return;
    gfx_begin(0);
    if (field) {
        msg_up = 1;
        nbtn = prompt_btn0;
        gfx_vgrad(0, PROMPT_Y, SCREEN_W, PROMPT_H, C_BG2, C_BG1);
        gfx_rect(0, PROMPT_Y, SCREEN_W, 1, C_AMBER);
        gfx_text(5, PROMPT_Y + 5, s, (unsigned char)strlen(s), F_SMALL, C_AMBER);
    } else {
        gfx_rect(0, 266, SCREEN_W, 13, C_BG0);
        say(gfx_center(0, SCREEN_W, s, (unsigned char)strlen(s), F_TINY), 267, s, F_TINY, C_AMBER);
    }
    gfx_end();
}

/* ---------------------------------------------------------------- game over */

static void over_banner(void)
{
    unsigned char i, n, w = 0;
    char nm[9];
    short y = GY + GH / 2 - 26;
    gfx_vgrad(GX, y, GW, 52, mix(C_BG0, C_MAG, 6), C_BG0);
    gfx_rect(GX, y, GW, 1, C_MAG);
    gfx_rect(GX, y + 51, GW, 1, C_MAG);
    gfx_glow_text(gfx_center(GX, GW, TH_ENDSHOUT, sizeof TH_ENDSHOUT - 1, F_BIG), y + 5, TH_ENDSHOUT,
                  sizeof TH_ENDSHOUT - 1, F_BIG, C_WHITE, C_MAG);
    sn = 0;
    SLIT("TOP ");
    for (i = 0; i < SEATS; ++i)
        w += winners >> i & 1;
    if (w > 1)
        SLIT(T_PLAYER_P ":");
    else
        SLIT(T_PLAYER ":");
    for (i = 0; i < SEATS; ++i)
        if (winners >> i & 1) {
            sput(" ", 1);
            n = trimmed(i, nm);
            sput(nm, n);
        }
    gfx_text(gfx_center(GX, GW, sb, sn, F_SMALL), y + 31, sb, sn, F_SMALL, C_AMBER);
}

void display_game_over(void)
{
    over = 1;
    over_t = ticks();
    if (field && !helping_now) {
        gfx_begin(1);
        over_banner();
        gfx_end();
        dirty_prompt = 1;
    }
}

/* ---------------------------------------------------------------- the game loop's pass */

static unsigned rnd = 0x1234;
static unsigned char random8(void)
{
    rnd = rnd * 25173U + 13849U;
    return (unsigned char)(rnd >> 8);
}

void display_idle(void)
{
    unsigned char i, b, now = ticks(), step_t;
    static unsigned char last;

    snd_service();
    if (!field || helping_now)
        return;
    step_t = (unsigned char)(now - last);
    if (step_t >= 3) {                  /* the animation's clock: ~20 steps a second */
        last = now;
        ++anim_t;
        pulse = (unsigned char)((anim_t & 15) < 8 ? (anim_t & 7) * 2 : 14 - (anim_t & 7) * 2);
        if (pr_kind && !leaving) {
            dirty_prompt = 1;           /* the question's glow */
            if (pr_kind == 'P') {
                dirty_kit = 1;
                for (i = 0; i < RULE_KITSIZE; ++i)
                    if (kit[i] != NONE && !(kit[i] & 0x80))
                        mark_node(kit[i]);
            }
            if (pr_kind == 'F' || pr_kind == 'K' || (pr_kind == 'B' && !step))
                dirty_hives = 1;
        } else if (turn < SEATS && turn != me && !(anim_t & 15))
            dirty_prompt = 1;           /* WAITING FOR...'s dots */
        if (flashing) {
            flashing = 0;
            for (i = 0; i < NODES; ++i)
                if (flash[i]) {
                    --flash[i];
                    mark_node(i);
                    flashing = 1;
                }
        }
        if (blink_node != NONE && blink_node == last_node) {
            b = (unsigned char)(now - blink_t) < (unsigned char)(BLINK_SECS * 60);
            i = b ? (unsigned char)((anim_t >> 2) & 1) : 1;
            if (i != lit) {
                lit = i;
                mark_node(blink_node);
            }
            if (!b)
                blink_node = NONE;
        }
    }
    gfx_begin(0);
    for (i = 0; i < NODES; ++i)
        if (dirty_node[i >> 3] & (1 << (i & 7)))
            paint_node(i);
    for (i = 0; i < sizeof dirty_node; ++i)
        dirty_node[i] = 0;
    if (dirty_hives) {
        dirty_hives = 0;
        paint_hives();
        for (i = 0; i < NODES; ++i)     /* Black ICE that came or went */
            if (dirty_node[i >> 3] & (1 << (i & 7)))
                paint_node(i);
        for (i = 0; i < sizeof dirty_node; ++i)
            dirty_node[i] = 0;
    }
    if (dirty_kit) {
        dirty_kit = 0;
        paint_kit();
    }
    if (dirty_seats) {
        dirty_seats = 0;
        paint_seats();
    }
    if (dirty_log) {
        dirty_log = 0;
        paint_log();
    }
    if (dirty_prompt) {
        dirty_prompt = 0;
        paint_clock();
        paint_prompt();
    }
    if (over && (unsigned char)(now - over_t) < 240 && (anim_t & 1)) {     /* fireworks over the banner */
        short x = GX + 10 + random8() % (GW - 20), y = GY + 8 + random8() % (GH - 16);
        rgb_t c = syn_rgb[random8() % NSYN];
        for (i = 0; i < 8; ++i)
            gfx_pixel(x + (signed char)(random8() % 9) - 4, y + (signed char)(random8() % 9) - 4, c);
    }
    gfx_end();
}

/* ---------------------------------------------------------------- the title */

#define T_PANEL_Y 206
#define T_BTN_Y   282

static unsigned char floor_phase, title_drawn;

static void sky(void)
{
    unsigned char i;
    unsigned seed = 77;
    gfx_vgrad(0, 0, SCREEN_W, 150, 0x07021AUL, 0x3A0B52UL);
    for (i = 0; i < 60; ++i) {          /* stars, always in the same places */
        seed = seed * 25173U + 13849U;
        gfx_pixel((short)(seed % SCREEN_W), (short)((seed >> 9) % 110), i & 3 ? 0x8890C0UL : C_WHITE);
    }
}

static void sun(void)                   /* a striped sun going down behind the city */
{
    short r = 46, cy = 150, y, w, k;
    for (y = -r; y < 0; ++y) {
        if (y > -18 && ((-y) % 6) < 2 + (18 + y) / 8)
            continue;                   /* the stripes, wider near the horizon */
        for (k = r, w = 0; k > 0; --k)
            if ((long)k * k + (long)y * y <= (long)r * r) {
                w = k;
                break;
            }
        gfx_rect(160 - w, cy + y, 2 * w, 1, mix(0xFFE45CUL, 0xFF2E88UL, (unsigned char)((y + r) * 16 / r)));
    }
}

static void city(void)
{
    short x = 0, w, h, wx, wy;
    unsigned seed = 1977;
    rgb_t lights[3];
    lights[0] = C_CYAN; lights[1] = C_AMBER; lights[2] = C_MAG;
    while (x < SCREEN_W) {
        seed = seed * 25173U + 13849U;
        w = 14 + (short)(seed % 22);
        h = 18 + (short)((seed >> 5) % 46);
        if (x > 110 && x < 200)
            h = h / 2;                  /* lower in the middle: the sun shows */
        gfx_rect(x, 150 - h, w, h, 0x0A0820UL);
        gfx_rect(x, 150 - h, 1, h, 0x1A1440UL);
        for (wy = 150 - h + 3; wy < 147; wy += 4)
            for (wx = x + 3; wx < x + w - 2; wx += 4) {
                seed = seed * 25173U + 13849U;
                if ((seed >> 7) % 5 == 0)
                    gfx_rect(wx, wy, 2, 1, mix(lights[(seed >> 11) % 3], 0x0A0820UL, 6));
            }
        x += w + 1;
    }
}

static void floor_grid(void)            /* the synthwave floor, running towards the viewer */
{
    short y, i, d, x;
    gfx_vgrad(0, 150, SCREEN_W, 52, 0x1A0530UL, 0x05010CUL);
    gfx_rect(0, 150, SCREEN_W, 1, 0xFF2E88UL);
    for (i = -9; i <= 9; ++i) {         /* lines from the vanishing point */
        x = 160 + i * 36;
        gfx_line(160 + i * 4, 151, x, 201, mix(0xFF2E88UL, 0x1A0530UL, 6));
    }
    for (i = 0; i < 7; ++i) {           /* crossing lines, closer together near the horizon */
        d = (short)((i * 8 + floor_phase) % 56);
        y = 151 + (d * d) / 62;
        if (y < 201)
            gfx_rect(0, y, SCREEN_W, 1, mix(0xFF2E88UL, 0x1A0530UL, (unsigned char)(10 - d / 8)));
    }
}

static void logo(void)
{
    short x = gfx_center(0, SCREEN_W, TH_TITLE, sizeof TH_TITLE - 1, F_LOGO), y = 22;
    gfx_text(x + 2, y + 2, TH_TITLE, sizeof TH_TITLE - 1, F_LOGO, 0x40104FUL);   /* a shadow */
    gfx_glow_text(x, y, TH_TITLE, sizeof TH_TITLE - 1, F_LOGO, C_CYAN, 0x8A1A7AUL);
    gfx_clip(0, y, SCREEN_W, F_LOGO_ASC / 2 + 3);      /* chrome: the top half white */
    gfx_text(x, y, TH_TITLE, sizeof TH_TITLE - 1, F_LOGO, 0xE8FBFFUL);
    gfx_unclip();
    say(gfx_center(0, SCREEN_W, "A NETWORK GAME OF HACKING AND CRYPTO", 36, F_TINY), 62,
        "A NETWORK GAME OF HACKING AND CRYPTO", F_TINY, 0xFF6AD5UL);
}

static void title_buttons(void)
{
    short x = 136, w = 44;
    nbtn = 0;
    plate(6, T_BTN_Y, 124, 32, T_JACKIN, F_HEAD, C_CYAN, edit_len != 0);
    add_button(6, T_BTN_Y, 124, 32, ACT_KEY, "\r");
    plate(x, T_BTN_Y, w, 32, "HELP", F_TINY, C_AMBER, 0);
    add_button(x, T_BTN_Y, w, 32, ACT_KEY, "\002");
    x += w + 2;
    plate(x, T_BTN_Y, w, 32, "HINTS", F_TINY, hints ? C_CYAN : C_DIM, 0);
    add_button(x, T_BTN_Y, w, 32, ACT_KEY, "\001");
    x += w + 2;
    plate(x, T_BTN_Y, w, 32, sound_on ? "SOUND" : "MUTED", F_TINY, sound_on ? C_GREEN : C_DIM, 0);
    add_button(x, T_BTN_Y, w, 32, ACT_SOUND, "");
    x += w + 2;
    plate(x, T_BTN_Y, w - 2, 32, "QUIT", F_TINY, C_RED, 0);
    add_button(x, T_BTN_Y, w - 2, 32, ACT_KEY, "\033Y");
}

void display_title_update(void)
{
    char b[9];
    unsigned char i, n;
    const struct region_entry *r = &regions[palm_region];
    if (helping_now || picking_region)
        return;
    gfx_begin(0);
    gfx_rect(70, T_PANEL_Y + 4, 240, 12, C_BG1);
    say(70, T_PANEL_Y + 5, r->name, F_SMALL, C_WHITE);
    say(76 + gfx_width(r->name, (unsigned char)strlen(r->name), F_SMALL), T_PANEL_Y + 7, r->pace, F_TINY, C_DIM);
    gfx_text(SCREEN_W - 52, T_PANEL_Y + 7, "CHANGE", 6, F_TINY, C_CYAN);
    gfx_rect(70, T_PANEL_Y + 20, 240, 13, C_BG1);
    if (status_len) {
        n = status_len - 1;
        gfx_text(70, T_PANEL_Y + 21, (const char *)status_text, n, F_SMALL, C_GREEN);
    } else if (status_code == NO_CARRIER)
        say(70, T_PANEL_Y + 21, "NO CARRIER: JACK-IN DIALS AGAIN", F_SMALL, C_RED);
    else
        say(70, T_PANEL_Y + 21, "DIALING...", F_SMALL, C_DIM);
    gfx_bevel(70, T_PANEL_Y + 37, 100, 22, C_BG0, C_EDGE, C_EDGEHI);
    for (i = 0; i < 8; ++i)
        b[i] = (char)(i < edit_len ? edit[i] : ' ');
    gfx_text(75, T_PANEL_Y + 39, b, 8, F_TEXT, C_WHITE);
    if (edit_len < 8)
        gfx_rect(75 + gfx_width(b, edit_len, F_TEXT), T_PANEL_Y + 40, 7, 15, C_AMBER);
    gfx_text(178, T_PANEL_Y + 44, edit_len ? "TAP TO CHANGE" : "TAP TO TYPE", (unsigned char)(edit_len ? 13 : 11),
             F_TINY, C_DIM);
    title_buttons();
    add_button(0, T_PANEL_Y, SCREEN_W, 18, ACT_KEY, "\003");        /* the region: pick another */
    add_button(66, T_PANEL_Y + 34, 250, 28, ACT_HANDLE, "");
    gfx_end();
}

void display_title_screen(void)
{
    field = 0;
    over = 0;
    picking_region = 0;
    gfx_begin(1);
    sky();
    sun();
    city();
    floor_grid();
    logo();
    gfx_rect(0, 202, SCREEN_W, SCREEN_H - 202, C_BG0);
    gfx_bevel(4, T_PANEL_Y, SCREEN_W - 8, 62, C_BG1, C_EDGE, C_BG0);
    gfx_rect(4, T_PANEL_Y, SCREEN_W - 8, 1, C_CYAN);
    say(12, T_PANEL_Y + 6, "REGION", F_TINY, C_DIM);
    say(12, T_PANEL_Y + 22, "STATUS", F_TINY, C_DIM);
    say(12, T_PANEL_Y + 43, "HANDLE", F_TINY, C_DIM);
    gfx_end();
    if (refused)
        display_message(refused == REFUSED_LATE ? "TOO LATE: THE LAST SEAT WAS TAKEN" : "THAT HANDLE IS TAKEN");
    else
        display_message("TYPE A HANDLE, THEN JACK-IN");
    display_title_update();
    if (!title_drawn) {
        title_drawn = 1;
        play_chord(CHORD_END);          /* the first time: a fanfare */
    }
}

/* The title's life: the floor runs towards you, the stars twinkle, the logo glitches now and then. */
void display_title_idle(void)
{
    static unsigned char last, glitch;
    unsigned char now = ticks();
    short y;

    snd_service();
    if (helping_now || picking_region || field || (unsigned char)(now - last) < 3)
        return;
    last = now;
    ++anim_t;
    gfx_begin(1);
    floor_phase = (unsigned char)((floor_phase + 1) % 8);
    floor_grid();
    y = (short)(random8() % 110);
    gfx_pixel((short)(random8() % SCREEN_W), y, random8() & 1 ? C_WHITE : 0x07021AUL);
    if (glitch) {                       /* put the slice back */
        --glitch;
        if (!glitch) {
            gfx_vgrad(0, 18, SCREEN_W, 42, mix(0x07021AUL, 0x3A0B52UL, 2), mix(0x07021AUL, 0x3A0B52UL, 6));
            logo();
        }
    } else if (random8() < 6) {         /* a glitch: a slice of the logo slips sideways, in magenta */
        y = 24 + (short)(random8() % 26);
        gfx_clip(0, y, SCREEN_W, 3 + random8() % 4);
        gfx_rect(0, y, SCREEN_W, 8, 0x1A0632UL);
        gfx_text(gfx_center(0, SCREEN_W, TH_TITLE, sizeof TH_TITLE - 1, F_LOGO) + (random8() & 1 ? 4 : -4), 22,
                 TH_TITLE, sizeof TH_TITLE - 1, F_LOGO, C_MAG);
        gfx_unclip();
        glitch = 2;
    }
    if (!(anim_t & 7)) {                /* the cursor blinks */
        gfx_rect(75 + gfx_width("        ", edit_len, F_TEXT), T_PANEL_Y + 40, 7, 15,
                 edit_len < 8 && (anim_t & 8) ? C_AMBER : C_BG0);
    }
    gfx_end();
}

/* ---------------------------------------------------------------- the regions */

#if REGION_PICK
static char url[48];

const char *region_url(void)
{
    char p[6];
    unsigned char n = 0, k = 0;
    unsigned port = regions[palm_region].port;
    strcpy(url, "N:TCP://" REGION_HOST ":");
    do {
        p[n++] = (char)('0' + port % 10);
        port /= 10;
    } while (port);
    k = (unsigned char)strlen(url);
    while (n)
        url[k++] = p[--n];
    url[k++] = '/';
    url[k] = 0;
    return url;
}

void region_pick(unsigned char (*key)(void))
{
    unsigned char i, c;
    short y;
    picking_region = 1;
    gfx_begin(1);
    gfx_bevel(16, 70, SCREEN_W - 32, 30 + NREGIONS * 33 + 32, C_BG1, C_CYAN, C_BG0);
    say(gfx_center(16, SCREEN_W - 32, "PICK A REGION", 13, F_HEAD), 78, "PICK A REGION", F_HEAD, C_CYAN);
    nbtn = 0;
    for (i = 0; i < NREGIONS; ++i) {
        char k[2];
        y = 100 + i * 33;
        plate(26, y, SCREEN_W - 52, 31, "", F_TINY, C_CYAN, i == palm_region);
        say(36, y + 2, regions[i].name, F_SMALL, i == palm_region ? C_BG0 : C_WHITE);
        say(36, y + 17, regions[i].pace, F_TINY, i == palm_region ? C_BG0 : C_DIM);
        k[0] = (char)('1' + i);
        k[1] = 0;
        add_button(26, y, SCREEN_W - 52, 31, ACT_KEY, k);
    }
    y = 100 + NREGIONS * 33 + 2;
    plate(gfx_center(16, SCREEN_W - 32, "", 0, F_TINY) - 40, y, 80, 22, "CANCEL", F_TINY, C_TEXT, 0);
    add_button(120, y, 80, 22, ACT_KEY, "\033");
    gfx_end();
    for (;;) {
        c = key();
        if (c == K_ESC || c == K_RETURN)
            break;
        if (c >= '1' && c < '1' + NREGIONS) {
            palm_region = (unsigned char)(c - '1');
            break;
        }
    }
    picking_region = 0;
}
#endif

/* ---------------------------------------------------------------- the instructions */

void help_open(void)
{
    helping_now = 1;
    help_pg = 1;
    help_draw(help_pg);
}

unsigned char help_key(unsigned char k)
{
    if (k == '>' || k == '<' || k == K_DOWN || k == K_UP) {
        if ((k == '>' || k == K_DOWN) && help_pg < help_pages())
            help_draw(++help_pg);
        else if ((k == '<' || k == K_UP) && help_pg > 1)
            help_draw(--help_pg);
        return 1;
    }
    if (k != K_ESC && k != K_RETURN && k != K_HELP)
        return 1;
    helping_now = 0;
    if (field)
        paint_all();
    else
        display_title_screen();
    return 0;
}

/* ---------------------------------------------------------------- taps */

/* The keys for the number being typed (a cash-out or a swap), set to v. */
static void set_number(int v)
{
    unsigned char held = pr[3];
    if (v < 0)
        v = 0;
    if (v > held)
        v = held;
    key_push(K_DELETE);
    key_push(K_DELETE);
    if (v >= 10)
        key_push((unsigned char)('0' + v / 10));
    key_push((unsigned char)('0' + v % 10));
}

static int typed_now(void)
{
    int v = 0;
    unsigned char i;
    for (i = 0; i < edit_len; ++i)
        v = v * 10 + edit[i] - '0';
    return v;
}

unsigned char palm_tap(short x, short y)
{
    unsigned char i, j;
    struct button *b;

    if (helping_now) {                  /* the instructions: halves turn the page, the bottom bar leaves */
        key_push(y >= SCREEN_H - 22 && x > 100 && x < 220 ? K_ESC : x < SCREEN_W / 2 ? '<' : '>');
        return 1;
    }
    for (i = 0; i < nbtn; ++i) {
        b = &btn[i];
        if (x < b->x || x >= b->x + b->w || y < b->y || y >= b->y + b->h)
            continue;
        switch (b->act) {
        case ACT_KEY:
            for (j = 0; j < sizeof b->keys && b->keys[j]; ++j)
                key_push((unsigned char)b->keys[j]);
            break;
        case ACT_INC: set_number(typed_now() + 1); break;
        case ACT_DEC: set_number(typed_now() - 1); break;
        case ACT_MAX: set_number(pr[3]); break;
        case ACT_HANDLE: palm_handle_dialog(); break;
        case ACT_SOUND:
            snd_toggle();
            if (!field) {
                gfx_begin(0);
                title_buttons();
                add_button(0, T_PANEL_Y, SCREEN_W, 18, ACT_KEY, "\003");
                add_button(66, T_PANEL_Y + 34, 250, 28, ACT_HANDLE, "");
                gfx_end();
            }
            break;
        }
        return 1;
    }
    if (picking_region || !field)
        return 0;
    if (y < HDR_H) {                    /* the header's tools */
        if (x >= SCREEN_W - 4 * TOOL_W) {
            switch ((SCREEN_W - 1 - x) / TOOL_W) {
            case 0: key_push(K_ESC); break;
            case 1:
                snd_toggle();
                gfx_begin(0);
                tool_icon(1);
                gfx_end();
                break;
            case 2:
                key_push(K_OPTION);
                gfx_begin(0);
                tool_icon(2);
                gfx_end();
                break;
            case 3: key_push(K_HELP); break;
            }
            return 1;
        }
        return 0;
    }
    if (x >= GX && x < GX + GW && y >= GY && y < GY + GH) {     /* a node: the exploit that runs there */
        unsigned char node = (unsigned char)((y - GY) / CELL * RULE_COLS + (x - GX) / CELL);
        if (pr_kind != 'P')
            return 0;
        for (j = 0; j < RULE_KITSIZE; ++j)
            if (kit[j] != NONE && (kit[j] & 0x7F) == node) {
                key_push((unsigned char)('1' + j));
                return 1;
            }
        return 0;
    }
    if (x >= PX && y >= HIVE_Y && y < HIVE_Y + NSYN * HIVE_H) {   /* a syndicate */
        j = (unsigned char)((y - HIVE_Y) / HIVE_H);
        if (pr_kind == 'F' || pr_kind == 'K' || (pr_kind == 'B' && !step)) {
            key_push((unsigned char)syn_letter[j]);
            return 1;
        }
        return 0;
    }
    if (x >= PX && y >= KIT_Y && y < KIT_Y + 2 * (CHIP_H + 1)) {  /* a kit slot */
        j = (unsigned char)((y - KIT_Y) / (CHIP_H + 1) * 3 + (x - PX) / (CHIP_W + 1));
        if (pr_kind == 'P' && j < RULE_KITSIZE) {
            key_push((unsigned char)('1' + j));
            return 1;
        }
        return 0;
    }
    if (y >= log_y && y < LOG_END) {    /* the log: back through it; its bottom right corner forward again */
        key_push(x >= SCREEN_W - 40 && y >= LOG_END - LOG_H ? K_DOWN : K_UP);
        return 1;
    }
    return 0;
}

unsigned char palm_now(void)
{
    return ticks();
}

#ifdef PALM_HOST
/* The PC preview's autoplay (test/palm_host.c): where to tap for key k, as a player would. */
unsigned char palm_spot(unsigned char k, short *x, short *y)
{
    unsigned char i, j;
    for (i = 0; i < nbtn; ++i)
        if (btn[i].act == ACT_KEY && (unsigned char)btn[i].keys[0] == k && !btn[i].keys[1]) {
            *x = btn[i].x + btn[i].w / 2;
            *y = btn[i].y + btn[i].h / 2;
            return 1;
        }
    if (!field)
        return 0;
    if (pr_kind == 'P' && k >= '1' && k < '1' + RULE_KITSIZE && kit[k - '1'] != NONE) {
        j = (unsigned char)(kit[k - '1'] & 0x7F);
        if (k & 1) {                    /* the node, or (even keys) the kit slot */
            *x = GX + (j % RULE_COLS) * CELL + CELL / 2;
            *y = GY + (j / RULE_COLS) * CELL + CELL / 2;
        } else {
            *x = PX + (k - '1') % 3 * (CHIP_W + 1) + CHIP_W / 2;
            *y = KIT_Y + 1 + (k - '1') / 3 * (CHIP_H + 1) + CHIP_H / 2;
        }
        return 1;
    }
    if (pr_kind == 'F' || pr_kind == 'K' || (pr_kind == 'B' && !step))
        for (j = 0; j < NSYN; ++j)
            if (syn_letter[j] == k) {
                *x = PX + PW / 2;
                *y = HIVE_Y + j * HIVE_H + HIVE_H / 2;
                return 1;
            }
    return 0;
}
#endif
