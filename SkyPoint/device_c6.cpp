/**
 * device_c6.cpp - Waveshare ESP32-C6-Touch-AMOLED-2.06 implementation of device.h.
 *
 *   display  CO5300 410x502 AMOLED over QSPI (GFX Library for Arduino), LVGL in 48-line stripes
 *   touch    FT3168 @0x38          motion  QMI8658 @0x6B/0x6A, 6-axis -> Mahony fusion
 *   clock    PCF85063 @0x51        power   AXP2101 @0x34 (battery %, PWR key)
 *   no GPS:  time from the RTC / NTP, position from Wi-Fi (IP lookup) or a chosen city
 *
 * Pin map and chip quirks follow the verified HUNTR-SEEKR port for this board.
 */
#include <Arduino.h>   // brings in sdkconfig (CONFIG_IDF_TARGET_*)
#if defined(CONFIG_IDF_TARGET_ESP32C6)
#include <Wire.h>
#include <Preferences.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <sys/time.h>
#include <time.h>
#include "device.h"
#include "sats.h"
#include "astro.h"
#include "ui.h"
#include "netsync.h"
#include "sound.h"
#include <driver/i2s_std.h>

#define PIN_LCD_SCLK 0
#define PIN_LCD_D0 1
#define PIN_LCD_D1 2
#define PIN_LCD_D2 3
#define PIN_LCD_D3 4
#define PIN_LCD_CS 5
#define PIN_LCD_RST 11
#define LCD_XOFF 22
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 7
#define PIN_TP_INT 15
#define TP_ADDR 0x38
#define PMU_ADDR 0x34
#define RTC_ADDR 0x51
#define PIN_BOOT 9
#define QSPI_HZ 40000000UL
#define LV_STRIPE 48

static Arduino_DataBus *g_bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
static Arduino_GFX *g_gfx = new Arduino_CO5300(g_bus, PIN_LCD_RST, 0, 410, 502, LCD_XOFF, 0, 0, 0);

static Preferences g_prefs;
static Settings g_set;
static Observer g_obs;
static char g_ssid[33] = "", g_pass[65] = "", g_locName[32] = "";
static bool g_manualLoc = false, g_timeFromNtp = false, g_rtcOk = false, g_pmuOk = false, g_imuOk = false;
static uint32_t g_lastSyncUnix = 0;
static volatile TleState g_tleState = TLE_IDLE;
static char g_tleMsg[96] = "";
static char *g_tlePending = nullptr;
static volatile bool g_tleReady = false, g_locReady = false;
static double g_pendLat = 0, g_pendLon = 0;
static char g_pendCity[32] = "";
static bool g_screenOff = false;

/* ------------------------------------------------------------------ I2C helpers */
static bool i2cRead(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, (uint8_t)n) != n) return false;
    for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

static bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t v)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}

/* ------------------------------------------------------------------ display + LVGL */
static void panelBrightness(uint8_t lvl)
{
    int v = (int)((uint32_t)lvl * lvl / 255);   // gamma 2: even-feeling slider
    if (lvl && v < 1) v = 1;
    g_bus->beginWrite();
    g_bus->writeC8D8(0x51, (uint8_t)v);
    g_bus->endWrite();
}

static void panelPower(bool on)
{
    g_bus->beginWrite();
    g_bus->writeCommand(on ? 0x29 : 0x28);
    g_bus->endWrite();
}

static void flushCb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    g_gfx->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t *)px, lv_area_get_width(a), lv_area_get_height(a));
    lv_display_flush_ready(d);
}

/* the CO5300 wants even window coordinates */
static void rounderCb(lv_event_t *e)
{
    lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
    a->x1 &= ~1; a->y1 &= ~1;
    a->x2 |= 1; a->y2 |= 1;
}

/* ------------------------------------------------------------------ touch (FT3168) */
static volatile bool g_tpFlag = false;
static void IRAM_ATTR tpIsr() { g_tpFlag = true; }
static bool g_tpDown = false;
static int16_t g_tpX = 0, g_tpY = 0;
static uint32_t g_tpAt = 0;

static bool ftRead8(uint8_t reg, uint8_t *v)
{
    Wire.beginTransmission(TP_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom((uint8_t)TP_ADDR, (uint8_t)1) != 1) return false;
    *v = Wire.read();
    return true;
}

