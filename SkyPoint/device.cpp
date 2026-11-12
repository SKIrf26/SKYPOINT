/**
 * device.cpp - T-Watch Ultra implementation of device.h:
 * BHI260AP orientation, MIA-M10Q GNSS time + position, PCF85063 RTC, DRV2605 haptics,
 * settings in NVS, TLE files on the SD card and an optional Wi-Fi TLE refresh.
 */
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <Preferences.h>
#include <SD.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <bosch/BoschSensorDataHelper.hpp>
#include <esp_sleep.h>
#include <sys/time.h>
#include "device.h"
#include "sats.h"
#include "astro.h"
#include "ui.h"
#include "netsync.h"
#include "sound.h"
#include <ESP_I2S.h>        // same driver family LilyGoLib uses on the Ultra

static Preferences g_prefs;
static Settings g_set;
static SensorQuaternion *g_quat = nullptr;
static SensorXYZ *g_acc = nullptr;
static float g_q[4] = { 1, 0, 0, 0 }, g_a[3] = { 0, 0, 1 };
static bool g_haveQ = false, g_haveA = false, g_newA = false;
static uint32_t g_qMs = 0;
static Observer g_obs;
static bool g_timeFromGps = false;
static uint32_t g_lastObsSave = 0;
static volatile TleState g_tleState = TLE_IDLE;
static char g_tleMsg[96] = "";
static char *g_tlePending = nullptr;   // filled by the Wi-Fi task, consumed in devLoop
static volatile bool g_tleReady = false;

static char g_ssid[33] = "", g_pass[65] = "", g_locName[32] = "";
static bool g_manualLoc = false, g_sdWifi = false, g_sdReady = false;
static const char *kDir = "/skypoint";
static const char *kTleFile = "/skypoint/tle.txt";
static const char *kWifiFile = "/skypoint/wifi.txt";

/* ------------------------------------------------------------------ settings */
static void loadSettings()
{
    g_prefs.begin("skypoint", true);
    g_set.utcOffsetMin = g_prefs.getShort("utc", g_set.utcOffsetMin);
    g_set.autoDst = g_prefs.getBool("dst", g_set.autoDst);
    g_set.pointMode = g_prefs.getUChar("mode", g_set.pointMode);
    g_set.axisRot = g_prefs.getUChar("axis", g_set.axisRot);
    g_set.night = g_prefs.getBool("night", g_set.night);
    g_set.constellations = g_prefs.getBool("const", g_set.constellations);
    g_set.grid = g_prefs.getBool("grid", g_set.grid);
    g_set.labels = g_prefs.getUChar("labels", g_set.labels);
    g_set.brightness = g_prefs.getUChar("bright", g_set.brightness);
    g_set.fovIndex = g_prefs.getUChar("fov", g_set.fovIndex);
    g_set.passAlerts = g_prefs.getBool("alerts", g_set.passAlerts);
    g_set.belowHorizon = g_prefs.getBool("below", g_set.belowHorizon);
    g_set.volume = g_prefs.getUChar("vol", g_set.volume);
    g_set.haptics = g_prefs.getBool("hapt", g_set.haptics);
    g_prefs.getString("ssid", g_ssid, sizeof g_ssid);
    g_prefs.getString("pass", g_pass, sizeof g_pass);
    g_manualLoc = g_prefs.getBool("mloc", false);
    g_prefs.getString("locname", g_locName, sizeof g_locName);
    if (g_prefs.isKey("lat")) {
        g_obs.lat = g_prefs.getDouble("lat", 0);
        g_obs.lon = g_prefs.getDouble("lon", 0);
        g_obs.heightM = g_prefs.getDouble("hgt", 0);
        g_obs.valid = true;
    }
    g_prefs.end();
}

