#include "sound.h"
#include <string.h>
#include <math.h>

#define RATE 16000
#define CHUNK 128

struct Tone { int16_t hz; int16_t ms; };
struct Voice {
    uint32_t ph, inc;
    int pos, total, fade, amp;
    bool on, rest, blip;
};

/* Sound designs: short, bright, synthetic - in keeping with the HUD look. */
static const Tone T_TAP[] = { { 2400, 14 } };
static const Tone T_KEY[] = { { 2100, 12 } };
static const Tone T_PAGE[] = { { 1500, 18 }, { 2100, 22 } };
static const Tone T_LOCK[] = { { 2600, 16 }, { 3400, 24 } };
static const Tone T_TICK[] = { { 1800, 14 } };
static const Tone T_FOUND[] = { { 1600, 60 }, { 0, 15 }, { 2000, 60 }, { 0, 15 }, { 2700, 140 } };
static const Tone T_ALIGN[] = { { 1300, 70 }, { 1950, 70 }, { 2600, 120 } };
static const Tone T_GPS[] = { { 1900, 60 }, { 0, 20 }, { 2500, 60 }, { 0, 20 }, { 3200, 160 } };
static const Tone T_ALERT[] = { { 2000, 110 }, { 0, 60 }, { 2000, 110 }, { 0, 60 }, { 2700, 260 } };
static const Tone T_BOOT[] = { { 700, 60 }, { 1000, 60 }, { 1400, 60 }, { 1900, 70 }, { 2600, 180 } };
static const Tone T_ERROR[] = { { 520, 140 }, { 0, 40 }, { 380, 220 } };
static const Tone T_OK[] = { { 1800, 70 }, { 2700, 130 } };
static const Tone T_ZOOM[] = { { 1700, 14 }, { 2300, 18 } };

struct Seq { const Tone *t; uint8_t n; bool blip; };
static const Seq kSeq[] = {
    { T_TAP, 1, true },   { T_KEY, 1, true },   { T_PAGE, 2, true },   { T_LOCK, 2, true },  { T_TICK, 1, true },
    { T_FOUND, 5, false }, { T_ALIGN, 3, false }, { T_GPS, 5, false }, { T_ALERT, 5, false }, { T_BOOT, 5, false },
    { T_ERROR, 3, false }, { T_OK, 2, false },  { T_ZOOM, 2, true },
};

static int16_t g_sin[256];
static volatile uint8_t g_vol = 60;
static bool g_ready = false;

#ifdef ARDUINO
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
static QueueHandle_t g_q = nullptr;

static Voice vA, vB;               // A: tunes, B: blips on top
static Tone fifoA[16], fifoB[8];
static int nA = 0, nB = 0;

static void voiceStart(Voice &v, const Tone &t, bool blip)
{
    v.total = RATE * t.ms / 1000;
    if (v.total < 32) v.total = 32;
    v.pos = 0; v.ph = 0; v.on = true; v.rest = t.hz <= 0; v.blip = blip;
    v.inc = t.hz > 0 ? (uint32_t)(((uint64_t)t.hz << 32) / RATE) : 0;
    v.fade = blip ? 16 : (v.total / 4 < 64 ? v.total / 4 : 64);
    v.amp = blip ? 22000 : 15000;
}

static inline int voiceNext(Voice &v)
{
    int f = v.pos, env = 256;
    if (f < v.fade) env = f * 256 / v.fade;
    else if (v.blip) env = (v.total - f) * 256 / (v.total - v.fade);
    else if (f > v.total - v.fade) env = (v.total - f) * 256 / v.fade;
    int s = v.rest ? 0 : (((int32_t)g_sin[v.ph >> 24] * v.amp >> 15) * env >> 8);
    v.ph += v.inc;
    if (++v.pos >= v.total) v.on = false;
    return s;
}

static void audioTask(void *)
{
    static int16_t buf[CHUNK];
    bool ampOn = false;
    uint32_t lastSound = 0;
    for (;;) {
        uint8_t id;
        bool busy = vA.on || vB.on || nA || nB;
        /* idle: block until something is requested; keep the amp on for 3 s after a sound */
        TickType_t wait = busy ? 0 : (ampOn ? pdMS_TO_TICKS(50) : portMAX_DELAY);
        while (xQueueReceive(g_q, &id, wait) == pdTRUE) {
            wait = 0;
            if (id >= sizeof(kSeq) / sizeof(kSeq[0]) || g_vol == 0) continue;
            const Seq &s = kSeq[id];
            if (s.blip) { nB = 0; for (int i = 0; i < s.n && nB < 8; i++) fifoB[nB++] = s.t[i]; vB.on = false; }
            else { nA = 0; for (int i = 0; i < s.n && nA < 16; i++) fifoA[nA++] = s.t[i]; vA.on = false; }
            if (!ampOn) { sndHwAmp(true); ampOn = true; vTaskDelay(pdMS_TO_TICKS(6)); }
            lastSound = millis();
        }
        busy = vA.on || vB.on || nA || nB;
        if (!busy) {
            if (ampOn && millis() - lastSound > 3000) { sndHwAmp(false); ampOn = false; }
            continue;
        }
        int vol = g_vol;
        for (int i = 0; i < CHUNK; i++) {
            if (!vA.on && nA) { voiceStart(vA, fifoA[0], false); for (int k = 1; k < nA; k++) fifoA[k - 1] = fifoA[k]; nA--; }
            if (!vB.on && nB) { voiceStart(vB, fifoB[0], true); for (int k = 1; k < nB; k++) fifoB[k - 1] = fifoB[k]; nB--; }
            int s = 0;
            if (vA.on) s += voiceNext(vA);
            if (vB.on) s += voiceNext(vB);
            s = s * vol * vol / 10000;                 // perceptual-ish curve
            if (s > 32767) s = 32767;
            if (s < -32767) s = -32767;
            buf[i] = (int16_t)s;
        }
        sndHwWrite(buf, CHUNK);
        lastSound = millis();
    }
}

void sndBegin()
{
    for (int i = 0; i < 256; i++) g_sin[i] = (int16_t)(32767 * sin(i * 2 * 3.14159265358979 / 256));
    if (!sndHwBegin(RATE)) { Serial.println("[sound] no audio output"); return; }
    g_q = xQueueCreate(8, 1);
#if CONFIG_FREERTOS_UNICORE
    xTaskCreate(audioTask, "audio", 4096, nullptr, 3, nullptr);
#else
    xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 3, nullptr, 0);
#endif
    g_ready = true;
    Serial.println("[sound] ready");
}

void sndPlay(Sfx s)
{
    if (!g_ready || g_vol == 0) return;
    uint8_t id = s;
    xQueueSend(g_q, &id, 0);
}
#else
void sndBegin() {}
void sndPlay(Sfx) {}
#endif

void sndSetVolume(uint8_t v) { g_vol = v > 100 ? 100 : v; }
uint8_t sndVolume() { return g_vol; }