static void touchBegin()
{
    uint8_t id = 0;
    for (int i = 0; i < 5 && !ftRead8(0xA0, &id); i++) delay(200);
    i2cWrite(TP_ADDR, 0xA5, 0x01);    // monitor mode: INT pulses while touched
    pinMode(PIN_TP_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_TP_INT), tpIsr, FALLING);
}

static void touchRead(lv_indev_t *, lv_indev_data_t *data)
{
    uint32_t now = millis();
    /* read on the interrupt, and poll as well (cheap: 5 single-byte reads) */
    uint8_t d[5];
    bool ok = true;
    for (uint8_t i = 0; i < 5 && ok; i++) ok = ftRead8(0x02 + i, &d[i]);
    g_tpFlag = false;
    if (ok) {
        uint8_t n = d[0] & 0x0F, ev = d[1] >> 6;
        int16_t x = ((d[1] & 0x0F) << 8) | d[2], y = ((d[3] & 0x0F) << 8) | d[4];
        bool sane = x < 600 && y < 700 && !(x == 0 && y == 0);
        if (sane && n > 0 && ev != 1) { g_tpX = x; g_tpY = y; g_tpDown = true; g_tpAt = now; }
        else if (n == 0 || ev == 1) g_tpDown = false;
    }
    if (g_tpDown && now - g_tpAt > 150) g_tpDown = false;
    data->point.x = constrain(g_tpX, 0, 409);
    data->point.y = constrain(g_tpY, 0, 501);
    data->state = g_tpDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (g_tpDown && g_screenOff) { g_screenOff = false; panelPower(true); panelBrightness(g_set.brightness); }
}

/* ------------------------------------------------------------------ IMU (QMI8658) + Mahony fusion */
static uint8_t g_imuAddr = 0x6B;
static float g_q[4] = { 1, 0, 0, 0 };
static float g_acc[3] = { 0, 0, 1 }, g_bias[3] = { 0, 0, 0 };
static bool g_newAcc = false, g_haveQ = false;
static uint32_t g_imuAt = 0, g_imuStart = 0, g_stillMs = 0;

static bool imuBegin()
{
    const uint8_t cand[2] = { 0x6B, 0x6A };
    uint8_t id = 0;
    bool found = false;
    for (int k = 0; k < 2 && !found; k++)
        if (i2cRead(cand[k], 0x00, &id, 1) && id == 0x05) { g_imuAddr = cand[k]; found = true; }
    if (!found) return false;
    i2cWrite(g_imuAddr, 0x60, 0xB0);  // soft reset
    delay(30);
    for (int t = 0; t < 3; t++) {
        i2cWrite(g_imuAddr, 0x02, 0x40);   // auto-increment
        i2cWrite(g_imuAddr, 0x08, 0x00);   // all off while configuring
        delay(3);
        i2cWrite(g_imuAddr, 0x03, 0x16);   // accel +-4 g, ~112 Hz
        i2cWrite(g_imuAddr, 0x04, 0x46);   // gyro +-256 dps, ~112 Hz
        i2cWrite(g_imuAddr, 0x08, 0x03);   // accel + gyro on
        delay(5);
        uint8_t c7 = 0, c3 = 0;
        if (i2cRead(g_imuAddr, 0x08, &c7, 1) && i2cRead(g_imuAddr, 0x04, &c3, 1) && c7 == 0x03 && c3 == 0x46) break;
    }
    g_imuStart = millis();
    return true;
}

static void mahony(float gx, float gy, float gz, float ax, float ay, float az, float dt, float kp)
{
    float q0 = g_q[0], q1 = g_q[1], q2 = g_q[2], q3 = g_q[3];
    float n = sqrtf(ax * ax + ay * ay + az * az);
    if (n > 0.3f) {
        ax /= n; ay /= n; az /= n;
        float vx = 2 * (q1 * q3 - q0 * q2), vy = 2 * (q0 * q1 + q2 * q3), vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;
        float ex = ay * vz - az * vy, ey = az * vx - ax * vz, ez = ax * vy - ay * vx;
        /* trust the accelerometer less while the watch is being swung */
        float w = fabsf(n - 1.0f) < 0.15f ? 1.0f : 0.2f;
        gx += kp * w * ex; gy += kp * w * ey; gz += kp * w * ez;
    }
    float h = 0.5f * dt;
    float a = q0, b = q1, c = q2;
    q0 += (-b * gx - c * gy - q3 * gz) * h;
    q1 += (a * gx + c * gz - q3 * gy) * h;
    q2 += (a * gy - b * gz + q3 * gx) * h;
    q3 += (a * gz + b * gy - c * gx) * h;
    n = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    g_q[0] = q0 / n; g_q[1] = q1 / n; g_q[2] = q2 / n; g_q[3] = q3 / n;
}

