#include "sky.h"
#include "catalog_data.h"
#include "sats.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

using namespace astro;

/* ------------------------------------------------------------------ camera */
void Camera::setFov(double fov)
{
    fovDeg = fov;
    f = (w / 2.0) / tan(fov * 0.5 * D2R);
}

bool Camera::project(const Vec3 &v, float &x, float &y, float margin) const
{
    double z = dot(v, fwd);
    if (z < 0.05) return false;
    double px = cx + f * dot(v, right) / z;
    double py = cy - f * dot(v, up) / z;
    if (px < -margin || px > w + margin || py < -margin || py > h + margin) return false;
    x = (float)px; y = (float)py;
    return true;
}

Vec3 Camera::unproject(float x, float y) const
{
    double a = (x - cx) / f, b = -(y - cy) / f;
    return normalize({ fwd.x + a * right.x + b * up.x, fwd.y + a * right.y + b * up.y, fwd.z + a * right.z + b * up.z });
}

void Camera::boresightAzAlt(double &az, double &alt) const { enuToAzAlt(fwd, az, alt); }

/* ------------------------------------------------------------------ state */
static Mat3 g_prec;                 // J2000 -> equator of date
static float *g_starEnu = nullptr;
static uint32_t *g_starCol = nullptr;
static Observer g_obs;
static double g_jd = 0, g_lastBodies = -1, g_lastPrec = -1, g_lastNight = -1;
static double g_bodyRa[BODY_COUNT], g_bodyDec[BODY_COUNT];
static BodyState g_body[BODY_COUNT];
static Vec3 g_bodyEnu[BODY_COUNT];
static Vec3 g_dsoEq[CAT_NDSO], g_dsoEnu[CAT_NDSO];
static Mat3 g_eq2enu;
static NightInfo g_night;

static const uint32_t kBodyColor[BODY_COUNT] = { 0xFFD54A, 0xE8ECF2, 0xC9B9AE, 0xFFF1CC, 0xFF6A3D,
                                                 0xF4D7AE, 0xEBD58E, 0x9FE6E8, 0x7F9CFF };

static void precessAll(double jd)
{
    Mat3 P = precessionFromJ2000(jd);
    g_prec = P;
    for (uint8_t i = 0; i < CAT_NDSO; i++) g_dsoEq[i] = mul(P, radecToVec(CAT_DSO[i].ra, CAT_DSO[i].dec));
}

void skyBegin()
{
    if (g_starEnu) return;
    g_starEnu = (float *)malloc(sizeof(float) * 3 * CAT_NSTARS);
    g_starCol = (uint32_t *)malloc(sizeof(uint32_t) * CAT_NSTARS);
    for (uint16_t i = 0; i < CAT_NSTARS; i++) {
        uint8_t r, g, b;
        bvToRgb(CAT_STARS[i].ci100 / 100.0, r, g, b);
        g_starCol[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
    memset(&g_night, 0, sizeof(g_night));
}

static NightInfo computeNightFor(double jd, double la, double lo)
{
    NightInfo n;
    memset(&n, 0, sizeof(n));
    n.sunset = nextCrossing(SUN, jd, 1.0, la, lo, -0.833, false);
    n.sunrise = nextCrossing(SUN, jd, 1.0, la, lo, -0.833, true);
    n.duskAstro = nextCrossing(SUN, jd, 1.0, la, lo, -18, false);
    n.dawnAstro = nextCrossing(SUN, jd, 1.0, la, lo, -18, true);
    n.moonrise = nextCrossing(MOON, jd, 1.0, la, lo, 0.125, true);
    n.moonset = nextCrossing(MOON, jd, 1.0, la, lo, 0.125, false);
    n.moonPhase = computeBody(MOON, jd, la, lo, 0).phase;
    n.moonAgeDays = moonAgeDays(jd);
    n.moonPhaseName = moonPhaseName(jd);
    n.sunAlt = bodyAltitude(SUN, jd, la, lo);
    return n;
}

/* ------------------------------------------------------------------ background astronomy
 * Planets/Moon/Sun (every 20 s) and twilight + rise/set times (every 10 min) are pure
 * double-precision maths. On the watch they run in a low-priority task (second core on the
 * ESP32-S3) so they never stall a frame; on the desktop they run inline. */
struct AstroJob { double jd, lat, lon, h; bool night; };
struct AstroResult { BodyState body[BODY_COUNT]; NightInfo night; bool hasNight; double jd; };
static AstroResult g_res;
static volatile bool g_resReady = false, g_jobBusy = false;
static AstroJob g_job;

static void runJob(const AstroJob &j, AstroResult &r)
{
    for (int b = 0; b < BODY_COUNT; b++) r.body[b] = computeBody((Body)b, j.jd, j.lat, j.lon, j.h);
    r.hasNight = j.night;
    if (j.night) r.night = computeNightFor(j.jd, j.lat, j.lon);
    r.jd = j.jd;
}

#ifdef ARDUINO
static TaskHandle_t g_worker = nullptr;
static void workerTask(void *)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        AstroJob j = g_job;
        static AstroResult r;
        runJob(j, r);
        while (g_resReady) vTaskDelay(1);     // previous result not consumed yet
        g_res = r;
        g_resReady = true;
        g_jobBusy = false;
    }
}
#endif

