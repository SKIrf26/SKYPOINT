#include "netsync.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <time.h>
#include <sys/time.h>

bool netConnect(const char *ssid, const char *pass, uint32_t timeoutMs, char *msg, size_t msgLen)
{
    snprintf(msg, msgLen, "Connecting to %s...", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) vTaskDelay(pdMS_TO_TICKS(200));
    return WiFi.status() == WL_CONNECTED;
}

void netDisconnect()
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

bool netSyncTime(uint32_t timeoutMs)
{
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        if (time(nullptr) > 1735689600) return true;   // after 2025-01-01
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return false;
}

static bool jsonNum(const String &s, const char *key, double &out)
{
    int k = s.indexOf(key);
    if (k < 0) return false;
    k = s.indexOf(':', k);
    if (k < 0) return false;
    out = atof(s.c_str() + k + 1);
    return true;
}

bool netIpLocation(double &lat, double &lon, char *city, size_t cityLen)
{
    HTTPClient http;
    if (!http.begin("http://ip-api.com/json/?fields=status,lat,lon,city")) return false;
    bool ok = false;
    if (http.GET() == 200) {
        String s = http.getString();
        if (s.indexOf("success") >= 0 && jsonNum(s, "\"lat\"", lat) && jsonNum(s, "\"lon\"", lon)) {
            ok = true;
            int c = s.indexOf("\"city\":\"");
            if (c >= 0) {
                int e = s.indexOf('"', c + 8);
                String nm = s.substring(c + 8, e);
                strlcpy(city, nm.c_str(), cityLen);
            } else if (cityLen) city[0] = 0;
        }
    }
    http.end();
    return ok;
}

bool netFetchTles(String &out)
{
    static const uint32_t ids[] = { 25544, 48274, 20580 };   // ISS, Tiangong (CSS), Hubble
    for (uint32_t id : ids) {
        NetworkClientSecure c;
        c.setInsecure();   // public data; every TLE line is checksum-validated before use
        HTTPClient http;
        char url[96];
        snprintf(url, sizeof url, "https://celestrak.org/NORAD/elements/gp.php?CATNR=%u&FORMAT=TLE", (unsigned)id);
        if (http.begin(c, url)) {
            if (http.GET() == 200) out += http.getString() + "\n";
            http.end();
        }
    }
    return out.length() > 100;
}

int netScan(char (*names)[33], int max)
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    int n = WiFi.scanNetworks();
    int k = 0;
    for (int i = 0; i < n && k < max; i++) {
        String s = WiFi.SSID(i);
        if (!s.length()) continue;
        bool dup = false;
        for (int j = 0; j < k; j++)
            if (s == names[j]) dup = true;
        if (dup) continue;
        strlcpy(names[k++], s.c_str(), 33);
    }
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    return k;
}