static void imuPoll()
{
    if (!g_imuOk) return;
    uint32_t now = micros();
    if (now - g_imuAt < 9000) return;               // ~110 Hz
    float dt = g_imuAt ? (now - g_imuAt) / 1e6f : 0.01f;
    if (dt > 0.1f) dt = 0.1f;
    g_imuAt = now;
    uint8_t b[12];
    if (!i2cRead(g_imuAddr, 0x35, b, 12)) return;
    float ax = (int16_t)(b[0] | (b[1] << 8)) / 8192.0f, ay = (int16_t)(b[2] | (b[3] << 8)) / 8192.0f,
          az = (int16_t)(b[4] | (b[5] << 8)) / 8192.0f;
    float g[3] = { (int16_t)(b[6] | (b[7] << 8)) / 128.0f, (int16_t)(b[8] | (b[9] << 8)) / 128.0f,
                   (int16_t)(b[10] | (b[11] << 8)) / 128.0f };   // deg/s
    /* gyro bias: learn it whenever the watch is still */
    float gm = fabsf(g[0] - g_bias[0]) + fabsf(g[1] - g_bias[1]) + fabsf(g[2] - g_bias[2]);
    float an = sqrtf(ax * ax + ay * ay + az * az);
    if (gm < 4.0f && fabsf(an - 1.0f) < 0.05f) g_stillMs += (uint32_t)(dt * 1000);
    else g_stillMs = 0;
    if (g_stillMs > 400) {
        float k = millis() - g_imuStart < 3000 ? 0.05f : 0.005f;
        for (int i = 0; i < 3; i++) g_bias[i] += (g[i] - g_bias[i]) * k;
    }
    const float D = 0.01745329f;
    float kp = millis() - g_imuStart < 2500 ? 8.0f : 1.2f;    // converge fast after boot
    mahony((g[0] - g_bias[0]) * D, (g[1] - g_bias[1]) * D, (g[2] - g_bias[2]) * D, ax, ay, az, dt, kp);
    g_acc[0] = ax; g_acc[1] = ay; g_acc[2] = az;
    g_newAcc = true;
    g_haveQ = true;
}

bool devQuat(float q[4], uint32_t *age)
{
    if (!g_haveQ) return false;
    memcpy(q, g_q, sizeof g_q);
    if (age) *age = 0;
    return true;
}

bool devAccel(float a[3])
{
    if (!g_newAcc) return false;
    g_newAcc = false;
    memcpy(a, g_acc, sizeof g_acc);
    return true;
}

