/**
 * sky.h - the live sky model: every object SkyPoint can show, in local ENU coordinates,
 * plus the camera/projection that maps the watch's orientation onto the screen.
 * Pure C++ (no Arduino), so it runs in the desktop simulator too.
 */
#pragma once
#include "astro.h"
#include <stdint.h>

enum ObjKind : uint8_t { OK_STAR, OK_PLANET, OK_MOON, OK_SUN, OK_DSO, OK_SAT, OK_NONE };

struct ObjRef {
    ObjKind kind = OK_NONE;
    uint16_t idx = 0;           // star index / astro::Body / Messier index / sat index
    bool operator==(const ObjRef &o) const { return kind == o.kind && idx == o.idx; }
    bool valid() const { return kind != OK_NONE; }
};

struct ObjInfo {
    char name[32];
    char kindText[32];          // "Red giant - Orion", "Planet", "Open cluster - Taurus"
    char line1[48], line2[48];  // extra info lines for the card
    double az, alt;             // degrees
    double mag;
    uint32_t color;             // 0xRRGGBB
};

struct Observer {
    double lat = 0, lon = 0, heightM = 0;
    bool valid = false;         // a real position (GPS or saved)
};

struct Camera {
    astro::Vec3 fwd{0, 1, 0}, up{0, 0, 1}, right{1, 0, 0}; // ENU unit vectors
    double fovDeg = 60;         // horizontal field of view
    int w = 410, h = 502, cx = 205, cy = 251;
    double f = 355;             // focal length in px
    void setFov(double fov);
    /* Project an ENU unit vector; returns false if behind the viewer / far off-screen. */
    bool project(const astro::Vec3 &enu, float &x, float &y, float margin = 40) const;
    /* Inverse: screen pixel -> ENU direction. */
    astro::Vec3 unproject(float x, float y) const;
    void boresightAzAlt(double &az, double &alt) const;
};

/* Sky state (single global instance). */
void skyBegin();
void skyUpdate(double unixTime, const Observer &obs);   // cheap when called often; heavy work is throttled
double skyJd();
const Observer &skyObserver();

/* Positions (ENU unit vectors) */
uint16_t skyStarCount();
const float *skyStarEnu(uint16_t i);                   // x,y,z
float skyStarMag(uint16_t i);
uint32_t skyStarColor(uint16_t i);
const char *skyStarName(uint16_t i);                   // "" if none
astro::Vec3 skyBodyEnu(astro::Body b);
const astro::BodyState &skyBodyState(astro::Body b);
uint32_t skyBodyColor(astro::Body b);
astro::Vec3 skyDsoEnu(uint8_t i);
astro::Vec3 skySunEnu();
bool skySatEnu(int i, astro::Vec3 &enu, double &rangeKm, int16_t &vis);

/* Object info for UI cards and lists */
bool skyObjInfo(const ObjRef &r, ObjInfo &out);
astro::Vec3 skyObjEnu(const ObjRef &r);
double skyObjMag(const ObjRef &r);
/* Nearest notable object to an ENU direction within maxDeg (prefers bright things). */
ObjRef skyPick(const astro::Vec3 &dir, double maxDeg);

/* Twilight / sun info for "tonight" */
struct NightInfo {
    double sunset, sunrise, duskAstro, dawnAstro;   // JD (0 = none in next 24h)
    double moonrise, moonset;
    double moonPhase, moonAgeDays;
    const char *moonPhaseName;
    double sunAlt;
};
NightInfo skyNight();
double skyRiseTime(const ObjRef &r);     // next rise (JD) within 24 h, 0 if none / circumpolar
double skySetTime(const ObjRef &r);

/* Constellations */
uint8_t skyConstellationOf(const astro::Vec3 &enu); // index of nearest centroid (approx.)
const char *skyConstellationName(uint8_t i);
