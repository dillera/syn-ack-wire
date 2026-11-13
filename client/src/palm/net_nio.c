/* platform.h's network calls on the Palm: FujiNet NIO running on the Palm itself (fujinet-nio-palmOS), reached
 * through fujinet-nio-lib, over whatever network the Palm has (Bluetooth to a gateway, Wi-Fi, a cradle). As
 * src/amiga/net_fn.c, which this follows: one TCP session, offsets counted in each direction. The first open dials
 * the Palm's network service; the Palm shows its own progress while it does. */
#include <string.h>
#include <fujinet-nio.h>
#include "../platform.h"
#include "palm.h"

unsigned char rx_ring[256];
unsigned char rx_head, rx_tail, tx[16];

#define READ_CAP 128

static fn_handle_t sess;
static uint32_t roff, woff;
static unsigned char is_open, inited, eof;
static char host[64];

/* "N:TCP://host:port/": the host, and the port (1-65535); 0 when it doesn't parse */
static unsigned parse(const char *url)
{
    const char *p = url, *colon;
    unsigned long port = 0;
    unsigned len;
    if (p[0] == 'N' && p[1] == ':')
        p += 2;
    if (strncmp(p, "TCP://", 6))
        return 0;
    p += 6;
    colon = strchr(p, ':');
    if (!colon || colon == p || (unsigned)(colon - p) >= sizeof host)
        return 0;
    len = (unsigned)(colon - p);
    memcpy(host, p, len);
    host[len] = 0;
    for (p = colon + 1; *p >= '0' && *p <= '9'; ++p) {
        port = port * 10 + (unsigned)(*p - '0');
        if (port > 65535UL)
            return 0;
    }
    return (unsigned)port;
}

unsigned char net_open(const char *url)
{
    unsigned port = parse(url);
    unsigned char rc;
    rx_head = rx_tail = 0;
    roff = woff = 0;
    eof = is_open = 0;
    if (!port)
        return 1;
    if (!inited) {
        if ((rc = fn_init()) != FN_OK)
            return rc;
        inited = 1;
    }
    if ((rc = fn_tcp_open(&sess, host, (uint16_t)port)) != FN_OK)
        return rc;
    is_open = 1;
    return 0;
}

unsigned char net_poll(void)
{
    uint16_t got = 0;
    uint8_t flags = 0, rc;
    unsigned char room = rx_head - rx_tail - 1, n;
    if (!is_open)
        return NET_CLOSED;
    if (eof)
        return rx_head == rx_tail ? NET_CLOSED : 0;
    n = room;
    if (rx_tail && n > (unsigned char)-rx_tail)         /* room to the ring's end */
        n = -rx_tail;
    if (n > READ_CAP)
        n = READ_CAP;
    if (!n)
        return 0;
    rc = fn_read(sess, roff, rx_ring + rx_tail, n, &got, &flags);
    if (rc == FN_ERR_NOT_READY)
        return 0;
    if (rc != FN_OK)
        return rc;
    rx_tail += (unsigned char)got;
    roff += got;
    if (flags & FN_READ_EOF) {
        eof = 1;
        return got || rx_head != rx_tail ? 0 : NET_CLOSED;
    }
    return 0;
}

unsigned char net_send(unsigned char len)
{
    uint16_t w = 0;
    uint8_t rc;
    if (tx[0] == 'H')
        palm_save_handle();             /* the hello carries the handle the player chose */
    rc = fn_write(sess, woff, tx, len, &w);
    woff += w;
    return rc ? rc : (w == len ? 0 : 1);
}

void net_close(void)
{
    if (is_open)
        fn_close(sess);
    is_open = 0;
}

void net_shutdown(void)
{
    net_close();
    if (inited)
        fn_shutdown();
    inited = 0;
}

unsigned char net_appkey(unsigned char key)
{
    (void)key;
    rx_ring[2] = 0;                     /* no FujiNet game lobby here */
    return 0;
}