static void submitJob(const AstroJob &j, bool blocking)
{
#ifdef ARDUINO
    if (!blocking) {
        if (g_jobBusy) return;
        if (!g_worker) {
            /* idle priority: never starves IDLE, so the task watchdog stays fed */
#if CONFIG_FREERTOS_UNICORE
            xTaskCreate(workerTask, "astro", 8192, nullptr, tskIDLE_PRIORITY, &g_worker);
#else
            xTaskCreatePinnedToCore(workerTask, "astro", 8192, nullptr, tskIDLE_PRIORITY, &g_worker, 0);
#endif
        }
        g_job = j;
        g_jobBusy = true;
        xTaskNotifyGive(g_worker);
        return;
    }
#endif
    static AstroResult r;
    runJob(j, r);
    g_res = r;
    g_resReady = true;
}

/* satellite positions, cached per update (SGP4 is costly in double precision) */
static Vec3 g_satEnu[SAT_MAX];
static double g_satRange[SAT_MAX];
static int16_t g_satVis[SAT_MAX];
static bool g_satOk[SAT_MAX];

void skyUpdate(double unixTime, const Observer &obs)
{
    bool moved = fabs(obs.lat - g_obs.lat) > 0.01 || fabs(obs.lon - g_obs.lon) > 0.01;
    g_obs = obs;
    g_jd = julianDate(unixTime);
    if (g_lastPrec < 0 || fabs(g_jd - g_lastPrec) > 30) { precessAll(g_jd); g_lastPrec = g_jd; }
    bool first = g_lastBodies < 0;
    if (first || moved || fabs(g_jd - g_lastBodies) > 20.0 / 86400.0) {
        bool night = first || moved || g_lastNight < 0 || fabs(g_jd - g_lastNight) > 10.0 / 1440.0;
        AstroJob j = { g_jd, obs.lat, obs.lon, obs.heightM, night };
        submitJob(j, first);                     // the very first time: inline, so the sky is complete
        g_lastBodies = g_jd;
        if (night) g_lastNight = g_jd;
    }
    if (g_resReady) {
        memcpy(g_body, g_res.body, sizeof(g_body));
        if (g_res.hasNight) g_night = g_res.night;
        g_resReady = false;
    }
    g_eq2enu = equatorialToEnu(g_jd, obs.lat, obs.lon);
    /* stars: J2000 int16 unit vectors -> precession -> local ENU in one matrix */
    Mat3 PE = mul(g_eq2enu, g_prec);
    float M[9];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) M[r * 3 + c] = (float)(PE.m[r][c] / 32767.0);
    for (uint16_t i = 0; i < CAT_NSTARS; i++) {
        const int16_t *v = CAT_VEC[i];
        float *o = g_starEnu + i * 3;
        o[0] = M[0] * v[0] + M[1] * v[1] + M[2] * v[2];
        o[1] = M[3] * v[0] + M[4] * v[1] + M[5] * v[2];
        o[2] = M[6] * v[0] + M[7] * v[1] + M[8] * v[2];
    }
    for (int b = 0; b < BODY_COUNT; b++) g_bodyEnu[b] = mul(g_eq2enu, radecToVec(g_body[b].ra, g_body[b].dec));
    for (uint8_t i = 0; i < CAT_NDSO; i++) g_dsoEnu[i] = mul(g_eq2enu, g_dsoEq[i]);
    satsSetSite(obs.lat, obs.lon, obs.heightM);
    for (int i = 0; i < satsCount() && i < SAT_MAX; i++) {
        SatNow s;
        g_satOk[i] = satCompute(i, g_jd, s);
        g_satEnu[i] = azAltToEnu(s.az, s.el);
        g_satRange[i] = s.rangeKm;
        g_satVis[i] = s.vis;
    }
}