/* ------------------------------------------------------------------ RTC (PCF85063) */
static uint8_t bcd2(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t tobcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static void rtcLoad()
{
    uint8_t r[7];
    g_rtcOk = i2cRead(RTC_ADDR, 0x04, r, 7);
    if (!g_rtcOk || (r[0] & 0x80)) return;     // oscillator stopped: time not valid
    int y = 2000 + bcd2(r[6]), mo = bcd2(r[5] & 0x1F), d = bcd2(r[3] & 0x3F);
    int h = bcd2(r[2] & 0x3F), mi = bcd2(r[1] & 0x7F), s = bcd2(r[0] & 0x7F);
    if (y < 2025 || mo < 1 || mo > 12 || d < 1) return;
    struct timeval tv = { (time_t)astro::civilToUnix(y, mo, d, h, mi, s), 0 };
    settimeofday(&tv, nullptr);
}

static void rtcStore()
{
    int y, mo, d, h, mi, s;
    astro::unixToCivil((int64_t)time(nullptr), y, mo, d, h, mi, s);
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(0x04);
    Wire.write(tobcd(s)); Wire.write(tobcd(mi)); Wire.write(tobcd(h)); Wire.write(tobcd(d));
    Wire.write(0); Wire.write(tobcd(mo)); Wire.write(tobcd(y - 2000));
    Wire.endTransmission();
}

/* ------------------------------------------------------------------ PMU (AXP2101) + buttons */
static int g_batPct = -1;
static bool g_charging = false;
static uint32_t g_pmuAt = 0;

static void pmuBegin()
{
    uint8_t id = 0, v = 0;
    g_pmuOk = i2cRead(PMU_ADDR, 0x03, &id, 1) && id != 0 && id != 0xFF;
    if (!g_pmuOk) return;
    if (i2cRead(PMU_ADDR, 0x18, &v, 1) && !(v & 0x08)) i2cWrite(PMU_ADDR, 0x18, v | 0x08);   // fuel gauge on
    if (i2cRead(PMU_ADDR, 0x41, &v, 1)) i2cWrite(PMU_ADDR, 0x41, v | 0x0C);                 // PWR key IRQs on
    i2cWrite(PMU_ADDR, 0x49, 0x0C);
}

static void pmuPoll()
{
    if (!g_pmuOk || millis() - g_pmuAt < 200) return;
    g_pmuAt = millis();
    uint8_t s = 0;
    if (i2cRead(PMU_ADDR, 0x49, &s, 1) && (s & 0x0C)) {
        i2cWrite(PMU_ADDR, 0x49, s & 0x0C);
        if (s & 0x08) {           // PWR short press: screen off / on
            g_screenOff = !g_screenOff;
            panelPower(!g_screenOff);
            if (!g_screenOff) panelBrightness(g_set.brightness);
        }
    }
    static uint32_t slow = 0;
    if (millis() - slow > 5000) {
        slow = millis();
        uint8_t s0 = 0, s1 = 0, pct = 0;
        if (i2cRead(PMU_ADDR, 0x00, &s0, 1) && i2cRead(PMU_ADDR, 0x01, &s1, 1)) {
            bool bat = s0 & 0x08;
            uint8_t cs = s1 & 0x07;
            g_charging = bat && cs >= 1 && cs <= 4;
            g_batPct = (bat && i2cRead(PMU_ADDR, 0xA4, &pct, 1) && pct <= 100) ? pct : -1;
        }
    }
}

static void bootButtonPoll()
{
    static bool down = false;
    static uint32_t downAt = 0, lastUp = 0;
    static int clicks = 0;
    bool now = digitalRead(PIN_BOOT) == LOW;
    uint32_t t = millis();
    if (now && !down && t - lastUp > 30) { down = true; downAt = t; }
    if (!now && down && t - downAt > 30) { down = false; lastUp = t; clicks++; }
    if (clicks && !down && t - lastUp > 320) {
        uiButton(clicks >= 2 ? 2 : 1);
        clicks = 0;
    }
}

/* ------------------------------------------------------------------ sound: ES8311 codec + amp
 * Register sequence and clocking from HUNTR-SEEKR's verified port (Espressif es8311 driver):
 * codec clocked from BCLK, 16 kHz, Philips I2S with 32-bit slots, amplifier enable on GPIO 6. */
#define ES8311_ADDR 0x18
#define PIN_I2S_BCLK 20
#define PIN_I2S_LRCK 22
#define PIN_I2S_DOUT 23
#define PIN_PA 6
static i2s_chan_handle_t g_i2s = nullptr;

static bool esW(uint8_t r, uint8_t v) { return i2cWrite(ES8311_ADDR, r, v); }
static bool esR(uint8_t r, uint8_t *v) { return i2cRead(ES8311_ADDR, r, v, 1); }

static bool codecInit()
{
    Wire.beginTransmission(ES8311_ADDR);
    if (Wire.endTransmission() != 0) { Serial.println("[sound] ES8311 not found"); return false; }
    uint8_t r;
    esW(0x00, 0x1F); delay(20);
    esW(0x00, 0x00); esW(0x00, 0x80);
    esW(0x01, 0x3F | 0x80);
    if (esR(0x06, &r)) { r &= ~0x20; esW(0x06, r); }
    r = 0; esR(0x02, &r); r &= 0x07; r |= (1 - 1) << 5; r |= 2 << 3; esW(0x02, r);
    esW(0x03, (0 << 6) | 0x10);
    esW(0x04, 0x10);
    esW(0x05, ((1 - 1) << 4) | (1 - 1));
    r = 0; esR(0x06, &r); r &= 0xE0; r |= (4 - 1); esW(0x06, r);
    r = 0; esR(0x07, &r); r &= 0xC0; esW(0x07, r);
    esW(0x08, 0xFF);
    r = 0; esR(0x00, &r); r &= 0xBF; esW(0x00, r);
    esW(0x09, 4 << 2); esW(0x0A, 4 << 2);
    esW(0x0D, 0x01); esW(0x0E, 0x02); esW(0x12, 0x00); esW(0x13, 0x10);
    esW(0x1C, 0x6A); esW(0x37, 0x08);
    r = 0; esR(0x31, &r); r &= ~(0x40 | 0x20); esW(0x31, r);
    esW(0x32, (uint8_t)(85 * 256 / 100 - 1));       // codec DAC level; the app volume is applied digitally
    return true;
}

bool sndHwBegin(uint32_t rate)
{
    pinMode(PIN_PA, OUTPUT);
    digitalWrite(PIN_PA, LOW);
    if (!codecInit()) return false;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    cc.dma_desc_num = 4;
    cc.dma_frame_num = 128;
    if (i2s_new_channel(&cc, &g_i2s, nullptr) != ESP_OK) return false;
    i2s_std_config_t sc = {};
    sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    sc.gpio_cfg.bclk = (gpio_num_t)PIN_I2S_BCLK;
    sc.gpio_cfg.ws = (gpio_num_t)PIN_I2S_LRCK;
    sc.gpio_cfg.dout = (gpio_num_t)PIN_I2S_DOUT;
    sc.gpio_cfg.din = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(g_i2s, &sc) != ESP_OK) return false;
    return i2s_channel_enable(g_i2s) == ESP_OK;
}

