#include "sats.h"
#include <Sgp4.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* Fallback elements, CelesTrak/SatNOGS, epoch 2026-11-09 (day 313.85). Update over Wi-Fi or SD for accuracy. */
static const char *kIssName = "ISS";
static const char *kIssL1 = "1 25544U 98067A   26313.85251676  .00008451  00000-0  15976-3 0  9994";
static const char *kIssL2 = "2 25544  51.6322 299.2609 0007289   7.6181 352.4917 15.49281775594331";

static Sgp4 *g_sat[SAT_MAX];
static int g_n = 0;
static double g_lat = 0, g_lon = 0, g_alt = 0;

static bool checksumOk(const char *l)
{
    if (strlen(l) < 69) return false;
    int c = 0;
    for (int i = 0; i < 68; i++) {
        if (l[i] >= '0' && l[i] <= '9') c += l[i] - '0';
        else if (l[i] == '-') c += 1;
    }
    return (c % 10) == (l[68] - '0');
}

static uint32_t noradOf(const char *l1)
{
    char b[6] = { 0 };
    memcpy(b, l1 + 2, 5);
    return (uint32_t)atol(b);
}

void satsBegin()
{
    if (g_n == 0) satsAddTle(kIssName, kIssL1, kIssL2);
}

bool satsAddTle(const char *name, const char *l1, const char *l2)
{
    if (!checksumOk(l1) || !checksumOk(l2) || l1[0] != '1' || l2[0] != '2') return false;
    uint32_t id = noradOf(l1);
    int slot = -1;
    for (int i = 0; i < g_n; i++)
        if (noradOf(g_sat[i]->line1) == id) slot = i;
    if (slot < 0) {
        if (g_n >= SAT_MAX) return false;
        slot = g_n++;
        g_sat[slot] = new Sgp4();
    } else {
        /* keep whichever element set is newer */
        char ep[15] = { 0 };
        memcpy(ep, l1 + 18, 14);
        char eo[15] = { 0 };
        memcpy(eo, g_sat[slot]->line1 + 18, 14);
        double yn = atof(ep), yo = atof(eo);
        auto full = [](double y) { int yy = (int)(y / 1000); return (yy < 57 ? 2000 + yy : 1900 + yy) * 1000.0 + fmod(y, 1000.0); };
        if (full(yn) < full(yo)) return false;
        g_sat[slot]->line1[0] = 0;     // force re-init even when line 1 is identical
    }
    char a[130], b[130], nm[25];
    strncpy(a, l1, 129); a[129] = 0; a[69] = 0;
    strncpy(b, l2, 129); b[129] = 0; b[69] = 0;
    /* trim the name */
    const char *s = name;
    if (s[0] == '0' && s[1] == ' ') s += 2;
    while (*s == ' ') s++;
    strncpy(nm, s, 24); nm[24] = 0;
    for (int k = (int)strlen(nm) - 1; k >= 0 && (nm[k] == ' ' || nm[k] == '\r' || nm[k] == '\n'); k--) nm[k] = 0;
    g_sat[slot]->init(nm, a, b);
    g_sat[slot]->site(g_lat, g_lon, g_alt);
    return true;
}

int satsLoadTleText(const char *text)
{
    /* Accept "name\nline1\nline2" groups or bare line pairs. */
    char lines[3][100];
    int have = 0, loaded = 0;
    const char *p = text;
    char name[40] = "SAT";
    while (*p) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        int len = (int)(e - p);
        if (len > 99) len = 99;
        char ln[100];
        memcpy(ln, p, len); ln[len] = 0;
        while (len > 0 && (ln[len - 1] == '\r' || ln[len - 1] == ' ')) ln[--len] = 0;
        if (len > 0) {
            if (ln[0] == '1' && len >= 69 && ln[1] == ' ') { strcpy(lines[1], ln); have = 1; }
            else if (ln[0] == '2' && len >= 69 && ln[1] == ' ' && have == 1) {
                strcpy(lines[2], ln);
                if (satsAddTle(name, lines[1], lines[2])) loaded++;
                have = 0;
                strcpy(name, "SAT");
            } else {
                const char *n = ln;
                if (n[0] == '0' && n[1] == ' ') n += 2;
                strncpy(name, n, 39); name[39] = 0;
            }
        }
        p = *e ? e + 1 : e;
    }
    return loaded;
}

int satsCount() { return g_n; }
const char *satName(int i) { return (i >= 0 && i < g_n) ? g_sat[i]->satName : ""; }
double satEpochJd(int i) { return (i >= 0 && i < g_n) ? g_sat[i]->satrec.jdsatepoch : 0; }
double satTleAgeDays(int i, double jd) { return jd - satEpochJd(i); }
uint32_t satNorad(int i) { return (i >= 0 && i < g_n) ? noradOf(g_sat[i]->line1) : 0; }

void satsSetSite(double lat, double lon, double altM)
{
    g_lat = lat; g_lon = lon; g_alt = altM;
    for (int i = 0; i < g_n; i++) g_sat[i]->site(lat, lon, altM);
}

bool satCompute(int i, double jd, SatNow &o)
{
    if (i < 0 || i >= g_n) return false;
    Sgp4 *s = g_sat[i];
    s->findsat(jd);
    o.az = s->satAz; o.el = s->satEl; o.rangeKm = s->satDist; o.altKm = s->satAlt;
    o.lat = s->satLat; o.lon = s->satLon; o.vis = s->satVis;
    return true;
}

