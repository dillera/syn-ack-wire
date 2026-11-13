/* The Palm client's display on a PC (make pc-palm): src/palm/display.c and help.c drawing into a 320 x 320 picture
 * with the game's fonts (build/palm/fonts/glyphs<N>.bin, drawn by src/palm/text.c), the common code over sockets, and test/autoplay.c's
 * keys. Each of autoplay's dumps saves the picture as DUMPDIR/scrNNN.ppm (tools/palm_render.py makes PNGs).
 *
 *   build/pc/pc-palm [host] [port]
 *
 * TAPS, if set, is a file of "x y" lines: each is a tap, one every KEY_TICKS, given to palm_tap() before autoplay's
 * keys, to try the stylus parts. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "../src/config.h"
#include "../src/platform.h"
#include "../src/display.h"
#include "../src/common/proto.h"
#include "../src/common/game.h"
#include "../src/palm/palm.h"

void synack_main(void);

/* ---------------------------------------------------------------- the picture */

static unsigned long fb[SCREEN_H][SCREEN_W];
static short cx0, cy0, cx1 = SCREEN_W, cy1 = SCREEN_H;
static unsigned char *font_data[PALM_FONTS];
static rgb_t pen;

static void put(short x, short y, rgb_t c)
{
    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1)
        fb[y][x] = c;
}

void gfx_init(void)
{
    char path[256];
    int f;
    long n;
    FILE *in;
    for (f = 0; f < PALM_FONTS; ++f) {
        snprintf(path, sizeof path, "build/palm/fonts/glyphs%d.bin", f);
        if (!(in = fopen(path, "rb"))) {
            perror(path);
            exit(1);
        }
        fseek(in, 0, SEEK_END);
        n = ftell(in);
        rewind(in);
        font_data[f] = malloc(n);
        if (fread(font_data[f], 1, n, in) != (size_t)n)
            exit(1);
        fclose(in);
    }
}

const unsigned char *gfx_font(unsigned char font)
{
    return font < PALM_FONTS ? font_data[font] : 0;
}

void gfx_color(rgb_t c)
{
    pen = c;
}

void gfx_done(void) {}
void gfx_begin(unsigned char whole) { (void)whole; }
void gfx_end(void) {}

void gfx_rect(short x, short y, short w, short h, rgb_t c)
{
    short i, j;
    for (j = y; j < y + h; ++j)
        for (i = x; i < x + w; ++i)
            put(i, j, c);
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
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy, e2;
    for (;;) {
        put(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_pixel(short x, short y, rgb_t c)
{
    put(x, y, c);
}

void gfx_fill(short x, short y, short w, short h)
{
    gfx_rect(x, y, w, h, pen);
}

void gfx_clip(short x, short y, short w, short h)
{
    cx0 = x; cy0 = y; cx1 = x + w; cy1 = y + h;
}

void gfx_unclip(void)
{
    cx0 = cy0 = 0;
    cx1 = SCREEN_W;
    cy1 = SCREEN_H;
}

void pc_dump(void)
{
    static int n;
    char path[512];
    const char *dir = getenv("DUMPDIR");
    FILE *f;
    int x, y;
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/scr%03d.ppm", dir, n++);
    if (!(f = fopen(path, "wb")))
        return;
    fprintf(f, "P6 %d %d 255\n", SCREEN_W, SCREEN_H);
    for (y = 0; y < SCREEN_H; ++y)
        for (x = 0; x < SCREEN_W; ++x) {
            fputc((int)(fb[y][x] >> 16 & 255), f);
            fputc((int)(fb[y][x] >> 8 & 255), f);
            fputc((int)(fb[y][x] & 255), f);
        }
    fclose(f);
}

/* ---------------------------------------------------------------- the network: sockets */

unsigned char rx_ring[256], rx_head, rx_tail, tx[16];
static int sock = -1;
static const char *host = "127.0.0.1", *port = "6599";

unsigned char net_open(const char *url)
{
    struct addrinfo *ai;
    (void)url;
    rx_head = rx_tail = 0;
    if (getaddrinfo(host, port, 0, &ai))
        return 1;
    sock = socket(ai->ai_family, SOCK_STREAM, 0);
    if (connect(sock, ai->ai_addr, ai->ai_addrlen))
        return 2;
    fcntl(sock, F_SETFL, O_NONBLOCK);
    return 0;
}

unsigned char net_poll(void)
{
    unsigned char room = rx_head - rx_tail - 1, end = -rx_tail;
    int n;
    if (rx_tail && end < room)
        room = end;
    if (!room)
        return 0;
    n = recv(sock, rx_ring + rx_tail, room, 0);
    if (n == 0)
        return NET_CLOSED;
    if (n > 0)
        rx_tail += n;
    return 0;
}

unsigned char net_send(unsigned char len)
{
    return send(sock, tx, len, 0) == len ? 0 : 1;
}

void net_close(void)
{
    close(sock);
}

unsigned char net_appkey(unsigned char key)
{
    (void)key;
    rx_ring[2] = 0;
    return 0;
}

/* ---------------------------------------------------------------- keys, taps, time, sound */

static unsigned char q[K_TAP_MAX], qh, qt;
static FILE *taps;

void key_push(unsigned char k)
{
    unsigned char next = (qt + 1) % K_TAP_MAX;
    if (next != qh) {
        q[qt] = k;
        qt = next;
    }
}

void keys_push(const char *s)
{
    while (*s)
        key_push((unsigned char)*s++);
}

unsigned char ticks(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (unsigned char)((tv.tv_sec * 1000000LL + tv.tv_usec) / 16667);
}

unsigned char ticks_per_sec(void)
{
    return 60;
}

/* autoplay's keys come first; the queue (taps, from TAPS) is read when it has none */
unsigned char in_key(void)
{
    static unsigned char last;
    unsigned char k, now = ticks();
    char line[64];
    int x, y;
    usleep(2000);
    if (qh != qt) {
        k = q[qh];
        qh = (qh + 1) % K_TAP_MAX;
        return k;
    }
    if (taps && (unsigned char)(now - last) >= 30 && fgets(line, sizeof line, taps)) {
        last = now;
        if (sscanf(line, "%d %d", &x, &y) == 2) {
            fprintf(stderr, "tap %d %d: %s\n", x, y, palm_tap((short)x, (short)y) ? "taken" : "nothing");
            pc_dump();
        }
    }
    return 0;
}

/* autoplay's AUTOCLICK hook: the key as a tap where a player would make it (display.c's palm_spot), else 0 */
unsigned char palm_spot(unsigned char k, short *x, short *y);
unsigned char amiga_autoclick(unsigned char k)
{
    short x, y;
    if (!palm_spot(k, &x, &y))
        return 0;
    return palm_tap(x, y);
}

unsigned char palm_handle_dialog(void) { return 0; }
void palm_save_handle(void) {}
void platform_quit(void) { exit(0); }
unsigned char palm_region;
unsigned char sound_on = 1;
void snd_init(void) {}
void play_chord(unsigned char n) { (void)n; }
void snd_toggle(void) { sound_on ^= 1; }
void snd_service(void) {}
void snd_click(void) {}
void snd_off(void) {}

int main(int argc, char **argv)
{
    if (argc > 1)
        host = argv[1];
    if (argc > 2)
        port = argv[2];
    if (getenv("TAPS"))
        taps = fopen(getenv("TAPS"), "r");
    gfx_init();
    synack_main();
    return 0;
}
