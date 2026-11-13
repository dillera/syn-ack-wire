/* platform.h's chords on the Palm: the speaker plays one square-wave note at a time, so each chord is a quick run of
 * notes (an arpeggio), played without waiting: snd_service starts each next note when the one before has had its
 * time. The volume is the Palm's own "game sounds" setting; at 0 the game is quiet. */
#include <PalmOS.h>
#include "../config.h"
#include "../platform.h"
#include "palm.h"

struct note { unsigned short hz; unsigned char ms; };   /* hz 0: a rest; ms 0: the end */

#define C4 262
#define E4 330
#define G4 392
#define A4 440
#define B4 494
#define C5 523
#define D5 587
#define E5 659
#define FS5 740
#define G5 784
#define A5 880
#define C6 1047
#define E6 1319
#define G6 1568

static const struct note n_spike[] = { { 196, 18 }, { 294, 26 }, { 0, 0 } };
static const struct note n_buy[] = { { C5, 35 }, { E5, 35 }, { G5, 55 }, { 0, 25 }, { E5, 35 }, { G5, 35 },
                                     { C6, 80 }, { 0, 0 } };
static const struct note n_hive[] = { { G4, 45 }, { B4, 45 }, { D5, 45 }, { G5, 110 }, { 0, 0 } };
static const struct note n_jack[] = { { A5, 30 }, { 831, 30 }, { G5, 30 }, { FS5, 30 }, { 698, 30 }, { E5, 30 },
                                      { 0, 30 }, { C5, 50 }, { E5, 50 }, { G5, 50 }, { C6, 70 }, { G5, 40 },
                                      { C6, 150 }, { 0, 0 } };
static const struct note n_ice[] = { { G6, 25 }, { 2093, 25 }, { G6, 25 }, { 2637, 50 }, { 0, 25 }, { 3136, 40 },
                                     { 2637, 40 }, { 3136, 90 }, { 0, 0 } };
static const struct note n_turn[] = { { A4, 55 }, { 554, 55 }, { E5, 80 }, { 0, 40 }, { D5, 55 }, { FS5, 55 },
                                      { A5, 120 }, { 0, 0 } };
static const struct note n_end[] = { { C5, 90 }, { E5, 90 }, { G5, 90 }, { C6, 160 }, { 0, 60 }, { A4, 80 },
                                     { C5, 80 }, { E5, 80 }, { A5, 160 }, { 0, 60 }, { 698, 80 }, { A5, 80 },
                                     { C6, 80 }, { 1397, 160 }, { 0, 40 }, { G5, 70 }, { C6, 70 }, { E6, 70 },
                                     { 2093, 250 }, { 0, 0 } };
static const struct note *const chords[] = { n_spike, n_buy, n_hive, n_jack, n_ice, n_turn, n_end };
static const struct note n_click[] = { { 1800, 6 }, { 0, 0 } };

unsigned char sound_on = 1;
static const struct note *at;
static unsigned char playing = 0xFF;    /* the chord sounding, 0xFF none */
static UInt32 due;

static void tone(unsigned short hz, unsigned char ms)
{
    SndCommandType c;
    UInt16 amp = PrefGetPreference(prefGameSoundVolume);
    if (!amp)
        return;
    c.cmd = sndCmdFrqOn;
    c.reserved = 0;
    c.param1 = hz;
    c.param2 = ms;
    c.param3 = amp;
    SndDoCmd(NULL, &c, true);
}

void snd_service(void)
{
    UInt32 now;
    if (!at)
        return;
    now = TimGetTicks();
    if ((Int32)(now - due) < 0)
        return;
    if (!at->ms) {
        at = 0;
        playing = 0xFF;
        return;
    }
    if (at->hz)
        tone(at->hz, at->ms);
    due = now + ((UInt32)at->ms * SysTicksPerSecond() + 999) / 1000;
    ++at;
}

void snd_init(void)
{
}

void play_chord(unsigned char n)
{
    if (!sound_on || n >= sizeof chords / sizeof chords[0])
        return;
    if (at && playing != 0xFF && playing > n)
        return;                         /* only a higher one cuts a chord off */
    at = chords[n];
    playing = n;
    due = 0;
    snd_service();
}

void snd_toggle(void)
{
    sound_on ^= 1;
    if (sound_on)
        play_chord(CHORD_HIVE);
    else
        snd_off();
}

void snd_click(void)
{
    if (!sound_on || at)
        return;
    at = n_click;
    playing = 0xFF;
    due = 0;
    snd_service();
}

void snd_off(void)
{
    SndCommandType c;
    at = 0;
    playing = 0xFF;
    c.cmd = sndCmdQuiet;
    c.reserved = 0;
    c.param1 = c.param2 = c.param3 = 0;
    SndDoCmd(NULL, &c, true);
}