void sndHwWrite(const int16_t *mono, int n)
{
    static int32_t st[256];
    while (n > 0) {
        int k = n > 128 ? 128 : n;
        for (int i = 0; i < k; i++) st[2 * i] = st[2 * i + 1] = (int32_t)mono[i] << 16;
        size_t w;
        i2s_channel_write(g_i2s, st, k * 8, &w, portMAX_DELAY);
        mono += k; n -= k;
    }
}

void sndHwAmp(bool on) { digitalWrite(PIN_PA, on ? HIGH : LOW); }

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
    g_prefs.getString("ssid", g_ssid, sizeof g_ssid);
    g_prefs.getString("pass", g_pass, sizeof g_pass);
    g_prefs.getString("locname", g_locName, sizeof g_locName);
    g_manualLoc = g_prefs.getBool("mloc", false);
    g_lastSyncUnix = g_prefs.getUInt("lastsync", 0);
    if (g_prefs.isKey("lat")) {
        g_obs.lat = g_prefs.getDouble("lat", 0);
        g_obs.lon = g_prefs.getDouble("lon", 0);
        g_obs.heightM = g_prefs.getDouble("hgt", 0);
        g_obs.valid = true;
    }
    String tle = g_prefs.getString("tle", "");
    g_prefs.end();
    if (tle.length()) satsLoadTleText(tle.c_str());
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
void devApplyBrightness() { if (!g_screenOff) panelBrightness(g_set.brightness); }

/* ------------------------------------------------------------------ Wi-Fi sync: time + place + orbits */
static void syncTask(void *)
{
    bool ok = false;
    String tles;
    if (netConnect(g_ssid, g_pass, 20000, g_tleMsg, sizeof g_tleMsg)) {
        snprintf(g_tleMsg, sizeof g_tleMsg, "Setting the clock...");
        if (netSyncTime(10000)) { rtcStore(); g_timeFromNtp = true; }
        if (!g_manualLoc) {
            snprintf(g_tleMsg, sizeof g_tleMsg, "Finding your location...");
            if (netIpLocation(g_pendLat, g_pendLon, g_pendCity, sizeof g_pendCity)) g_locReady = true;
        }
        snprintf(g_tleMsg, sizeof g_tleMsg, "Downloading orbits...");
        ok = netFetchTles(tles);
    }
    netDisconnect();
    if (ok) {
        g_tlePending = strdup(tles.c_str());
        g_tleReady = true;
    } else {
        snprintf(g_tleMsg, sizeof g_tleMsg, g_timeFromNtp ? "Clock set, orbit download failed" : "Sync failed: check Wi-Fi");
        g_tleState = TLE_FAIL;
    }
    vTaskDelete(nullptr);
}

