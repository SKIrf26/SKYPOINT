// SkyPoint desktop simulator: real LVGL + real UI/sky code, device layer faked.
// Renders screens at 410x502 to RAW RGB565 for tools/simulator/topng.py.
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <chrono>
#include <malloc.h>
// TIMING
#include "../../SkyPoint/device.h"
#include "../../SkyPoint/ui.h"
#include "../../SkyPoint/orient.h"
#include "../../SkyPoint/skyview.h"
#include "../../SkyPoint/sats.h"
#include "../../SkyPoint/theme.h"

using namespace astro;

static uint32_t g_ms = 1000;
static uint32_t tickcb() { return g_ms; }
static double g_unix = 0;
static Settings g_set;
static Observer g_obs;
static float g_q[4] = { 1, 0, 0, 0 };

/* ---- device stubs */
void devBegin() {}
void devLoop() {}
double devUnixTime() { return g_unix + (g_ms - 1000) / 1000.0; }
bool devTimeValid() { return true; }
Observer devObserver() { return g_obs; }
GpsStatus devGps() { GpsStatus s; s.online = true; s.fix = true; s.sats = 11; s.hdop = 0.9; s.timeFromGps = true; return s; }
bool devQuat(float q[4], uint32_t *age) { memcpy(q, g_q, sizeof g_q); if (age) *age = 0; return true; }
bool devAccel(float a[3]) { return false; }
void devHaptic(Haptic) {}
Settings &devSettings() { return g_set; }
void devSaveSettings() {}
void devApplyBrightness() {}
int devBatteryPct() { return 87; }
bool devCharging() { return false; }
void devStartTleUpdate() {}
TleState devTleState() { return TLE_IDLE; }
const char *devTleMsg() { return "Add /skypoint/wifi.txt to the SD card"; }
bool devSdReady() { return true; }
void devExitToLauncher() {}
bool devHasLauncher() { return true; }
bool devHasGps() { return true; }
bool devHasHaptics() { return true; }
void devStatusLine(char *b, size_t n) { snprintf(b, n, "GPS 11  \xC2\xB7  87%%"); }
void devStatusDetail(char *b, size_t n) { snprintf(b, n, "GPS FIX  \xC2\xB7  11 SATS  \xC2\xB7  HDOP 0.9\nPOS 41.8781\xC2\xB0, -87.6298\xC2\xB0  180 M\nTIME GPS  \xC2\xB7  WI-FI HOME"); }
const char *devNoLocationHint() { return "GO OUTSIDE FOR A GPS FIX"; }
int devWifiScan(char (*names)[33], int max) { const char *n[] = { "HOME", "HOME-5G", "Neighbor" }; for (int i = 0; i < 3; i++) strcpy(names[i], n[i]); return 3; }
void devWifiSetCreds(const char *, const char *) {}
bool devWifiHasCreds() { return true; }
const char *devWifiSsid() { return "HOME"; }
void devSetManualLocation(double lat, double lon, const char *) { g_obs.lat = lat; g_obs.lon = lon; g_obs.valid = true; }
const char *devLocationName() { return "GPS"; }
static bool g_simFix = true;
bool devGpsDetail(GpsDetail &d)
{
    memset(&d, 0, sizeof(d));
    d.online = true; d.fix = g_simFix; d.used = g_simFix ? 9 : 0; d.inView = 14; d.hdop = 0.9; d.altM = 182;
    d.lat = 41.8781; d.lon = -87.6298; d.ttffMs = g_simFix ? 42000 : 0;
    return true;
}
int devGpsSats(GpsSat *out, int max)
{
    static const int16_t S[][4] = { { 40, 62, 44, 1 }, { 120, 35, 38, 1 }, { 200, 71, 46, 1 }, { 265, 22, 31, 1 }, { 310, 48, 41, 1 },
                                    { 15, 15, 24, 0 }, { 165, 12, 18, 0 }, { 90, 55, 42, 1 }, { 230, 30, 35, 1 }, { 340, 8, 0, 0 },
                                    { 60, 28, 33, 1 }, { 285, 66, 40, 1 }, { 140, 5, 0, 0 }, { 185, 40, 27, 0 } };
    int n = 0;
    for (auto &r : S) {
        if (n >= max) break;
        out[n].az = r[0]; out[n].el = r[1]; out[n].cn0 = r[2]; out[n].used = g_simFix && r[3]; out[n].sys = 1; n++;
    }
    return n;
}