void devSaveSettings()
{
    g_prefs.begin("skypoint", false);
    g_prefs.putShort("utc", g_set.utcOffsetMin);
    g_prefs.putBool("dst", g_set.autoDst);
    g_prefs.putUChar("mode", g_set.pointMode);
    g_prefs.putUChar("axis", g_set.axisRot);
    g_prefs.putBool("night", g_set.night);
    g_prefs.putBool("const", g_set.constellations);
    g_prefs.putBool("grid", g_set.grid);
    g_prefs.putUChar("labels", g_set.labels);
    g_prefs.putUChar("bright", g_set.brightness);
    g_prefs.putUChar("fov", g_set.fovIndex);
    g_prefs.putBool("alerts", g_set.passAlerts);
    g_prefs.putBool("below", g_set.belowHorizon);
    g_prefs.putUChar("vol", g_set.volume);
    g_prefs.putBool("hapt", g_set.haptics);
    g_prefs.end();
}

static void saveObserver()
{
    g_prefs.begin("skypoint", false);
    g_prefs.putDouble("lat", g_obs.lat);
    g_prefs.putDouble("lon", g_obs.lon);
    g_prefs.putDouble("hgt", g_obs.heightM);
    g_prefs.putBool("mloc", g_manualLoc);
    g_prefs.putString("locname", g_locName);
    g_prefs.end();
}

Settings &devSettings() { return g_set; }

void devApplyBrightness() { instance.setBrightness(g_set.brightness); }

/* ------------------------------------------------------------------ TLE storage */
static void loadStoredTles()
{
    satsBegin();
    if (instance.isCardReady() && SD.exists(kTleFile)) {
        File f = SD.open(kTleFile, FILE_READ);
        if (f) {
            String s = f.readString();
            f.close();
            int n = satsLoadTleText(s.c_str());
            Serial.printf("[SkyPoint] %d TLEs from SD\n", n);
            return;
        }
    }
    g_prefs.begin("skypoint", true);
    String s = g_prefs.getString("tle", "");
    g_prefs.end();
    if (s.length()) satsLoadTleText(s.c_str());
}

static void storeTles()
{
    static char buf[1400];
    int n = satsSerialize(buf, sizeof buf);
    if (n <= 0) return;
    g_prefs.begin("skypoint", false);
    g_prefs.putString("tle", buf);
    g_prefs.end();
    if (instance.isCardReady()) {
        if (!SD.exists(kDir)) SD.mkdir(kDir);
        File f = SD.open(kTleFile, FILE_WRITE);
        if (f) { f.write((const uint8_t *)buf, n); f.close(); }
    }
}

static void tleTask(void *)
{
    char ssid[64] = "", pass[65] = "";
    if (instance.isCardReady() && SD.exists(kWifiFile)) {
        File f = SD.open(kWifiFile, FILE_READ);
        if (f) {
            String a = f.readStringUntil('\n'), b = f.readStringUntil('\n');
            f.close();
            a.trim(); b.trim();
            strlcpy(ssid, a.c_str(), sizeof ssid);
            strlcpy(pass, b.c_str(), sizeof pass);
        }
    }
    if (!ssid[0] && g_ssid[0]) { strlcpy(ssid, g_ssid, sizeof ssid); strlcpy(pass, g_pass, sizeof pass); }
    if (!ssid[0]) {
        snprintf(g_tleMsg, sizeof g_tleMsg, "Set up Wi-Fi first (Setup > Wi-Fi)");
        g_tleState = TLE_FAIL;
        vTaskDelete(nullptr);
        return;
    }
    bool ok = false;
    String all;
    if (netConnect(ssid, pass, 20000, g_tleMsg, sizeof g_tleMsg)) {
        snprintf(g_tleMsg, sizeof g_tleMsg, "Downloading orbits...");
        ok = netFetchTles(all);
    }
    netDisconnect();
    if (ok) {
        g_tlePending = strdup(all.c_str());
        g_tleReady = true;
    } else {
        snprintf(g_tleMsg, sizeof g_tleMsg, "Update failed: no Wi-Fi or CelesTrak unreachable");
        g_tleState = TLE_FAIL;
    }
    vTaskDelete(nullptr);
}

void devStartTleUpdate()
{
    if (g_tleState == TLE_RUNNING) return;
    g_tleState = TLE_RUNNING;
    snprintf(g_tleMsg, sizeof g_tleMsg, "Starting Wi-Fi...");
    xTaskCreatePinnedToCore(tleTask, "tle", 10240, nullptr, 1, nullptr, 0);
}