void devStartTleUpdate()
{
    if (g_tleState == TLE_RUNNING) return;
    if (!g_ssid[0]) {
        snprintf(g_tleMsg, sizeof g_tleMsg, "Set up Wi-Fi first (Setup > Wi-Fi)");
        g_tleState = TLE_FAIL;
        return;
    }
    g_tleState = TLE_RUNNING;
    snprintf(g_tleMsg, sizeof g_tleMsg, "Starting Wi-Fi...");
    xTaskCreate(syncTask, "sync", 10240, nullptr, 1, nullptr);
}

TleState devTleState() { return g_tleState; }
const char *devTleMsg() { return g_tleMsg; }

/* ------------------------------------------------------------------ misc API */
double devUnixTime()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1e6;
}
bool devTimeValid() { return devUnixTime() > 1735689600.0; }
Observer devObserver() { return g_obs; }
GpsStatus devGps() { GpsStatus s; s.timeFromGps = false; return s; }
void devHaptic(Haptic) {}                       // no vibration motor on this board
int devBatteryPct() { return g_batPct < 0 ? 0 : g_batPct; }
bool devCharging() { return g_charging; }
bool devSdReady() { return false; }
void devExitToLauncher() { esp_restart(); }
bool devHasLauncher() { return false; }
bool devGpsDetail(GpsDetail &d) { memset(&d, 0, sizeof(d)); return false; }
int devGpsSats(GpsSat *, int) { return 0; }
bool devHasGps() { return false; }
bool devHasHaptics() { return false; }

void devStatusLine(char *b, size_t n)
{
    const char *src = g_timeFromNtp ? "NTP" : (devTimeValid() ? "RTC" : "NO CLOCK");
    if (g_batPct >= 0) snprintf(b, n, "%s  \xC2\xB7  %s  \xC2\xB7  %d%%%s", src, g_locName[0] ? g_locName : "NO PLACE", g_batPct, g_charging ? "+" : "");
    else snprintf(b, n, "%s  \xC2\xB7  %s", src, g_locName[0] ? g_locName : "NO PLACE");
}

void devStatusDetail(char *b, size_t n)
{
    char last[32] = "NEVER";
    if (g_lastSyncUnix) {
        long ago = (long)(devUnixTime() - g_lastSyncUnix);
        if (ago < 3600) snprintf(last, sizeof last, "%ld MIN AGO", ago / 60);
        else if (ago < 172800) snprintf(last, sizeof last, "%ld H AGO", ago / 3600);
        else snprintf(last, sizeof last, "%ld DAYS AGO", ago / 86400);
    }
    snprintf(b, n, "PLACE %s  %.3f\xC2\xB0, %.3f\xC2\xB0\nTIME %s  \xC2\xB7  WI-FI %s\nLAST SYNC %s  \xC2\xB7  IMU %s",
             g_obs.valid ? (g_locName[0] ? g_locName : "SET") : "NOT SET", g_obs.lat, g_obs.lon,
             g_timeFromNtp ? "NTP" : (devTimeValid() ? "RTC" : "NOT SET"), g_ssid[0] ? g_ssid : "NOT SET", last,
             g_imuOk ? "OK" : "MISSING");
}

const char *devNoLocationHint() { return "SETUP > LOCATION  \xC2\xB7  OR SYNC OVER WI-FI"; }

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

bool devWifiHasCreds() { return g_ssid[0] != 0; }
const char *devWifiSsid() { return g_ssid; }

void devSetManualLocation(double lat, double lon, const char *name)
{
    g_obs.lat = lat; g_obs.lon = lon; g_obs.heightM = 0; g_obs.valid = true;
    g_manualLoc = name != nullptr;              // nullptr = "use Wi-Fi location"
    strlcpy(g_locName, name ? name : "", sizeof g_locName);
    saveObserver();
}

const char *devLocationName() { return g_locName; }