double skyJd() { return g_jd; }
const Observer &skyObserver() { return g_obs; }
uint16_t skyStarCount() { return CAT_NSTARS; }
const float *skyStarEnu(uint16_t i) { return g_starEnu + i * 3; }
float skyStarMag(uint16_t i) { return CAT_STARS[i].mag100 / 100.0f; }
uint32_t skyStarColor(uint16_t i) { return g_starCol[i]; }
const char *skyStarName(uint16_t i) { return CAT_NAMES[CAT_STARS[i].name]; }
Vec3 skyBodyEnu(Body b) { return g_bodyEnu[b]; }
const BodyState &skyBodyState(Body b) { return g_body[b]; }
uint32_t skyBodyColor(Body b) { return kBodyColor[b]; }
Vec3 skyDsoEnu(uint8_t i) { return g_dsoEnu[i]; }
Vec3 skySunEnu() { return g_bodyEnu[SUN]; }
NightInfo skyNight() { return g_night; }

bool skySatEnu(int i, Vec3 &enu, double &rangeKm, int16_t &vis)
{
    if (i < 0 || i >= SAT_MAX || i >= satsCount() || !g_satOk[i]) return false;
    enu = g_satEnu[i];
    rangeKm = g_satRange[i];
    vis = g_satVis[i];
    return true;
}

/* ------------------------------------------------------------------ info */
static const char *conName(uint8_t c) { return c < CAT_NCONS ? CAT_CONS[c].name : ""; }

uint8_t skyConstellationOf(const Vec3 &enu)
{
    float best = -2, ex = (float)enu.x, ey = (float)enu.y, ez = (float)enu.z;
    uint8_t con = 255;
    for (uint16_t i = 0; i < CAT_NSTARS; i++) {
        const float *v = g_starEnu + i * 3;
        float d = v[0] * ex + v[1] * ey + v[2] * ez;
        if (d > best && CAT_STARS[i].con < CAT_NCONS) { best = d; con = CAT_STARS[i].con; }
    }
    return con;
}

const char *skyConstellationName(uint8_t i) { return conName(i); }

static const char *starColourWord(double bv)
{
    if (bv < -0.1) return "Blue star";
    if (bv < 0.15) return "Blue-white star";
    if (bv < 0.45) return "White star";
    if (bv < 0.8) return "Yellow star";
    if (bv < 1.35) return "Orange star";
    return "Red star";
}

Vec3 skyObjEnu(const ObjRef &r)
{
    switch (r.kind) {
    case OK_STAR: { const float *v = skyStarEnu(r.idx); return { v[0], v[1], v[2] }; }
    case OK_PLANET: case OK_MOON: case OK_SUN: return g_bodyEnu[r.idx];
    case OK_DSO: return g_dsoEnu[r.idx];
    case OK_SAT: { Vec3 e; double rk; int16_t vis; if (skySatEnu(r.idx, e, rk, vis)) return e; return { 0, 0, -1 }; }
    default: return { 0, 0, -1 };
    }
}

double skyObjMag(const ObjRef &r)
{
    switch (r.kind) {
    case OK_STAR: return skyStarMag(r.idx);
    case OK_PLANET: case OK_MOON: case OK_SUN: return g_body[r.idx].mag;
    case OK_DSO: return CAT_DSO[r.idx].mag100 / 100.0;
    case OK_SAT: return -1.5;
    default: return 99;
    }
}

static void fmtDist(char *b, size_t n, double km)
{
    if (km < 1e5) snprintf(b, n, "%.0f km", km);
    else if (km < 1e7) snprintf(b, n, "%.0fk km", km / 1000);
    else snprintf(b, n, "%.2f AU", km / 149597870.7);
}