/* camera pose -> quaternion (viewfinder: device -Z = forward, +Y = up) */
static void poseQuat(double az, double alt, double roll)
{
    Vec3 f = azAltToEnu(az, alt);
    Vec3 worldUp = { 0, 0, 1 };
    Vec3 r = normalize(cross(f, worldUp));
    if (norm(cross(f, worldUp)) < 1e-6) r = { 1, 0, 0 };
    Vec3 u = cross(r, f);
    double c = cos(roll * D2R), s = sin(roll * D2R);
    Vec3 r2 = { r.x * c + u.x * s, r.y * c + u.y * s, r.z * c + u.z * s };
    Vec3 u2 = { u.x * c - r.x * s, u.y * c - r.y * s, u.z * c - r.z * s };
    /* columns: device X -> r2, device Y -> u2, device Z -> -f */
    double m[3][3] = { { r2.x, u2.x, -f.x }, { r2.y, u2.y, -f.y }, { r2.z, u2.z, -f.z } };
    double tr = m[0][0] + m[1][1] + m[2][2], w, x, y, z;
    if (tr > 0) {
        double S = sqrt(tr + 1.0) * 2; w = 0.25 * S;
        x = (m[2][1] - m[1][2]) / S; y = (m[0][2] - m[2][0]) / S; z = (m[1][0] - m[0][1]) / S;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        double S = sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2; w = (m[2][1] - m[1][2]) / S;
        x = 0.25 * S; y = (m[0][1] + m[1][0]) / S; z = (m[0][2] + m[2][0]) / S;
    } else if (m[1][1] > m[2][2]) {
        double S = sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2; w = (m[0][2] - m[2][0]) / S;
        x = (m[0][1] + m[1][0]) / S; y = 0.25 * S; z = (m[1][2] + m[2][1]) / S;
    } else {
        double S = sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2; w = (m[1][0] - m[0][1]) / S;
        x = (m[0][2] + m[2][0]) / S; y = (m[1][2] + m[2][1]) / S; z = 0.25 * S;
    }
    g_q[0] = w; g_q[1] = x; g_q[2] = y; g_q[3] = z;
}

/* ---- display */
static uint16_t fb[SCR_W * SCR_H];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    uint16_t *p = (uint16_t *)px;
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) fb[y * SCR_W + x] = *p++;
    lv_display_flush_ready(d);
}
static void pump(int n = 10) { for (int i = 0; i < n; i++) { g_ms += 34; uiLoop(); lv_timer_handler(); } }
static void snap(const char *name)
{
    pump(3);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    char p[160];
    snprintf(p, sizeof p, "shots/%s.raw", name);
    FILE *f = fopen(p, "wb");
    fwrite(fb, 2, SCR_W * SCR_H, f);
    fclose(f);
    printf("shot %s\n", name);
}
static void look(double az, double alt, double roll = 0, int settle = 20)
{
    poseQuat(az, alt, roll);
    pump(settle);
}

extern bool g_nightMode;