/* ------------------------------------------------------------------ lifecycle */
static void bootLog(const char *stage)
{
    Serial.printf("[boot] %6lu ms  %-14s free heap %u\n", (unsigned long)millis(), stage, (unsigned)ESP.getFreeHeap());
}

void devBegin()
{
    Serial.begin(115200);
    delay(300);   // give USB-CDC a moment so the first lines are not lost
    esp_reset_reason_t rr = esp_reset_reason();
    Serial.printf("\n[boot] SkyPoint C6, reset reason %d%s\n", (int)rr,
                  (rr == ESP_RST_PANIC || rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT) ? "  <-- crashed last time" : "");
    bool panelOk = g_gfx->begin(QSPI_HZ);
    bootLog(panelOk ? "panel ok" : "PANEL FAILED");
    g_gfx->fillScreen(0);
    /* light the panel straight away with a boot message, so a later hang is visible */
    g_gfx->setTextColor(0x5F9F);
    g_gfx->setTextSize(3);
    g_gfx->setCursor(120, 230);
    g_gfx->print("SKYPOINT");
    g_gfx->setTextSize(2);
    g_gfx->setTextColor(0x7BEF);
    g_gfx->setCursor(150, 270);
    g_gfx->print("starting");
    panelBrightness(180);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);
    satsBegin();
    loadSettings();
    bootLog("settings");
    pmuBegin();
    bootLog(g_pmuOk ? "pmu ok" : "pmu MISSING");
    rtcLoad();
    bootLog(g_rtcOk ? "rtc ok" : "rtc MISSING");
    touchBegin();
    bootLog("touch");
    g_imuOk = imuBegin();
    bootLog(g_imuOk ? "imu ok" : "imu MISSING");
    pinMode(PIN_BOOT, INPUT_PULLUP);

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    lv_display_t *disp = lv_display_create(410, 502);
    size_t sz = 410 * LV_STRIPE * 2;
    void *b1 = heap_caps_malloc(sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);   // one stripe buffer: heap is precious
    if (!b1) Serial.println("[boot] LVGL buffer allocation FAILED");
    lv_display_set_buffers(disp, b1, nullptr, sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flushCb);
    lv_display_add_event_cb(disp, rounderCb, LV_EVENT_INVALIDATE_AREA, nullptr);
    lv_timer_set_period(lv_display_get_refr_timer(disp), 15);
    lv_indev_t *tp = lv_indev_create();
    lv_indev_set_type(tp, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(tp, touchRead);
    bootLog("lvgl ready");
    sndSetVolume(g_set.volume);
    sndBegin();
    bootLog("sound");
}

void devLoop()
{
    imuPoll();
    pmuPoll();
    bootButtonPoll();
    static bool faded = false;
    if (!faded) { faded = true; panelBrightness(g_set.brightness); bootLog("first loop"); }
    static uint32_t hb = 0, frames = 0;
    frames++;
    if (millis() - hb > 10000) {
        extern uint32_t g_uiFrames;
        Serial.printf("[run] %.1f fps  free heap %u  min %u\n", g_uiFrames / 10.0f, (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMinFreeHeap());
        g_uiFrames = 0;
        hb = millis(); frames = 0;
    }
    if (g_locReady) {
        g_locReady = false;
        devSetManualLocation(g_pendLat, g_pendLon, nullptr);
        strlcpy(g_locName, g_pendCity[0] ? g_pendCity : "WI-FI", sizeof g_locName);
        saveObserver();
    }
    if (g_tleReady) {
        g_tleReady = false;
        int n = satsLoadTleText(g_tlePending);
        free(g_tlePending);
        g_tlePending = nullptr;
        static char buf[1400];
        int len = satsSerialize(buf, sizeof buf);
        g_prefs.begin("skypoint", false);
        if (len > 0) g_prefs.putString("tle", buf);
        g_lastSyncUnix = (uint32_t)devUnixTime();
        g_prefs.putUInt("lastsync", g_lastSyncUnix);
        g_prefs.end();
        snprintf(g_tleMsg, sizeof g_tleMsg, "Synced: clock%s, %d orbit%s", g_locName[0] ? ", place" : "", n, n == 1 ? "" : "s");
        g_tleState = TLE_OK;
    }
}

#endif // CONFIG_IDF_TARGET_ESP32C6