TleState devTleState() { return g_tleState; }
const char *devTleMsg() { return g_tleMsg; }
bool devSdReady() { return g_sdReady; }

/* ------------------------------------------------------------------ time / GPS */
double devUnixTime()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

bool devTimeValid() { return devUnixTime() > 1735689600.0; }   // after 2025-01-01

Observer devObserver() { return g_obs; }

GpsStatus devGps()
{
    GpsStatus s;
    s.online = instance.getDeviceProbe() & HW_GPS_ONLINE;
    s.fix = instance.gps.location.isValid() && instance.gps.location.age() < 5000;
    s.sats = instance.gps.satellites.isValid() ? (int)instance.gps.satellites.value() : 0;
    s.hdop = instance.gps.hdop.isValid() ? instance.gps.hdop.hdop() : 0;
    s.timeFromGps = g_timeFromGps;
    return s;
}

static void gpsPoll()
{
    GPS &g = instance.gps;
    if (g.date.isValid() && g.time.isValid() && g.date.year() >= 2025 && g.time.age() < 1500) {
        int64_t t = astro::civilToUnix(g.date.year(), g.date.month(), g.date.day(), g.time.hour(), g.time.minute(),
                                       g.time.second());
        double now = devUnixTime();
        if (fabs((double)t - now) > 1.5) {
            struct timeval tv = { (time_t)t, 0 };
            settimeofday(&tv, nullptr);
            instance.rtc.hwClockWrite();
        }
        g_timeFromGps = true;
    }
    if (g.location.isValid() && g.location.age() < 3000) {
        bool first = !g_obs.valid;
        g_obs.lat = g.location.lat();
        g_obs.lon = g.location.lng();
        if (g.altitude.isValid()) g_obs.heightM = g.altitude.meters();
        g_obs.valid = true;
        if (g_manualLoc) { g_manualLoc = false; strlcpy(g_locName, "GPS", sizeof g_locName); first = true; }
        if (first || millis() - g_lastObsSave > 600000) { saveObserver(); g_lastObsSave = millis(); }
    }
}

/* ------------------------------------------------------------------ sensors */
bool devQuat(float q[4], uint32_t *age)
{
    if (!g_haveQ) return false;
    for (int i = 0; i < 4; i++) q[i] = g_q[i];
    if (age) *age = millis() - g_qMs;
    return true;
}

bool devAccel(float a[3])
{
    if (!g_newA) return false;
    g_newA = false;
    a[0] = g_a[0]; a[1] = g_a[1]; a[2] = g_a[2];
    return true;
}

static void sensorPoll()
{
    if (g_quat && g_quat->hasUpdated()) {
        g_q[0] = g_quat->getW(); g_q[1] = g_quat->getX(); g_q[2] = g_quat->getY(); g_q[3] = g_quat->getZ();
        g_haveQ = true;
        g_qMs = millis();
    }
    if (g_acc && g_acc->hasUpdated()) {
        g_a[0] = g_acc->getX(); g_a[1] = g_acc->getY(); g_a[2] = g_acc->getZ();
        g_haveA = g_newA = true;
    }
}

/* ------------------------------------------------------------------ haptics / power */
void devHaptic(Haptic h)
{
    if (!(instance.getDeviceProbe() & HW_DRV_ONLINE)) return;
    if (!g_set.haptics) return;
    auto &d = instance.drv;
    switch (h) {
    case H_TICK: d.setWaveform(0, 24); d.setWaveform(1, 0); break;      // sharp tick
    case H_CLICK: d.setWaveform(0, 1); d.setWaveform(1, 0); break;      // strong click
    case H_LOCK: d.setWaveform(0, 10); d.setWaveform(1, 0); break;      // double click
    case H_SOFT: d.setWaveform(0, 7); d.setWaveform(1, 0); break;       // soft bump
    case H_ALERT:
        d.setWaveform(0, 14); d.setWaveform(1, 0x80 | 15); d.setWaveform(2, 14);
        d.setWaveform(3, 0x80 | 15); d.setWaveform(4, 14); d.setWaveform(5, 0); break;
    }
    d.run();
}