int main(int argc, char **argv)
{
    /* Default scene: Chicago, 2026-11-11 05:30 UTC (Nov 10, 23:30 CST) */
    g_unix = argc > 1 ? atof(argv[1]) : 1794375000.0;
    g_obs.lat = 41.8781; g_obs.lon = -87.6298; g_obs.heightM = 180; g_obs.valid = true;
    satsBegin();
    lv_init();
    lv_tick_set_cb(tickcb);
    lv_display_t *d = lv_display_create(SCR_W, SCR_H);
#ifdef SIM_PARTIAL   /* ESP32-C6 style: 48-line stripes */
    static uint16_t buf[SCR_W * 48];
    lv_display_set_buffers(d, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
#else
    static uint16_t buf[SCR_W * SCR_H];
    lv_display_set_buffers(d, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_FULL);
#endif
    lv_display_set_flush_cb(d, flush);

    struct mallinfo2 M0 = mallinfo2();
    auto T0 = std::chrono::steady_clock::now();
    uiBegin();
    struct mallinfo2 M1 = mallinfo2();
    fprintf(stderr, "heap used by uiBegin: %zu KB\n", (M1.uordblks - M0.uordblks) / 1024);
    auto T1 = std::chrono::steady_clock::now();
    fprintf(stderr, "uiBegin %.1f ms\n", std::chrono::duration<double, std::milli>(T1 - T0).count());
    pump(36);
    snap("00_intro");
    pump(60);
    look(0, 30);
    orientAlignAzimuth(0, 0, 0);          // pretend the user aligned already
    orientMarkAlignTime((uint32_t)devUnixTime());

    /* find interesting directions from the live sky */
    skyUpdate(devUnixTime(), g_obs);
    double maz, malt, jaz, jalt;
    enuToAzAlt(skyBodyEnu(MOON), maz, malt);
    enuToAzAlt(skyBodyEnu(JUPITER), jaz, jalt);
    printf("Moon az %.1f alt %.1f phase %.2f | Jupiter az %.1f alt %.1f\n", maz, malt, skyBodyState(MOON).phase, jaz, jalt);

    double saz, salt;
    enuToAzAlt(skyBodyEnu(SATURN), saz, salt);
    if (jalt < 15) { jaz = saz; jalt = salt; }
    look(jaz + 1.2, jalt + 0.8, 4);
    { auto A = std::chrono::steady_clock::now();
      for (int i = 0; i < 20; i++) { g_ms += 34; uiLoop(); lv_refr_now(NULL); }
      auto B = std::chrono::steady_clock::now();
      fprintf(stderr, "sky frame %.2f ms\n", std::chrono::duration<double, std::milli>(B - A).count() / 20); }
    snap("01_sky_lock_planet");
    /* Orion region (Betelgeuse) */
    for (uint16_t i = 0; i < skyStarCount(); i++)
        if (!strcmp(skyStarName(i), "Betelgeuse")) {
            const float *p = skyStarEnu(i);
            double az, alt;
            enuToAzAlt({ p[0], p[1], p[2] }, az, alt);
            printf("Betelgeuse az %.1f alt %.1f\n", az, alt);
            look(az + 6, alt - 4, -8);
            snap("02_sky_orion");
            break;
        }
    look(20, 18, 0);
    g_set.fovIndex = 0;
    skyviewSetFovLabel("90\xC2\xB0");
    pump(5);
    snap("03_sky_wide_horizon");
    g_set.fovIndex = 1;
    skyviewSetFovLabel("60\xC2\xB0");

    /* find mode toward the ISS position / moon */
    ObjRef moon; moon.kind = OK_MOON; moon.idx = MOON;
    skyviewSetTarget(moon);
    look(maz + 120, 25, 0);
    snap("04_sky_find_moon");
    skyviewSetTarget(ObjRef());
    look(maz, malt, 0);
    snap("05_sky_moon");

    /* other tiles: swipe programmatically */
    extern void uiButton(int);
    uiButton(1);
    pump(30);
    snap("06_tonight");
    lv_obj_t *tv = lv_obj_get_child(lv_screen_active(), 0);
    lv_tileview_set_tile_by_index(tv, 2, 0, LV_ANIM_OFF);
    lv_obj_send_event(tv, LV_EVENT_VALUE_CHANGED, NULL);
    pump(30);
    snap("07_orbit");
    lv_tileview_set_tile_by_index(tv, 3, 0, LV_ANIM_OFF);
    lv_obj_send_event(tv, LV_EVENT_VALUE_CHANGED, NULL);
    pump(30);
    snap("10_gps_locked");
    g_simFix = false;
    pump(40);
    snap("11_gps_searching");
    g_simFix = true;
    lv_tileview_set_tile_by_index(tv, 4, 0, LV_ANIM_OFF);
    lv_obj_send_event(tv, LV_EVENT_VALUE_CHANGED, NULL);
    pump(30);
    snap("08_setup");

    /* big-key password keyboard */
    extern void uiDebugKeyboard(const char *, const char *, int);
    uiDebugKeyboard("HOME-5G", "starlight", 0);
    pump(5);
    snap("12_keyboard");
    extern void uiRebuild();
    uiRebuild();
    pump(5);

    /* night vision */
    g_set.night = true; g_nightMode = true;
    uiRebuild();
    lv_obj_t *tv2 = lv_obj_get_child(lv_screen_active(), 0);
    lv_tileview_set_tile_by_index(tv2, 0, 0, LV_ANIM_OFF);
    lv_obj_send_event(tv2, LV_EVENT_VALUE_CHANGED, NULL);
    look(jaz - 10, jalt + 5, 0);
    snap("09_sky_night");
    extern int g_skyCmdPeak;
    printf("peak draw commands per frame: %d\n", g_skyCmdPeak);
    puts("done");
    return 0;
}
