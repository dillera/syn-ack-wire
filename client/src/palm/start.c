/* The Palm program: checks the screen, opens the one form (the menus live on it), loads the settings, and runs the
 * common code's main loop (src/common/main.c, built as synack_main). The loop never returns: leaving the program
 * (another app, the menu's Quit, QUIT on the title) jumps back here, where everything is put back. */
#include <PalmOS.h>
#include <DLServer.h>
#include "../config.h"
#include "../platform.h"
#include "../display.h"
#include "../common/game.h"
#include "palm.h"
#include "synackRsc.h"

#define CREATOR 'ANSW'
#define PREFS_ID 1
#define PREFS_VERSION 1

void synack_main(void);                 /* src/common/main.c */
void palm_form_open(void);              /* input.c */
void net_shutdown(void);                /* net_nio.c */

unsigned char palm_region;
static ErrJumpBuf jump;

static struct {
    char handle[8];
    unsigned char len, sound, hints, region;
} prefs;

void palm_quit_now(void)
{
    ErrLongJump(jump, 1);
}

void platform_quit(void)
{
    palm_quit_now();
}

void palm_save_handle(void)
{
    unsigned char i, n = 8;
    for (i = 0; i < 8; ++i)
        prefs.handle[i] = (char)tx[7 + i];
    while (n && prefs.handle[n - 1] == ' ')
        --n;
    prefs.len = n;
    prefs.region = palm_region;
    PrefSetAppPreferences(CREATOR, PREFS_ID, PREFS_VERSION, &prefs, sizeof prefs, true);
}

/* The handle: the one used last time, else the Palm's HotSync name, as the title lets a player type it. */
static void load_prefs(void)
{
    UInt16 size = sizeof prefs;
    char user[dlkUserNameBufSize];
    unsigned char i, n = 0;
    char c;

    if (PrefGetAppPreferences(CREATOR, PREFS_ID, &prefs, &size, true) == PREFS_VERSION && size == sizeof prefs) {
        for (i = 0; i < prefs.len && i < 8; ++i)
            edit[i] = (unsigned char)prefs.handle[i];
        edit_len = prefs.len > 8 ? 8 : prefs.len;
        sound_on = prefs.sound;
        hints = prefs.hints;
        palm_region = prefs.region < 4 ? prefs.region : 0;
        return;
    }
    prefs.sound = prefs.hints = 1;
    if (DlkGetSyncInfo(NULL, NULL, NULL, user, NULL, NULL) == errNone)
        for (i = 0; user[i] && n < 8; ++i) {
            c = user[i];
            if (c >= 'a' && c <= 'z')
                c -= 'a' - 'A';
            if (c > ' ' && c <= 'Z')
                edit[n++] = (unsigned char)c;
        }
    edit_len = n;
}

static void save_prefs(void)
{
    prefs.sound = sound_on;
    prefs.hints = hints;
    prefs.region = palm_region;
    PrefSetAppPreferences(CREATOR, PREFS_ID, PREFS_VERSION, &prefs, sizeof prefs, true);
}

/* Palm OS 5's high-density calls, a double-density screen, and 16-bit colour. */
static Boolean capable(void)
{
    UInt32 version = 0, density = 0, depths = 0;
    if (FtrGet(sysFtrCreator, sysFtrNumWinVersion, &version) != errNone || version < 4)
        return false;
    if (WinScreenGetAttribute(winScreenDensity, &density) != errNone || density != kDensityDouble)
        return false;
    if (WinScreenMode(winScreenModeGetSupportedDepths, NULL, NULL, &depths, NULL) != errNone)
        return false;
    return (depths & (1UL << 15)) != 0;
}

UInt32 PilotMain(UInt16 cmd, MemPtr pbp, UInt16 flags)
{
    (void)pbp;
    (void)flags;
    if (cmd != sysAppLaunchCmdNormalLaunch)
        return 0;
    if (!capable()) {
        FrmAlert(NeedAlert);
        return 0;
    }
    load_prefs();
    palm_form_open();
    gfx_init();
    if (ErrSetJump(jump) == 0)
        synack_main();
    snd_off();
    net_shutdown();
    save_prefs();
    gfx_done();
    FrmCloseAllForms();
    return 0;
}