bool skyObjInfo(const ObjRef &r, ObjInfo &o)
{
    memset(&o, 0, sizeof(o));
    if (!r.valid()) return false;
    Vec3 e = skyObjEnu(r);
    enuToAzAlt(e, o.az, o.alt);
    o.mag = skyObjMag(r);
    char d[24];
    switch (r.kind) {
    case OK_STAR: {
        const CatStar &s = CAT_STARS[r.idx];
        const char *nm = CAT_NAMES[s.name];
        const char *cn = s.con < CAT_NCONS ? CAT_CONS[s.con].abbr : "";
        if (nm[0]) snprintf(o.name, sizeof(o.name), "%s", nm);
        else if (s.bayer) snprintf(o.name, sizeof(o.name), "%s %s", CAT_GREEK[s.bayer], cn);
        else snprintf(o.name, sizeof(o.name), "HIP star in %s", cn);
        snprintf(o.kindText, sizeof(o.kindText), "%s \xC2\xB7 %s", starColourWord(s.ci100 / 100.0), conName(s.con));
        if (s.dist_pc > 0 && s.dist_pc < 10000)
            snprintf(o.line1, sizeof(o.line1), "MAG %.2f   %.0f LY", o.mag, s.dist_pc * 3.26156);
        else snprintf(o.line1, sizeof(o.line1), "MAG %.2f", o.mag);
        o.color = g_starCol[r.idx];
        break;
    }
    case OK_PLANET: {
        const BodyState &b = g_body[r.idx];
        snprintf(o.name, sizeof(o.name), "%s", bodyName((Body)r.idx));
        snprintf(o.kindText, sizeof(o.kindText), "Planet \xC2\xB7 in %s", conName(skyConstellationOf(e)));
        fmtDist(d, sizeof d, b.distKm);
        snprintf(o.line1, sizeof(o.line1), "MAG %.1f   %s   %.0f%% LIT", b.mag, d, b.phase * 100);
        snprintf(o.line2, sizeof(o.line2), "SIZE %.1f\"   %.0f\xC2\xB0 FROM SUN", b.angDiamArcsec, b.elongation);
        o.color = kBodyColor[r.idx];
        break;
    }
    case OK_MOON: {
        const BodyState &b = g_body[MOON];
        snprintf(o.name, sizeof(o.name), "Moon");
        snprintf(o.kindText, sizeof(o.kindText), "%s \xC2\xB7 %.0f%%", moonPhaseName(g_jd), b.phase * 100);
        snprintf(o.line1, sizeof(o.line1), "%.0f KM   AGE %.1f D", b.distKm, moonAgeDays(g_jd));
        snprintf(o.line2, sizeof(o.line2), "IN %s", conName(skyConstellationOf(e)));
        o.color = kBodyColor[MOON];
        break;
    }
    case OK_SUN:
        snprintf(o.name, sizeof(o.name), "Sun");
        snprintf(o.kindText, sizeof(o.kindText), "Our star \xC2\xB7 in %s", conName(skyConstellationOf(e)));
        snprintf(o.line1, sizeof(o.line1), "NEVER LOOK DIRECTLY AT IT");
        o.color = kBodyColor[SUN];
        break;
    case OK_DSO: {
        const CatDso &s = CAT_DSO[r.idx];
        if (s.name[0]) snprintf(o.name, sizeof(o.name), "M%u %s", s.m, s.name);
        else snprintf(o.name, sizeof(o.name), "Messier %u", s.m);
        snprintf(o.kindText, sizeof(o.kindText), "%s \xC2\xB7 %s", s.type, conName(skyConstellationOf(e)));
        snprintf(o.line1, sizeof(o.line1), "MAG %.1f", s.mag100 / 100.0);
        o.color = 0xB98CFF;
        break;
    }
    case OK_SAT: {
        SatNow s;
        satCompute(r.idx, g_jd, s);
        snprintf(o.name, sizeof(o.name), "%s", satName(r.idx));
        snprintf(o.kindText, sizeof(o.kindText), "Satellite \xC2\xB7 %.0f km up", s.altKm);
        snprintf(o.line1, sizeof(o.line1), "RANGE %.0f KM   %s", s.rangeKm,
                 s.vis > 0 ? "SUNLIT" : (s.vis == 0 ? "IN SHADOW" : (s.vis == -1 ? "DAYLIGHT" : "BELOW HORIZON")));
        o.color = 0x34F5C5;
        break;
    }
    default: return false;
    }
    if (!o.line2[0] || r.kind == OK_STAR || r.kind == OK_DSO || r.kind == OK_SAT || r.kind == OK_SUN)
        snprintf(o.line2, sizeof(o.line2), "ALT %.1f\xC2\xB0   AZ %.0f\xC2\xB0 %s", o.alt, o.az, compassPoint(o.az));
    return true;
}

