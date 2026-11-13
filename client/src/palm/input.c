/* platform.h's keys and time on the Palm, and the Palm's events: a tap goes to display.c's palm_tap, which says
 * what it means as keys; the keyboard (the Clie TG50 has one), Graffiti, the scroll buttons and a Clie's jog dial
 * give keys directly; the menu gives keys too. Keys wait in a small queue for in_key(). */
#include <PalmOS.h>
#include "../config.h"
#include "../platform.h"
#include "../display.h"
#include "../common/game.h"
#include "palm.h"
#include "synackRsc.h"

#define JOG_UP   0x1700                 /* a Sony Clie's jog dial (SonyChars.h) */
#define JOG_DOWN 0x1701
#define JOG_BACK 0x1707

void palm_quit_now(void);               /* start.c */

static unsigned char q[K_TAP_MAX], qh, qt;
static UInt32 last_events;

void key_push(unsigned char k)
{
    unsigned char next = (unsigned char)((qt + 1) % K_TAP_MAX);
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

/* A key from the Palm as the game's key, or 0. */
static unsigned char game_key_of(const EventType *e)
{
    WChar c = e->data.keyDown.chr;
    if (e->data.keyDown.modifiers & commandKeyMask) {
        switch (c) {
        case vchrPageUp: case JOG_UP: return K_UP;
        case vchrPageDown: case JOG_DOWN: return K_DOWN;
        case JOG_BACK: return K_ESC;
        }
        return 0;
    }
    if (c == chrBackspace)
        return K_DELETE;
    if (c == chrLineFeed || c == chrCarriageReturn)
        return K_RETURN;
    if (c == chrEscape)
        return K_ESC;
    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    return c >= ' ' && c < 0x7F ? (unsigned char)c : 0;
}

static Boolean MainHandler(EventType *e)
{
    switch (e->eType) {
    case menuEvent:
        switch (e->data.menu.itemID) {
        case MenuHelp: key_push(K_HELP); break;
        case MenuHints: key_push(K_OPTION); break;
        case MenuSound: snd_toggle(); display_repaint(); break;
        case MenuRegion: key_push(K_REGION); break;
        case MenuLogOff: key_push(K_ESC); break;
        case MenuQuit: palm_quit_now(); break;
        }
        return true;
    case frmUpdateEvent:
        display_repaint();
        return true;
    default:
        return false;
    }
}

void palm_form_open(void)
{
    FormType *f = FrmInitForm(MainForm);
    FrmSetActiveForm(f);
    FrmSetEventHandler(f, MainHandler);
    FrmDrawForm(f);
}

static void handle(EventType *e)
{
    Err err;
    unsigned char k;
    if (e->eType == appStopEvent)
        palm_quit_now();
    if (SysHandleEvent(e))
        return;
    if (MenuHandleEvent(0, e, &err))
        return;
    if (e->eType == penDownEvent && e->screenY < SCREEN_H / 2) {
        if (palm_tap((short)(e->screenX * 2), (short)(e->screenY * 2)))
            snd_click();
        return;
    }
    if (e->eType == keyDownEvent && (k = game_key_of(e)) != 0) {
        key_push(k);
        return;
    }
    FrmDispatchEvent(e);
}

unsigned char in_key(void)
{
    EventType e;
    unsigned char n, k;
    snd_service();
    for (n = 0; qh == qt && n < 6; ++n) {
        EvtGetEvent(&e, n ? 0 : 1);     /* the first waits a tick at most: the loop goes on, and the battery rests */
        last_events = TimGetTicks();
        if (e.eType == nilEvent)
            break;
        handle(&e);
    }
    if (qh == qt)
        return 0;
    k = q[qh];
    qh = (unsigned char)((qh + 1) % K_TAP_MAX);
    return k;
}

/* While the game waits without reading keys (a redial's pause), the Palm still gets to stop the program. */
static void events_meanwhile(void)
{
    EventType e;
    last_events = TimGetTicks();
    EvtGetEvent(&e, 0);
    if (e.eType == appStopEvent)
        palm_quit_now();
    if (e.eType != nilEvent)
        SysHandleEvent(&e);
}

unsigned char ticks(void)
{
    UInt32 t = TimGetTicks(), s = SysTicksPerSecond();
    if (t - last_events > s / 2)
        events_meanwhile();
    return (unsigned char)((t / s) * 60 + (t % s) * 60 / s);
}

unsigned char ticks_per_sec(void)
{
    return 60;
}

/* The handle typed in a Palm dialog (for a Palm without a keyboard): queued as the keys that type it. */
unsigned char palm_handle_dialog(void)
{
    FormType *f = FrmInitForm(HandleForm);
    UInt16 at = FrmGetObjectIndex(f, HandleField);
    FieldType *fld = (FieldType *)FrmGetObjectPtr(f, at);
    MemHandle h = MemHandleNew(9);
    char *p, c;
    unsigned char i, n = 0;

    if (h) {
        p = (char *)MemHandleLock(h);
        for (i = 0; i < edit_len && i < 8; ++i)
            p[i] = (char)edit[i];
        p[i] = 0;
        MemHandleUnlock(h);
        FldSetTextHandle(fld, h);
    }
    FrmSetFocus(f, at);
    if (FrmDoDialog(f) != HandleOK) {
        FrmDeleteForm(f);
        return 0;
    }
    for (i = 0; i < 8; ++i)
        key_push(K_DELETE);
    p = FldGetTextPtr(fld);
    for (i = 0; p && p[i] && n < 8; ++i) {
        c = p[i];
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        if (c > ' ' && c <= 'Z') {
            key_push((unsigned char)c);
            ++n;
        }
    }
    FrmDeleteForm(f);
    return 1;
}
