/**
 * sound.h - tiny UI sound engine (beeps, blips, chimes).
 *
 * Two voices mixed in integer maths (a tune voice and a blip voice that plays on top, so a
 * tap is heard at once even during a chime), 16 kHz mono. The board layer supplies the
 * output: ES8311 codec on the Waveshare C6 (after HUNTR-SEEKR), MAX98357A amp on the
 * T-Watch Ultra.
 */
#pragma once
#include <stdint.h>

enum Sfx : uint8_t {
    SFX_TAP,        // any touch press
    SFX_KEY,        // keyboard key
    SFX_PAGE,       // page swipe
    SFX_LOCK,       // reticle locked onto something
    SFX_TICK,       // find mode: 10 deg closer
    SFX_FOUND,      // find mode: target in the reticle
    SFX_ALIGN,      // heading aligned
    SFX_GPS,        // GPS lock acquired
    SFX_ALERT,      // ISS pass alert
    SFX_BOOT,       // intro sweep
    SFX_ERROR,      // something failed
    SFX_OK,         // something succeeded (sync done)
    SFX_ZOOM,       // FOV change
};

void sndBegin();                 // starts the audio task (board backend must be ready)
void sndPlay(Sfx s);
void sndSetVolume(uint8_t v);    // 0..100, 0 = silent (amplifier stays off)
uint8_t sndVolume();

/* ---- board backend, implemented in device.cpp / device_c6.cpp ---- */
bool sndHwBegin(uint32_t rate);              // set up the codec/amp + I2S
void sndHwWrite(const int16_t *mono, int n); // blocking write of n mono samples
void sndHwAmp(bool on);                      // amplifier power (quiet when idle)