ObjRef skyPick(const Vec3 &dir, double maxDeg)
{
    ObjRef best;
    double bestScore = 1e9, cosMax = cos(maxDeg * D2R);
    auto consider = [&](ObjKind k, uint16_t idx, const Vec3 &v, double mag, double bias) {
        double d = dot(v, dir);
        if (d < cosMax) return;
        double ang = acos(fmin(1.0, d)) * R2D;
        double score = ang / maxDeg + 0.06 * fmax(-2.0, fmin(6.0, mag)) - bias;
        if (score < bestScore) { bestScore = score; best.kind = k; best.idx = idx; }
    };
    float dx = (float)dir.x, dy = (float)dir.y, dz = (float)dir.z, cm = (float)cosMax;
    for (uint16_t i = 0; i < CAT_NSTARS; i++) {
        const CatStar &s = CAT_STARS[i];
        if (s.mag100 > 350 && !CAT_NAMES[s.name][0]) continue;
        const float *v = g_starEnu + i * 3;
        if (v[0] * dx + v[1] * dy + v[2] * dz < cm) continue;      // cheap float reject first
        consider(OK_STAR, i, { v[0], v[1], v[2] }, s.mag100 / 100.0, CAT_NAMES[s.name][0] ? 0.05 : 0);
    }
    for (int b = 0; b < BODY_COUNT; b++)
        consider(b == SUN ? OK_SUN : (b == MOON ? OK_MOON : OK_PLANET), b, g_bodyEnu[b], g_body[b].mag, 0.25);
    double dsoLim = maxDeg > 3 ? 6.5 : 9.5;     // faint deep-sky objects only when zoomed in
    for (uint8_t i = 0; i < CAT_NDSO; i++)
        if (CAT_DSO[i].mag100 / 100.0 <= dsoLim) consider(OK_DSO, i, g_dsoEnu[i], CAT_DSO[i].mag100 / 100.0, CAT_DSO[i].name[0] ? 0.05 : 0);
    for (int i = 0; i < satsCount(); i++) {
        Vec3 e; double rk; int16_t vis;
        if (skySatEnu(i, e, rk, vis) && e.z > 0) consider(OK_SAT, i, e, -2, 0.3);
    }
    return best;
}

double skyRiseTime(const ObjRef &r)
{
    const Observer &o = g_obs;
    switch (r.kind) {
    case OK_STAR: return nextCrossingFixed(CAT_STARS[r.idx].ra, CAT_STARS[r.idx].dec, g_jd, 1.0, o.lat, o.lon, -0.567, true);
    case OK_DSO: return nextCrossingFixed(CAT_DSO[r.idx].ra, CAT_DSO[r.idx].dec, g_jd, 1.0, o.lat, o.lon, -0.567, true);
    case OK_PLANET: return nextCrossing((Body)r.idx, g_jd, 1.0, o.lat, o.lon, -0.567, true);
    case OK_MOON: return nextCrossing(MOON, g_jd, 1.0, o.lat, o.lon, 0.125, true);
    case OK_SUN: return nextCrossing(SUN, g_jd, 1.0, o.lat, o.lon, -0.833, true);
    case OK_SAT: { SatPass p; return satNextPass(r.idx, g_jd, 10, p) ? p.jdStart : 0; }
    default: return 0;
    }
}

double skySetTime(const ObjRef &r)
{
    const Observer &o = g_obs;
    switch (r.kind) {
    case OK_STAR: return nextCrossingFixed(CAT_STARS[r.idx].ra, CAT_STARS[r.idx].dec, g_jd, 1.0, o.lat, o.lon, -0.567, false);
    case OK_DSO: return nextCrossingFixed(CAT_DSO[r.idx].ra, CAT_DSO[r.idx].dec, g_jd, 1.0, o.lat, o.lon, -0.567, false);
    case OK_PLANET: return nextCrossing((Body)r.idx, g_jd, 1.0, o.lat, o.lon, -0.567, false);
    case OK_MOON: return nextCrossing(MOON, g_jd, 1.0, o.lat, o.lon, 0.125, false);
    case OK_SUN: return nextCrossing(SUN, g_jd, 1.0, o.lat, o.lon, -0.833, false);
    case OK_SAT: { SatPass p; return satNextPass(r.idx, g_jd, 10, p) ? p.jdStop : 0; }
    default: return 0;
    }
}