static double elAt(Sgp4 *s, double jd)
{
    s->findsat(jd);
    return s->satEl;
}

static void nowOf(Sgp4 *s, double jd, SatNow &o)
{
    s->findsat(jd);
    o.az = s->satAz; o.el = s->satEl; o.rangeKm = s->satDist; o.altKm = s->satAlt;
    o.lat = s->satLat; o.lon = s->satLon; o.vis = s->satVis;
}

/* Pass finder: adaptive scan for horizon crossings (long steps while the satellite is far
 * below the horizon), bisection for rise/set, golden-section search for the culmination. */
static bool findPass(Sgp4 *s, double jd, double minEl, SatPass &out)
{
    memset(&out, 0, sizeof(out));
    const double minute = 1.0 / 1440.0, span = 3.0;
    double t0 = jd, e0 = elAt(s, t0);
    double rise = e0 >= 0 ? jd : 0;       // already up: pass in progress
    if (rise) {                            // walk back to the real rise (bounded to 20 min)
        double tb = jd;
        for (int k = 0; k < 20 && elAt(s, tb - minute) >= 0; k++) tb -= minute;
        double lo = tb - minute, hi = tb;
        for (int k = 0; k < 14; k++) { double m = (lo + hi) / 2; if (elAt(s, m) >= 0) hi = m; else lo = m; }
        rise = hi;
    }
    while (t0 < jd + span) {
        double step = e0 < -35 ? 4 * minute : (e0 < -12 ? 2 * minute : minute);
        double t1 = t0 + step, e1 = elAt(s, t1);
        if (!rise && e0 < 0 && e1 >= 0) {
            double lo = t0, hi = t1;
            for (int k = 0; k < 14; k++) { double m = (lo + hi) / 2; if (elAt(s, m) >= 0) hi = m; else lo = m; }
            rise = hi;
        } else if (rise && e0 >= 0 && e1 < 0) {
            double lo = t0, hi = t1;
            for (int k = 0; k < 14; k++) { double m = (lo + hi) / 2; if (elAt(s, m) >= 0) lo = m; else hi = m; }
            double set = lo;
            double a = rise, b = set;
            const double g = 0.6180339887;
            double c = b - g * (b - a), d = a + g * (b - a);
            double ec = elAt(s, c), ed = elAt(s, d);
            for (int k = 0; k < 22; k++) {
                if (ec > ed) { b = d; d = c; ed = ec; c = b - g * (b - a); ec = elAt(s, c); }
                else { a = c; c = d; ec = ed; d = a + g * (b - a); ed = elAt(s, d); }
            }
            double tmax = (a + b) / 2;
            SatNow o;
            nowOf(s, tmax, o);
            if (o.el >= minEl) {
                out.valid = true;
                out.jdStart = rise; out.jdStop = set; out.jdMax = tmax; out.maxEl = o.el; out.azMax = o.az;
                nowOf(s, rise, o); out.azStart = o.az;
                nowOf(s, set, o); out.azStop = o.az;
                for (int k = 0; k <= 12 && !out.visible; k++) {
                    nowOf(s, rise + (set - rise) * k / 12.0, o);
                    if (o.vis > 0) out.visible = true;
                }
                return true;
            }
            rise = 0;
        }
        t0 = t1; e0 = e1;
    }
    return false;
}

bool satNextPass(int i, double jd, double minEl, SatPass &out)
{
    memset(&out, 0, sizeof(out));
    if (i < 0 || i >= g_n) return false;
    return findPass(g_sat[i], jd, minEl, out);
}

int satTrack(int i, double jd0, double jd1, int n, float *az, float *el)
{
    if (i < 0 || i >= g_n || n < 2) return 0;
    SatNow o;
    for (int k = 0; k < n; k++) {
        satCompute(i, jd0 + (jd1 - jd0) * k / (n - 1), o);
        az[k] = (float)o.az; el[k] = (float)o.el;
    }
    return n;
}

/* ---- thread-safe variants: work on a private copy of the propagator */
Sgp4 *satCloneFor(int i)
{
    if (i < 0 || i >= g_n) return nullptr;
    Sgp4 *c = new Sgp4(*g_sat[i]);
    return c;
}

void satFreeClone(Sgp4 *c) { delete c; }

int satPassesSafe(int i, double jd, double minEl, SatPass *out, int maxPasses, float *trackAz, float *trackEl, int trackN)
{
    Sgp4 *c = satCloneFor(i);
    if (!c) return 0;
    int n = 0;
    double t = jd;
    while (n < maxPasses && findPass(c, t, minEl, out[n])) {
        t = out[n].jdStop + 1.0 / 1440.0;
        n++;
    }
    if (n && trackAz && trackN > 1) {
        SatNow o;
        for (int k = 0; k < trackN; k++) {
            nowOf(c, out[0].jdStart + (out[0].jdStop - out[0].jdStart) * k / (trackN - 1), o);
            trackAz[k] = (float)o.az; trackEl[k] = (float)o.el;
        }
    }
    satFreeClone(c);
    return n;
}

int satsSerialize(char *buf, int cap)
{
    int len = 0;
    for (int i = 0; i < g_n && len < cap - 1; i++)
        len += snprintf(buf + len, cap - len, "%s\n%s\n%s\n", g_sat[i]->satName, g_sat[i]->line1, g_sat[i]->line2);
    return len;
}