int devBatteryPct()
{
    float p = instance.getBatteryPercent();
    return p < 0 ? 0 : (p > 100 ? 100 : (int)p);
}

bool devCharging() { return instance.isCharging(); }

void devExitToLauncher()
{
    /* Launcher takes over on a wake from deep sleep: sleep for 150 ms and let it boot. */
    instance.setBrightness(0);
    delay(50);
    esp_sleep_enable_timer_wakeup(150000);
    esp_deep_sleep_start();
}

/* ------------------------------------------------------------------ lifecycle */
/* LilyGoLib renders whole frames into PSRAM. The sky redraws every pixel every frame, so render
 * 48-line strips in fast internal SRAM instead (two buffers), and refresh every 15 ms. The sky
 * view records its draw list once per frame and replays it per strip, so strips are cheap. */
#define STRIPE_LINES 48
static void stripeRounder(lv_event_t *e)
{
    lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
    a->x1 = 0;                                   // the Ultra's panel wants windows starting at x = 0
    a->x2 = 409;
    if (a->y1 & 1) a->y1--;
    if (!(a->y2 & 1)) a->y2++;
}

static void fastRenderSetup()
{
    lv_display_t *d = lv_display_get_default();
    if (!d) return;
    size_t sz = 410 * STRIPE_LINES * 2;
    void *b1 = heap_caps_aligned_alloc(16, sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b2 = heap_caps_aligned_alloc(16, sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (b1 && b2) {
        lv_display_set_buffers(d, b1, b2, sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_add_event_cb(d, stripeRounder, LV_EVENT_INVALIDATE_AREA, nullptr);
        Serial.printf("[boot] SRAM stripe rendering, %u lines x2\n", STRIPE_LINES);
    } else {
        if (b1) heap_caps_free(b1);
        if (b2) heap_caps_free(b2);
        Serial.println("[boot] SRAM stripes unavailable, keeping full-frame PSRAM rendering");
    }
    lv_timer_t *t = lv_display_get_refr_timer(d);
    if (t) lv_timer_set_period(t, 15);
}

static void crumb(const char *stage)
{
    Serial.printf("[boot] %6lu ms  %s\n", (unsigned long)millis(), stage);
    Serial.flush();
}

void devBegin()
{
    Serial.begin(115200);
    delay(400);   // let the USB serial reconnect after Launcher's reset
    esp_reset_reason_t rr = esp_reset_reason();
    Serial.printf("\n[boot] SkyPoint T-Watch Ultra, reset reason %d%s\n", (int)rr,
                  (rr == ESP_RST_PANIC || rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT) ? "  <-- crashed last time" : "");
    LilyGoDeviceInitOptions o;
    o.initNfc = false;
    o.initRadio = false;
    o.initAudio = false;
    o.initKeyboard = false;
    o.initSi4735 = false;
    o.initBme280 = false;
    o.initCompass = false;
    o.initCodec = false;
    o.initQmi8658 = false;
    o.initRotary = false;
    o.initPawA350 = false;
    o.initGauge = false;
    /* LilyGoLib's USB-drive FAT mount loops forever when the flash has no FAT partition
     * (e.g. under Launcher's partition layout). SkyPoint never uses it. */
    o.initFatfs = false;
#ifdef SKYPOINT_QEMU   /* emulator test build: no hardware at all, LVGL renders into a dummy buffer */
    Serial.println("[qemu] skipping hardware, dummy display");
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    lv_display_t *qd = lv_display_create(410, 502);
    size_t qsz = 410 * 502 * 2;
    lv_display_set_buffers(qd, ps_malloc(qsz), nullptr, qsz, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(qd, [](lv_display_t *d, const lv_area_t *, uint8_t *) { lv_display_flush_ready(d); });
    struct timeval tv = { 1794706200, 0 };
    settimeofday(&tv, nullptr);
    g_obs.lat = 41.88; g_obs.lon = -87.63; g_obs.valid = true;
    satsBegin();
    return;
#endif
    crumb("instance.begin...");
    instance.setDisplayParams(true, false);    // DMA pixel transfers
    instance.begin(o);
    Serial.printf("[boot] probe mask 0x%08X\n", (unsigned)instance.getDeviceProbe());
    crumb("instance.begin done");
    beginLvglHelper(instance);
    fastRenderSetup();
    crumb("lvgl helper done");
    loadSettings();
    crumb("settings loaded");
    devApplyBrightness();

    if (instance.getDeviceProbe() & HW_BHI260AP_ONLINE) {
        crumb("motion sensor: enabling rotation vector");
        g_quat = new SensorQuaternion(instance.sensor);
        g_quat->enable(100.0f, 0);
        crumb("motion sensor: enabling accelerometer");
        g_acc = new SensorXYZ(BoschSensorID::ACCEL_CORRECTED, instance.sensor);
        g_acc->enable(25.0f, 0);
        crumb("motion sensor ready");
    } else {
        Serial.println("[SkyPoint] BHI260AP offline");
    }
    crumb("loading orbits");
    loadStoredTles();
    crumb("orbits loaded");
    g_sdReady = instance.isCardReady();
    g_sdWifi = g_sdReady && SD.exists(kWifiFile);
    instance.bootButton.setClickHandler([](Button2 &) { uiButton(1); });
    instance.bootButton.setDoubleClickHandler([](Button2 &) { uiButton(2); });
    sndSetVolume(g_set.volume);
    sndBegin();
    crumb("devBegin done");
}

void devLoop()
{
#ifdef SKYPOINT_QEMU
    static uint32_t qhb = 0;
    if (millis() - qhb > 3000) {
        extern uint32_t g_uiFrames;
        Serial.printf("[qemu] %u frames/3s, free heap %u, psram %u\n", (unsigned)g_uiFrames, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
        qhb = millis(); g_uiFrames = 0;
    }
    return;
#endif
    static bool first = true;
    static uint32_t hb = 0, loops = 0;
    if (first) { first = false; Serial.println("[boot] first loop"); }
    if (++loops, millis() - hb > 10000) {
        extern uint32_t g_uiFrames;
        Serial.printf("[run] %.1f fps  heap %u  psram %u  quat %s\n", g_uiFrames / 10.0f, (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getFreePsram(), g_haveQ ? "yes" : "NO");
        g_uiFrames = 0;
        hb = millis(); loops = 0;
    }
    instance.loop();
    sensorPoll();
    gpsPoll();
    if (g_tleReady) {
        g_tleReady = false;
        int n = satsLoadTleText(g_tlePending);
        free(g_tlePending);
        g_tlePending = nullptr;
        if (n > 0) {
            storeTles();
            snprintf(g_tleMsg, sizeof g_tleMsg, "Updated %d orbit%s", n, n == 1 ? "" : "s");
            g_tleState = TLE_OK;
        } else {
            snprintf(g_tleMsg, sizeof g_tleMsg, "Downloaded data had no valid TLEs");
            g_tleState = TLE_FAIL;
        }
    }
}

/* ------------------------------------------------------------------ sound: MAX98357A on I2S */
static I2SClass g_spk;

bool sndHwBegin(uint32_t rate)
{
    g_spk.setPins(I2S_BCLK, I2S_WCLK, I2S_DOUT);
    return g_spk.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
}

void sndHwWrite(const int16_t *mono, int n)
{
    static int16_t st[256];
    while (n > 0) {
        int k = n > 128 ? 128 : n;
        for (int i = 0; i < k; i++) st[2 * i] = st[2 * i + 1] = mono[i];
        g_spk.write((const uint8_t *)st, (size_t)k * 4);
        mono += k; n -= k;
    }
}

void sndHwAmp(bool on) { instance.powerControl(POWER_SPEAK, on); }

/* ------------------------------------------------------------------ GPS page data */
bool devGpsDetail(GpsDetail &d)
{
    GPS &g = instance.gps;
    memset(&d, 0, sizeof(d));
    d.online = instance.getDeviceProbe() & HW_GPS_ONLINE;
    d.fix = g.location.isValid() && g.location.age() < 5000;
    d.hdop = g.hdop.isValid() ? g.hdop.hdop() : 0;
    d.altM = g.altitude.isValid() ? g.altitude.meters() : 0;
    d.lat = g.location.isValid() ? g.location.lat() : g_obs.lat;
    d.lon = g.location.isValid() ? g.location.lng() : g_obs.lon;
    d.ttffMs = g.timeToFirstFix();
    d.searchMs = millis();
    const GNSSSatelliteInfo *s = g.getSatellites();
    size_t n = g.getSatelliteCount();
    uint32_t now = millis();
    for (size_t i = 0; i < n; i++) {
        if (now - s[i].updated_ms > 15000) continue;      // stale entries
        d.inView++;
        if (s[i].used) d.used++;
    }
    if (d.used == 0 && g.satellites.isValid()) d.used = (int)g.satellites.value();
    return d.online;
}

int devGpsSats(GpsSat *out, int max)
{
    const GNSSSatelliteInfo *s = instance.gps.getSatellites();
    size_t n = instance.gps.getSatelliteCount();
    uint32_t now = millis();
    int k = 0;
    for (size_t i = 0; i < n && k < max; i++) {
        if (now - s[i].updated_ms > 15000 || s[i].elevation < 0) continue;
        out[k].az = s[i].azimuth; out[k].el = s[i].elevation;
        out[k].cn0 = s[i].has_cn0 ? s[i].cn0 : 0;
        out[k].used = s[i].used;
        out[k].sys = (uint8_t)s[i].system;
        k++;
    }
    return k;
}

/* ------------------------------------------------------------------ capabilities / status */
bool devHasLauncher() { return true; }
bool devHasGps() { return true; }
bool devHasHaptics() { return true; }

void devStatusLine(char *b, size_t n)
{
    GpsStatus g = devGps();
    snprintf(b, n, "%s %d  \xC2\xB7  %d%%%s", g.fix ? "GPS" : "NO FIX", g.sats, devBatteryPct(), devCharging() ? "+" : "");
}

void devStatusDetail(char *b, size_t n)
{
    GpsStatus g = devGps();
    snprintf(b, n, "GPS %s  \xC2\xB7  %d SATS  \xC2\xB7  HDOP %.1f\n%s %.4f\xC2\xB0, %.4f\xC2\xB0  %.0f M\nTIME %s  \xC2\xB7  WI-FI %s",
             g.fix ? "FIX" : (g.online ? "SEARCHING" : "OFFLINE"), g.sats, g.hdop,
             g_obs.valid ? (g_locName[0] ? g_locName : "POS") : "POS (NONE YET)", g_obs.lat, g_obs.lon, g_obs.heightM,
             g_timeFromGps ? "GPS" : (devTimeValid() ? "RTC" : "NOT SET"), g_ssid[0] ? g_ssid : "NOT SET");
}

const char *devNoLocationHint() { return "GO OUTSIDE FOR A GPS FIX  \xC2\xB7  OR SET A CITY IN SETUP"; }

int devWifiScan(char (*names)[33], int max) { return netScan(names, max); }

void devWifiSetCreds(const char *ssid, const char *pass)
{
    strlcpy(g_ssid, ssid, sizeof g_ssid);
    strlcpy(g_pass, pass, sizeof g_pass);
    g_prefs.begin("skypoint", false);
    g_prefs.putString("ssid", g_ssid);
    g_prefs.putString("pass", g_pass);
    g_prefs.end();
}

bool devWifiHasCreds() { return g_ssid[0] || g_sdWifi; }   // SD checked once at boot: no SPI-bus access from the UI
const char *devWifiSsid() { return g_ssid; }

void devSetManualLocation(double lat, double lon, const char *name)
{
    g_obs.lat = lat; g_obs.lon = lon; g_obs.heightM = 0; g_obs.valid = true;
    g_manualLoc = true;
    strlcpy(g_locName, name ? name : "", sizeof g_locName);
    saveObserver();
}

const char *devLocationName() { return g_locName; }

#endif // ARDUINO_T_WATCH_S3_ULTRA
