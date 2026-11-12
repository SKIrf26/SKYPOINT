/**
 * device.h - hardware abstraction for SkyPoint. device.cpp implements it on the
 * T-Watch Ultra; tools/simulator provides a desktop stand-in.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "sky.h"

struct Settings {
    int16_t utcOffsetMin = -360;   // local time offset shown in the UI (default US Central)
    bool autoDst = true;           // US DST rule on top of utcOffsetMin
    uint8_t pointMode = 0;         // 0 = viewfinder (look "through" the watch), 1 = pointer (12 o'clock edge)
    uint8_t axisRot = 0;           // 0..3: sensor mounting fix, x90 deg about the screen normal
    bool night = false;            // red night-vision palette
    bool constellations = true;
    bool grid = false;
    uint8_t labels = 1;            // 0 few, 1 normal, 2 lots
    uint8_t brightness = 200;
    uint8_t fovIndex = 1;          // 0 = 90, 1 = 60, 2 = 35, 3 = 20 deg
    bool passAlerts = true;
    bool belowHorizon = true;      // show objects under the ground, dimmed
    uint8_t volume = 60;           // UI sounds, 0 = off
    bool haptics = true;           // vibration (boards with a motor)
};

struct GpsStatus {
    bool online = false;           // receiver answered
    bool fix = false;
    int sats = 0;
    double hdop = 0;
    bool timeFromGps = false;
};

/* Detailed GNSS view for the GPS page (boards without GPS return false / 0). */
struct GpsSat { int16_t az, el, cn0; bool used; uint8_t sys; };   // sys: 1 GPS 2 GLONASS 3 BeiDou 4 Galileo 5 QZSS
struct GpsDetail {
    bool online, fix;
    int used, inView;
    double hdop, altM, lat, lon;
    uint32_t ttffMs;       // time to first fix (0 = not yet)
    uint32_t searchMs;     // how long it has been searching
};
bool devGpsDetail(GpsDetail &d);
int devGpsSats(GpsSat *out, int max);

void devBegin();
void devLoop();

double devUnixTime();              // UTC seconds (fractional)
bool devTimeValid();
Observer devObserver();
GpsStatus devGps();

/* Latest device->world quaternion (w,x,y,z), normalised. False when the IMU is offline. */
bool devQuat(float q[4], uint32_t *ageMs = nullptr);
bool devAccel(float a[3]);

enum Haptic : uint8_t { H_TICK = 1, H_CLICK = 2, H_LOCK = 3, H_ALERT = 4, H_SOFT = 5 };
void devHaptic(Haptic h);

Settings &devSettings();
void devSaveSettings();
void devApplyBrightness();

int devBatteryPct();
bool devCharging();

/* TLE refresh over Wi-Fi (credentials in /skypoint/wifi.txt on the SD card). */
enum TleState : uint8_t { TLE_IDLE, TLE_RUNNING, TLE_OK, TLE_FAIL };
void devStartTleUpdate();
TleState devTleState();
const char *devTleMsg();

bool devSdReady();
void devExitToLauncher();

/* Board capabilities and status text */
bool devHasLauncher();               // running under bmorcelli's Launcher (T-Watch Ultra)
bool devHasGps();
bool devHasHaptics();
void devStatusLine(char *b, size_t n);   // short line under the clock on the sky view
void devStatusDetail(char *b, size_t n); // multi-line block on the setup page
const char *devNoLocationHint();

/* Wi-Fi credentials (stored in NVS) and manual location */
int devWifiScan(char (*names)[33], int max);   // blocking, a few seconds
void devWifiSetCreds(const char *ssid, const char *pass);
bool devWifiHasCreds();
const char *devWifiSsid();
void devSetManualLocation(double lat, double lon, const char *name);
const char *devLocationName();        // "GPS", city name, or ""
/* Sync over Wi-Fi: orbits always; on boards without GPS also time + IP location. */
#define devStartSync devStartTleUpdate
