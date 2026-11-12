/**
 * skyview.cpp - draws the sky as a window behind the watch: ground, glowing horizon,
 * constellation figures, colour-true stars, planets, a phase-correct Moon, Messier
 * objects, satellites with their predicted track, and a fighter-jet style HUD.
 */
#include "skyview.h"
#include "theme.h"
#include "catalog_data.h"
#include "sats.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

using namespace astro;

static lv_obj_t *g_obj = nullptr;
static Camera g_cam;
static SkyHud g_hud;
static uint32_t g_ms = 0;
static ObjRef g_lock, g_target, g_align;
static bool g_alignNorth = false;
static ObjInfo g_lockInfo;
static uint32_t g_lockInfoMs = 0, g_lockSinceMs = 0;
static char g_fov[12] = "60\xC2\xB0";

/* predicted satellite tracks, refreshed every few seconds */
#define TRACK_N 40
static float g_trackAz[SAT_MAX][TRACK_N], g_trackEl[SAT_MAX][TRACK_N];
static float g_nextAz[SAT_MAX][TRACK_N], g_nextEl[SAT_MAX][TRACK_N];
static bool g_trackValid[SAT_MAX];
static double g_trackJd0 = 0;          // start time of the track being built
static int g_buildSat = 0, g_buildK = 0;

/* Build the next satellite path a few SGP4 points per frame instead of all at once. */
static void stepTracks(double jd)
{
    int n = satsCount() < SAT_MAX ? satsCount() : SAT_MAX;
    if (n == 0) return;
    if (g_buildSat == 0 && g_buildK == 0) g_trackJd0 = jd;
    for (int step = 0; step < 6; step++) {
        SatNow o;
        satCompute(g_buildSat, g_trackJd0 + (10.0 / 1440.0) * g_buildK / (TRACK_N - 1), o);
        g_nextAz[g_buildSat][g_buildK] = (float)o.az;
        g_nextEl[g_buildSat][g_buildK] = (float)o.el;
        if (++g_buildK == TRACK_N) {
            memcpy(g_trackAz[g_buildSat], g_nextAz[g_buildSat], sizeof(g_trackAz[0]));
            memcpy(g_trackEl[g_buildSat], g_nextEl[g_buildSat], sizeof(g_trackEl[0]));
            g_trackValid[g_buildSat] = true;
            g_buildK = 0;
            if (++g_buildSat >= n) { g_buildSat = 0; return; }
        }
    }
}

/* Cached object info for the find target (skyObjInfo is not cheap). */
static ObjRef g_tInfoRef;
static ObjInfo g_tInfo;
static uint32_t g_tInfoMs = 0;
static const ObjInfo &targetInfo()
{
    if (!(g_tInfoRef == g_target) || g_ms - g_tInfoMs > 500) {
        skyObjInfo(g_target, g_tInfo);
        g_tInfoRef = g_target;
        g_tInfoMs = g_ms;
    }
    return g_tInfo;
}

/* ------------------------------------------------------------------ draw helpers
 * Every primitive is recorded into a compact display list the first time a frame is drawn
 * and replayed (culled to the stripe) for the remaining stripes. With a full-screen buffer
 * (T-Watch Ultra, PSRAM) there is one stripe; on boards without PSRAM (ESP32-C6) the scene
 * maths then runs once per frame instead of once per stripe. */
static lv_layer_t *L;

enum CmdT : uint8_t { K_LINE, K_DISC, K_RECT, K_ARC, K_TRI, K_TEXT };
struct Cmd {
    uint8_t t, opa, w, flags;       // flags: line round / text align / rect border opa
    int16_t v[6];                   // geometry, x4 sub-pixel for floats
    int16_t bb[4];                  // bounding box (screen px)
    uint32_t col, col2;
};
#if defined(BOARD_HAS_PSRAM) || !defined(ARDUINO)
#define MAX_CMDS 1600
#define TEXT_POOL 6144
#else
#define MAX_CMDS 1000              // no PSRAM: keep heap for Wi-Fi + TLS
#define TEXT_POOL 4096
#endif
static Cmd *g_cmds = nullptr;
static int g_ncmd = 0;
static char *g_text = nullptr;
static int g_ntext = 0;
static uint32_t g_serial = 1, g_recSerial = 0;
static bool g_recording = false, g_overflow = false;
int g_skyCmdPeak = 0;
static const lv_font_t *const kFonts[] = { &f_mono13, &f_mono16, &f_ui16, &f_ui20, &f_ui24, &f_orb18, &f_orb26 };

static int fontId(const lv_font_t *f)
{
    for (int i = 0; i < (int)(sizeof kFonts / sizeof kFonts[0]); i++)
        if (kFonts[i] == f) return i;
    return 0;
}
static inline int16_t q4(float v) { v *= 4; return (int16_t)(v > 32000 ? 32000 : (v < -32000 ? -32000 : v)); }
static inline float uq(int16_t v) { return v / 4.0f; }

static bool visible(const Cmd &c)
{
    const lv_area_t &a = L->_clip_area;
    return !(c.bb[2] < a.x1 || c.bb[0] > a.x2 || c.bb[3] < a.y1 || c.bb[1] > a.y2);
}

static void exec(const Cmd &c)
{
    if (!visible(c)) return;
    switch (c.t) {
    case K_LINE: {
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.p1.x = uq(c.v[0]); d.p1.y = uq(c.v[1]); d.p2.x = uq(c.v[2]); d.p2.y = uq(c.v[3]);
        d.color = th_col(c.col); d.opa = c.opa; d.width = c.w;
        d.round_start = d.round_end = c.flags & 1;
        lv_draw_line(L, &d);
        break;
    }
    case K_DISC: {
        lv_draw_fill_dsc_t d;
        lv_draw_fill_dsc_init(&d);
        d.color = th_col(c.col); d.opa = c.opa; d.radius = LV_RADIUS_CIRCLE;
        lv_area_t a = { c.v[0], c.v[1], c.v[2], c.v[3] };
        lv_draw_fill(L, &d, &a);
        break;
    }
    case K_RECT: {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = th_col(c.col); d.bg_opa = c.opa; d.radius = c.v[4];
        if (c.w) { d.border_color = th_col(c.col2); d.border_opa = c.flags; d.border_width = c.w; }
        lv_area_t a = { c.v[0], c.v[1], c.v[2], c.v[3] };
        lv_draw_rect(L, &d, &a);
        break;
    }
    case K_ARC: {
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.center.x = c.v[0]; d.center.y = c.v[1]; d.radius = c.v[2];
        d.start_angle = c.v[3]; d.end_angle = c.v[4]; d.color = th_col(c.col); d.opa = c.opa; d.width = c.w;
        lv_draw_arc(L, &d);
        break;
    }
    case K_TRI: {
        lv_draw_triangle_dsc_t d;
        lv_draw_triangle_dsc_init(&d);
        for (int i = 0; i < 3; i++) { d.p[i].x = uq(c.v[i * 2]); d.p[i].y = uq(c.v[i * 2 + 1]); }
        d.color = th_col(c.col); d.opa = c.opa;
        lv_draw_triangle(L, &d);
        break;
    }
    case K_TEXT: {
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.text = g_text + (uint16_t)c.v[4]; d.text_local = 1; d.font = kFonts[c.w]; d.color = th_col(c.col);
        d.opa = c.opa; d.letter_space = (int8_t)c.flags;
        lv_area_t a = { c.bb[0], c.bb[1], c.bb[2], c.bb[3] };
        lv_draw_label(L, &d, &a);
        break;
    }
    }
}

static void emit(Cmd &c)
{
    if (g_recording && g_cmds) {
        if (g_ncmd < MAX_CMDS) g_cmds[g_ncmd++] = c;
        else g_overflow = true;
    }
    exec(c);
}

static void bbox(Cmd &c, float x0, float y0, float x1, float y1, float pad)
{
    c.bb[0] = (int16_t)fmaxf(-30000, fminf(x0, x1) - pad); c.bb[1] = (int16_t)fmaxf(-30000, fminf(y0, y1) - pad);
    c.bb[2] = (int16_t)fminf(30000, fmaxf(x0, x1) + pad); c.bb[3] = (int16_t)fminf(30000, fmaxf(y0, y1) + pad);
}

static void dLine(float x1, float y1, float x2, float y2, uint32_t col, lv_opa_t opa, int w = 1, bool round = false)
{
    Cmd c = {};
    c.t = K_LINE; c.opa = opa; c.w = (uint8_t)w; c.flags = round; c.col = col;
    c.v[0] = q4(x1); c.v[1] = q4(y1); c.v[2] = q4(x2); c.v[3] = q4(y2);
    bbox(c, x1, y1, x2, y2, w + 2);
    emit(c);
}

static void dDisc(float x, float y, float r, uint32_t col, lv_opa_t opa)
{
    if (r < 0.5f) r = 0.5f;
    Cmd c = {};
    c.t = K_DISC; c.opa = opa; c.col = col;
    c.v[0] = (int16_t)lroundf(x - r); c.v[1] = (int16_t)lroundf(y - r);
    c.v[2] = (int16_t)lroundf(x + r) - 1; c.v[3] = (int16_t)lroundf(y + r) - 1;
    if (c.v[2] < c.v[0]) c.v[2] = c.v[0];
    if (c.v[3] < c.v[1]) c.v[3] = c.v[1];
    bbox(c, c.v[0], c.v[1], c.v[2], c.v[3], 1);
    emit(c);
}

static void dRect(int x, int y, int w, int h, uint32_t col, lv_opa_t opa, int radius = 0, uint32_t border = 0,
                  lv_opa_t bopa = 0, int bw = 0)
{
    Cmd c = {};
    c.t = K_RECT; c.opa = opa; c.col = col; c.col2 = border; c.flags = bopa; c.w = (uint8_t)bw;
    c.v[0] = x; c.v[1] = y; c.v[2] = x + w - 1; c.v[3] = y + h - 1; c.v[4] = radius;
    bbox(c, x, y, x + w, y + h, 1);
    emit(c);
}

static void dArc(float x, float y, int r, float a0, float a1, uint32_t col, lv_opa_t opa, int w = 2)
{
    Cmd c = {};
    c.t = K_ARC; c.opa = opa; c.col = col; c.w = (uint8_t)w;
    c.v[0] = (int16_t)lroundf(x); c.v[1] = (int16_t)lroundf(y); c.v[2] = r;
    c.v[3] = (int16_t)fmodf(a0, 360.0f); c.v[4] = (int16_t)(c.v[3] + (a1 - a0));
    bbox(c, x - r, y - r, x + r, y + r, w + 1);
    emit(c);
}

static void dTri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t col, lv_opa_t opa)
{
    Cmd c = {};
    c.t = K_TRI; c.opa = opa; c.col = col;
    c.v[0] = q4(x0); c.v[1] = q4(y0); c.v[2] = q4(x1); c.v[3] = q4(y1); c.v[4] = q4(x2); c.v[5] = q4(y2);
    bbox(c, fminf(x0, fminf(x1, x2)), fminf(y0, fminf(y1, y2)), fmaxf(x0, fmaxf(x1, x2)), fmaxf(y0, fmaxf(y1, y2)), 1);
    emit(c);
}

static int textW(const char *t, const lv_font_t *f, int ls = 0)
{
    lv_point_t s;
    lv_text_get_size(&s, t, f, ls, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return s.x;
}

/* align: 0 left, 1 centre, 2 right. y = top. */
static void dText(const char *t, float x, float y, const lv_font_t *f, uint32_t col, lv_opa_t opa = 255, int align = 0,
                  int ls = 0)
{
    int len = (int)strlen(t);
    if (!g_text || g_ntext + len + 1 > TEXT_POOL) { if (g_recording) g_overflow = true; return; }
    int w = textW(t, f, ls) + 2, h = lv_font_get_line_height(f);
    int x0 = align == 1 ? (int)(x - w / 2) : (align == 2 ? (int)(x - w) : (int)x);
    Cmd c = {};
    c.t = K_TEXT; c.opa = opa; c.col = col; c.w = (uint8_t)fontId(f); c.flags = (uint8_t)(int8_t)ls;
    c.v[4] = (int16_t)g_ntext;
    memcpy(g_text + g_ntext, t, len + 1);
    g_ntext += len + 1;
    c.bb[0] = x0; c.bb[1] = (int16_t)y; c.bb[2] = x0 + w; c.bb[3] = (int16_t)y + h;
    emit(c);
}

/* ---------- label de-cluttering */
struct Rect { int16_t x0, y0, x1, y1; };
static Rect g_rects[64];
static int g_nrects = 0;
static bool place(int x0, int y0, int x1, int y1)
{
    for (int i = 0; i < g_nrects; i++) {
        const Rect &r = g_rects[i];
        if (x0 < r.x1 && x1 > r.x0 && y0 < r.y1 && y1 > r.y0) return false;
    }
    if (g_nrects < 64) g_rects[g_nrects++] = { (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1 };
    return true;
}
/* HUD zones: always reserved, even for forced labels */
static Rect g_hudz[6];
static int g_nhud = 0;
static void reserve(int x0, int y0, int x1, int y1)
{
    if (g_nhud < 6) g_hudz[g_nhud++] = { (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1 };
}
static bool hitsHud(int x0, int y0, int x1, int y1)
{
    for (int i = 0; i < g_nhud; i++) {
        const Rect &r = g_hudz[i];
        if (x0 < r.x1 && x1 > r.x0 && y0 < r.y1 && y1 > r.y0) return true;
    }
    return false;
}

static bool safeArea(float x, float y, float pad = 0)
{
    /* inside the display's rounded rectangle, inset by pad */
    const float R = SAFE_R;
    float cx = x < R ? R : (x > SCR_W - R ? SCR_W - R : x);
    float cy = y < R ? R : (y > SCR_H - R ? SCR_H - R : y);
    float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= (R - pad) * (R - pad) && x >= pad && y >= pad && x <= SCR_W - pad && y <= SCR_H - pad;
}

static bool labelAt(const char *t, float x, float y, const lv_font_t *f, uint32_t col, lv_opa_t opa, int ls = 0,
                    bool force = false)
{
    int w = textW(t, f, ls), h = lv_font_get_line_height(f);
    int x0 = (int)x, y0 = (int)y;
    if (!safeArea(x0, y0, 4) || !safeArea(x0 + w, y0 + h, 4)) return false;
    if (hitsHud(x0 - 2, y0, x0 + w + 2, y0 + h)) return false;
    if (!place(x0 - 2, y0, x0 + w + 2, y0 + h) && !force) return false;
    dText(t, x0, y0, f, col, opa, 0, ls);
    return true;
}

/* ------------------------------------------------------------------ state setters */
void skyviewSetCamera(const Camera &c) { g_cam = c; }
const Camera &skyviewCamera() { return g_cam; }
void skyviewSetHud(const SkyHud &h) { g_hud = h; }
void skyviewSetTime(uint32_t ms) { g_ms = ms; g_serial++; stepTracks(skyJd()); }
ObjRef skyviewLock() { return g_lock; }
void skyviewClearLock() { g_lock = ObjRef(); }
void skyviewSetTarget(const ObjRef &t) { g_target = t; }
ObjRef skyviewTarget() { return g_target; }
const ObjInfo &skyviewTargetInfo() { return targetInfo(); }
void skyviewSetAlignTarget(const ObjRef &t, bool north) { g_align = t; g_alignNorth = north; }
void skyviewSetFovLabel(const char *s) { snprintf(g_fov, sizeof g_fov, "%s", s); }

double skyviewTargetDistanceDeg()
{
    if (!g_target.valid()) return 0;
    Vec3 t = skyObjEnu(g_target);
    return acos(fmax(-1.0, fmin(1.0, dot(t, g_cam.fwd)))) * R2D;
}

bool skyviewUpdateLock()
{
    double r = g_cam.fovDeg * 0.07;
    if (r < 1.2) r = 1.2;
    ObjRef n = skyPick(g_cam.fwd, r);
    if (g_lock.valid() && !(n == g_lock)) {
        Vec3 v = skyObjEnu(g_lock);
        double ang = acos(fmax(-1.0, fmin(1.0, dot(v, g_cam.fwd)))) * R2D;
        if (ang < r * 1.6) n = g_lock;               // hysteresis
    }
    bool changed = !(n == g_lock);
    g_lock = n;
    if (changed) { g_lockSinceMs = g_ms; g_lockInfoMs = 0; }
    if (g_lock.valid() && (changed || g_ms - g_lockInfoMs > 700)) {
        skyObjInfo(g_lock, g_lockInfo);
        g_lockInfoMs = g_ms;
    }
    return changed;
}

/* ------------------------------------------------------------------ fast float projection
 * (the ESP32-C6 has no FPU at all; single precision is several times cheaper than double) */
static float g_fF[3], g_fU[3], g_fR[3], g_ff, g_fcx, g_fcy, g_fw, g_fh;
static void prepFloatCam()
{
    g_fF[0] = (float)g_cam.fwd.x; g_fF[1] = (float)g_cam.fwd.y; g_fF[2] = (float)g_cam.fwd.z;
    g_fU[0] = (float)g_cam.up.x; g_fU[1] = (float)g_cam.up.y; g_fU[2] = (float)g_cam.up.z;
    g_fR[0] = (float)g_cam.right.x; g_fR[1] = (float)g_cam.right.y; g_fR[2] = (float)g_cam.right.z;
    g_ff = (float)g_cam.f; g_fcx = (float)g_cam.cx; g_fcy = (float)g_cam.cy; g_fw = (float)g_cam.w; g_fh = (float)g_cam.h;
}
static inline bool projF(const float *v, float &x, float &y, float margin)
{
    float z = v[0] * g_fF[0] + v[1] * g_fF[1] + v[2] * g_fF[2];
    if (z < 0.05f) return false;
    float iz = g_ff / z;
    x = g_fcx + iz * (v[0] * g_fR[0] + v[1] * g_fR[1] + v[2] * g_fR[2]);
    y = g_fcy - iz * (v[0] * g_fU[0] + v[1] * g_fU[1] + v[2] * g_fU[2]);
    return !(x < -margin || x > g_fw + margin || y < -margin || y > g_fh + margin);
}

/* ------------------------------------------------------------------ layers */
static float magLimit()
{
    double f = g_cam.fovDeg;
    return f > 75 ? 4.3f : (f > 45 ? 4.8f : 5.3f);
}

static float zoomBoost() { return (float)pow(60.0 / g_cam.fovDeg, 0.35); }

static void drawGround()
{
    /* ground half-plane: value(x,y) = fwd.z + a*right.z + b*up.z < 0 with a=(x-cx)/f, b=-(y-cy)/f */
    const Camera &c = g_cam;
    auto val = [&](float x, float y) { return c.fwd.z + (x - c.cx) / c.f * c.right.z - (y - c.cy) / c.f * c.up.z; };
    float px[4] = { 0, (float)c.w, (float)c.w, 0 }, py[4] = { 0, 0, (float)c.h, (float)c.h };
    float ox[8], oy[8];
    int n = 0;
    float hx[2], hy[2];
    int nh = 0;
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        double vi = val(px[i], py[i]), vj = val(px[j], py[j]);
        if (vi < 0) { ox[n] = px[i]; oy[n] = py[i]; n++; }
        if ((vi < 0) != (vj < 0)) {
            double t = vi / (vi - vj);
            float ix = px[i] + (px[j] - px[i]) * t, iy = py[i] + (py[j] - py[i]) * t;
            ox[n] = ix; oy[n] = iy; n++;
            if (nh < 2) { hx[nh] = ix; hy[nh] = iy; nh++; }
        }
    }
    /* behind-the-camera case: the whole screen is ground if looking straight down */
    for (int i = 1; i + 1 < n; i++) dTri(ox[0], oy[0], ox[i], oy[i], ox[i + 1], oy[i + 1], C_GROUND, 255);
    if (nh == 2) {
        /* soft glow band on the ground side */
        float dx = hx[1] - hx[0], dy = hy[1] - hy[0], len = sqrtf(dx * dx + dy * dy);
        if (len > 1) {
            float nx = -dy / len, ny = dx / len;          // normal; make it point into the ground
            if (val(hx[0] + nx * 10, hy[0] + ny * 10) > 0) { nx = -nx; ny = -ny; }
            for (int k = 1; k <= 3; k++) {
                float o = k * 5.0f;
                dLine(hx[0] + nx * o, hy[0] + ny * o, hx[1] + nx * o, hy[1] + ny * o, C_HORIZON, 26 - k * 6, 6);
            }
            dLine(hx[0], hy[0], hx[1], hy[1], C_HORIZON, 70, 5);
            dLine(hx[0], hy[0], hx[1], hy[1], C_HORIZON, 230, 2);
        }
    }
    /* cardinal points + azimuth ticks on the horizon */
    for (int az = 0; az < 360; az += 15) {
        Vec3 v = azAltToEnu(az, 0);
        float x, y;
        if (!c.project(v, x, y, 0)) continue;
        bool card = az % 90 == 0, inter = az % 45 == 0;
        Vec3 v2 = azAltToEnu(az, card ? 2.2 : 1.0);
        float x2, y2;
        if (c.project(v2, x2, y2, 50)) dLine(x, y, x2, y2, C_HORIZON, card ? 255 : 150, card ? 2 : 1);
        if (inter) {
            static const char *nm[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
            const char *t = nm[az / 45];
            Vec3 v3 = azAltToEnu(az, -2.0);
            float x3, y3;
            if (c.project(v3, x3, y3, 0)) {
                const lv_font_t *f = card ? &f_orb26 : &f_orb18;
                int w = textW(t, f);
                if (safeArea(x3, y3, 20) && !hitsHud((int)x3 - w / 2, (int)y3, (int)x3 + w / 2, (int)y3 + 26)) {
                    dText(t, x3 - w / 2, y3 - 2, f, card ? (az == 0 ? C_ACCENT2 : C_ACCENT) : C_ACCENT, card ? 255 : 180);
                    place((int)x3 - w / 2, (int)y3, (int)x3 + w / 2, (int)y3 + lv_font_get_line_height(f));
                }
            }
        }
    }
    /* zenith marker */
    float zx, zy;
    if (c.project({ 0, 0, 1 }, zx, zy, 0)) {
        dArc(zx, zy, 7, 0, 360, C_HUD_DIM, 160, 1);
        dLine(zx - 11, zy, zx - 4, zy, C_HUD_DIM, 160);
        dLine(zx + 4, zy, zx + 11, zy, C_HUD_DIM, 160);
        labelAt("ZENITH", zx + 10, zy + 4, &f_mono13, C_HUD_DIM, 200, 1);
    }
}

static void drawGrid()
{
    const Camera &c = g_cam;
    for (int alt = 30; alt <= 60; alt += 30) {
        float lx = 0, ly = 0;
        bool have = false;
        for (int az = 0; az <= 360; az += 4) {
            float x, y;
            bool ok = c.project(azAltToEnu(az, alt), x, y, 400);
            if (ok && have) dLine(lx, ly, x, y, C_GRID, 200);
            have = ok; lx = x; ly = y;
        }
    }
    for (int az = 0; az < 360; az += 30) {
        float lx = 0, ly = 0;
        bool have = false;
        for (int alt = 0; alt <= 84; alt += 6) {
            float x, y;
            bool ok = c.project(azAltToEnu(az, alt), x, y, 400);
            if (ok && have) dLine(lx, ly, x, y, C_GRID, 160);
            have = ok; lx = x; ly = y;
        }
    }
}

static void drawConstellations(bool names)
{
    const Camera &c = g_cam;
    for (uint16_t i = 0; i < CAT_NSEGS; i++) {
        const float *a = skyStarEnu(CAT_SEGS[i][0]), *b = skyStarEnu(CAT_SEGS[i][1]);
        float x1, y1, x2, y2;
        if (!projF(a, x1, y1, 900) || !projF(b, x2, y2, 900)) continue;
        if ((x1 < 0 && x2 < 0) || (x1 > c.w && x2 > c.w) || (y1 < 0 && y2 < 0) || (y1 > c.h && y2 > c.h)) continue;
        /* shorten the segment so it does not touch the star dots */
        float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy);
        if (len < 14) continue;
        float s = 5.5f / len;
        bool below = (a[2] < 0 && b[2] < 0);
        dLine(x1 + dx * s, y1 + dy * s, x2 - dx * s, y2 - dy * s, C_CONLINE, below ? 45 : 120, 1);
    }
    if (!names) return;
    for (uint8_t i = 0; i < CAT_NCONS; i++) {
        if (!CAT_CONS[i].count) continue;
        /* centroid direction from the J2000 average, precession ignored (labels only) */
        Vec3 eq = radecToVec(CAT_CONS[i].ra, CAT_CONS[i].dec);
        Vec3 v = mul(equatorialToEnu(skyJd(), skyObserver().lat, skyObserver().lon), eq);
        float x, y;
        if (!c.project(v, x, y, 0)) continue;
        char up[24];
        int k = 0;
        for (const char *p = CAT_CONS[i].name; *p && k < 22; p++) up[k++] = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p;
        up[k] = 0;
        int w = textW(up, &f_mono13, 2);
        labelAt(up, x - w / 2, y, &f_mono13, C_CONNAME, v.z < 0 ? 70 : 170, 2);
    }
}

static void starDot(float x, float y, float mag, uint32_t col, bool below)
{
    float zb = zoomBoost();
    float r = (3.7f - 0.62f * mag) * zb;
    if (r < 0.9f) r = 0.9f;
    lv_opa_t opa = below ? 70 : 255;
    if (mag < 1.6f && !below) {
        float tw = 1.0f + 0.08f * sinf(g_ms * 0.004f + x * 0.37f + y * 0.11f);   // gentle scintillation
        dDisc(x, y, r * 3.2f * tw, col, 22);
        dDisc(x, y, r * 1.9f, col, 55);
    } else if (mag < 3.0f && !below) {
        dDisc(x, y, r * 2.0f, col, 34);
    }
    /* core: blend the colour toward white for the hot centre */
    uint8_t cr = (col >> 16) & 0xFF, cg = (col >> 8) & 0xFF, cb = col & 0xFF;
    uint32_t core = ((uint32_t)((cr + 255 * 2) / 3) << 16) | ((uint32_t)((cg + 255 * 2) / 3) << 8) | ((cb + 255 * 2) / 3);
    dDisc(x, y, r, core, opa);
}

static void drawStars()
{
    const Camera &c = g_cam;
    float lim = magLimit();
    int lblMode = 1;
    extern uint8_t g_labelDensity;
    lblMode = g_labelDensity;
    float nameLim = (c.fovDeg > 75 ? 1.3f : (c.fovDeg > 45 ? 2.1f : 3.0f)) + (lblMode - 1) * 0.8f;
    extern bool g_showBelow;
    for (uint16_t i = 0; i < skyStarCount(); i++) {
        float m = skyStarMag(i);
        if (m > lim) break;                       // catalogue is sorted by magnitude
        const float *p = skyStarEnu(i);
        bool below = p[2] < -0.005f;
        if (below && !g_showBelow) continue;
        float x, y;
        if (!projF(p, x, y, 8)) continue;
        starDot(x, y, m, skyStarColor(i), below);
        const char *nm = skyStarName(i);
        if (nm[0] && m < nameLim && !below) labelAt(nm, x + 7, y - 9, &f_ui16, C_TEXT_DIM, 230);
    }
}

static void drawDso()
{
    const Camera &c = g_cam;
    extern bool g_showBelow;
    double lim = c.fovDeg > 75 ? 5.0 : (c.fovDeg > 45 ? 7.5 : 10.0);
    for (uint8_t i = 0; i < CAT_NDSO; i++) {
        double m = CAT_DSO[i].mag100 / 100.0;
        if (m > lim) continue;
        Vec3 v = skyDsoEnu(i);
        if (v.z < 0 && !g_showBelow) continue;
        float x, y;
        if (!c.project(v, x, y, 0)) continue;
        lv_opa_t o = v.z < 0 ? 60 : 200;
        float r = 5.5f * zoomBoost();
        /* dashed ring */
        for (int k = 0; k < 6; k++) dArc(x, y, (int)r, k * 60, k * 60 + 34, C_DSO, o, 1);
        char t[8];
        snprintf(t, sizeof t, "M%u", CAT_DSO[i].m);
        if (v.z >= 0) labelAt(t, x + r + 3, y - 7, &f_mono13, C_DSO, 200);
    }
}

static void drawMoon(float x, float y, float R, const Vec3 &moon, const Vec3 &sun, double phase, bool below)
{
    lv_opa_t o = below ? 90 : 255;
    dDisc(x, y, R * 2.4f, 0xE8ECF2, below ? 8 : 18);         // halo
    dDisc(x, y, R, 0x272D35, o);                              // earthshine
    /* sun direction projected on the screen around the Moon */
    Vec3 s = { sun.x - moon.x * dot(sun, moon), sun.y - moon.y * dot(sun, moon), sun.z - moon.z * dot(sun, moon) };
    float sx = (float)dot(s, g_cam.right), sy = -(float)dot(s, g_cam.up);
    float sl = sqrtf(sx * sx + sy * sy);
    if (sl < 1e-6f) { sx = 1; sy = 0; } else { sx /= sl; sy /= sl; }
    float tx = -sy, ty = sx;
    double ci = 2 * phase - 1;                                 // cos(phase angle)
    int n = (int)(R * 2);
    if (n < 6) n = 6;
    for (int k = 0; k <= n; k++) {
        float v = -1 + 2.0f * k / n;
        float wv = sqrtf(fmaxf(0, 1 - v * v));
        float u0 = (float)(-ci * wv), u1 = wv;
        if (u1 - u0 < 0.02f) continue;
        float ax = x + R * (u0 * sx + v * tx), ay = y + R * (u0 * sy + v * ty);
        float bx = x + R * (u1 * sx + v * tx), by = y + R * (u1 * sy + v * ty);
        dLine(ax, ay, bx, by, 0xE9EEF5, o, 2, true);
    }
    /* a couple of maria for texture */
    if (R > 9) {
        dDisc(x - R * 0.25f, y - R * 0.2f, R * 0.22f, 0xB9C1CB, (lv_opa_t)(o * 0.35f));
        dDisc(x + R * 0.18f, y + R * 0.1f, R * 0.16f, 0xB9C1CB, (lv_opa_t)(o * 0.3f));
    }
}

static void drawBodies()
{
    const Camera &c = g_cam;
    extern bool g_showBelow;
    Vec3 sun = skyBodyEnu(SUN);
    for (int b = BODY_COUNT - 1; b >= 0; b--) {
        Vec3 v = skyBodyEnu((Body)b);
        bool below = v.z < -0.005;
        if (below && !g_showBelow) continue;
        float x, y;
        if (!c.project(v, x, y, 30)) continue;
        uint32_t col = skyBodyColor((Body)b);
        const BodyState &s = skyBodyState((Body)b);
        float zb = zoomBoost();
        if (b == SUN) {
            float R = 13 * zb;
            dDisc(x, y, R * 3.6f, col, below ? 8 : 20);
            dDisc(x, y, R * 2.2f, col, below ? 14 : 45);
            for (int k = 0; k < 12; k++) {
                float a = k * 30 * (float)D2R + g_ms * 0.0003f;
                dLine(x + cosf(a) * R * 1.35f, y + sinf(a) * R * 1.35f, x + cosf(a) * R * 1.9f, y + sinf(a) * R * 1.9f,
                      col, below ? 60 : 180, 2, true);
            }
            dDisc(x, y, R, 0xFFF3C4, below ? 90 : 255);
            labelAt("SUN", x + R * 2, y - 8, &f_orb18, col, below ? 100 : 255);
        } else if (b == MOON) {
            float R = 12 * zb * (float)(s.angDiamArcsec / 1865.0);
            drawMoon(x, y, R, v, sun, s.phase, below);
            labelAt("MOON", x + R + 6, y - 8, &f_orb18, col, below ? 100 : 230);
        } else {
            float r = (float)(3.6 - 0.55 * s.mag) * zb;
            if (r < 2.2f) r = 2.2f;
            if (r > 7.5f) r = 7.5f;
            if (!below) { dDisc(x, y, r * 3.0f, col, 26); dDisc(x, y, r * 1.8f, col, 60); }
            dDisc(x, y, r, col, below ? 90 : 255);
            if (b == SATURN) {
                dLine(x - r * 2.1f, y + r * 0.55f, x + r * 2.1f, y - r * 0.55f, col, below ? 70 : 220, 2, true);
            }
            char nm[12];
            const char *n = bodyName((Body)b);
            int k = 0;
            for (; n[k] && k < 11; k++) nm[k] = (n[k] >= 'a' && n[k] <= 'z') ? n[k] - 32 : n[k];
            nm[k] = 0;
            labelAt(nm, x + r + 6, y - 9, &f_ui16, col, below ? 90 : 255, 1, true);
        }
    }
}

static void drawSats()
{
    const Camera &c = g_cam;
    for (int i = 0; i < satsCount() && i < SAT_MAX; i++) {
        Vec3 v;
        double rk;
        int16_t vis;
        if (!skySatEnu(i, v, rk, vis)) continue;
        /* predicted path: dotted, fading out into the future */
        for (int k = 1; k < TRACK_N && g_trackValid[i]; k++) {
            if (g_trackEl[i][k] < -2) continue;
            float x, y;
            if (!c.project(azAltToEnu(g_trackAz[i][k], g_trackEl[i][k]), x, y, 0)) continue;
            dDisc(x, y, 1.4f, C_SAT, (lv_opa_t)(200 - k * 4));
        }
        if (v.z < -0.02) continue;
        float x, y;
        if (!c.project(v, x, y, 20)) continue;
        bool lit = vis > 0;
        uint32_t col = lit ? C_SAT : 0x5C8C86;
        float p = 0.5f + 0.5f * sinf(g_ms * 0.008f);
        dDisc(x, y, 10 + 4 * p, col, 30);
        /* ISS-style glyph: body + two solar wings */
        dRect((int)x - 3, (int)y - 3, 6, 6, col, 255, 1);
        dRect((int)x - 13, (int)y - 5, 8, 10, col, 200);
        dRect((int)x + 5, (int)y - 5, 8, 10, col, 200);
        dLine(x - 5, y, x + 5, y, col, 255, 1);
        labelAt(satName(i), x + 16, y - 9, &f_ui16, col, 255, 1, true);
    }
}

/* ------------------------------------------------------------------ HUD */
static void drawReticle()
{
    float cx = g_cam.cx, cy = g_cam.cy;
    uint32_t col = C_HUD;
    if (g_lock.valid()) {
        col = g_lockInfo.color ? g_lockInfo.color : C_HUD;
        /* lock-on animation: brackets close in, then spin slowly */
        float t = (g_ms - g_lockSinceMs) / 1000.0f;
        float k = t < 0.35f ? 1 - t / 0.35f : 0;
        float r = 26 + 22 * k * k;
        float spin = t * 40;
        for (int q = 0; q < 4; q++) {
            float a0 = q * 90 + spin + 20, a1 = a0 + 50;
            dArc(cx, cy, (int)r, a0, a1, col, 255, 3);
        }
        dArc(cx, cy, (int)(r + 8), 0, 360, col, 40, 1);
    } else {
        dArc(cx, cy, 22, 0, 360, C_HUD, 150, 1);
    }
    /* cross ticks */
    dLine(cx - 40, cy, cx - 28, cy, col, 220, 2);
    dLine(cx + 28, cy, cx + 40, cy, col, 220, 2);
    dLine(cx, cy - 40, cx, cy - 28, col, 220, 2);
    dLine(cx, cy + 28, cx, cy + 40, col, 220, 2);
    dDisc(cx, cy, 1.6f, col, 255);
}

static void drawAltLadder(double alt)
{
    /* right-side altitude tape */
    const int x = 360, cy = g_cam.cy, span = 150;
    const double pxPerDeg = 3.0;
    dLine(x, cy - span, x, cy + span, C_HUD_DIM, 160, 1);
    int a0 = (int)floor((alt - span / pxPerDeg) / 5) * 5;
    for (int a = a0; a <= alt + span / pxPerDeg; a += 5) {
        if (a < -90 || a > 90) continue;
        float y = cy - (a - alt) * pxPerDeg;
        if (y < cy - span || y > cy + span) continue;
        bool major = a % 15 == 0;
        dLine(x, y, x + (major ? 12 : 6), y, a == 0 ? C_HORIZON : C_HUD, major ? 220 : 120, a == 0 ? 2 : 1);
        if (major && fabs(y - cy) > 14) {
            char t[8];
            snprintf(t, sizeof t, "%d", a);
            dText(t, x - 4, y - 8, &f_mono13, a == 0 ? C_HORIZON : C_HUD_DIM, 220, 2);
        }
    }
    /* current value box */
    char t[10];
    snprintf(t, sizeof t, "%+.0f\xC2\xB0", alt);
    dRect(x - 46, cy - 12, 44, 24, C_BG, 200, 6, C_HUD, 200, 1);
    dText(t, x - 24, cy - 9, &f_mono16, C_HUD, 255, 1);
    dTri(x - 2, cy - 6, x - 2, cy + 6, x + 6, cy, C_HUD, 255);
}

static void drawCompassTape(double az, int y)
{
    const int cx = SCR_W / 2, half = 130;
    const double pxPerDeg = 3.2;
    bool cal = g_hud.calibrated;
    uint32_t col = cal ? C_HUD : C_WARN;
    dLine(cx - half, y, cx + half, y, C_HUD_DIM, 140, 1);
    int a0 = (int)floor((az - half / pxPerDeg) / 5) * 5;
    for (int a = a0; a <= az + half / pxPerDeg; a += 5) {
        float x = cx + (a - az) * pxPerDeg;
        if (x < cx - half || x > cx + half) continue;
        int an = ((a % 360) + 360) % 360;
        bool major = an % 45 == 0;
        float fade = 1.0f - fabsf(x - cx) / half;
        lv_opa_t o = (lv_opa_t)(60 + 195 * fade);
        dLine(x, y, x, y - (major ? 12 : (an % 15 == 0 ? 8 : 4)), col, o, major ? 2 : 1);
        if (an % 15 == 0 && fabs(x - cx) > 18) {
            char t[6];
            if (major) snprintf(t, sizeof t, "%s", compassPoint(an));
            else snprintf(t, sizeof t, "%d", an);
            dText(t, x, y - 30, major ? &f_ui16 : &f_mono13, an == 0 ? C_ACCENT2 : col, o, 1);
        }
    }
    char t[12];
    snprintf(t, sizeof t, cal ? "%03.0f" : "~%03.0f", fmod(az + 360, 360));
    dRect(cx - 30, y - 34, 60, 24, C_BG, 220, 6, col, 220, 1);
    dText(t, cx, y - 31, &f_mono16, col, 255, 1);
    dTri(cx - 6, y + 2, cx + 6, y + 2, cx, y - 6, col, 255);
}

static void drawCard(int y)
{
    const ObjInfo &o = g_lockInfo;
    const int x = 40, w = SCR_W - 80, h = 104;
    uint32_t col = o.color ? o.color : C_HUD;
    float t = (g_ms - g_lockSinceMs) / 1000.0f;
    float slide = t < 0.25f ? (1 - t / 0.25f) : 0;
    y += (int)(30 * slide * slide);
    lv_opa_t a = (lv_opa_t)(255 * (1 - slide));
    dRect(x, y, w, h, C_CARD, (lv_opa_t)(225 * (1 - slide)), 20, col, (lv_opa_t)(150 * (1 - slide)), 1);
    dRect(x + 14, y + 16, 4, h - 32, col, a, 2);                  // accent bar
    dText(o.name, x + 28, y + 6, &f_ui24, C_TEXT, a);
    dText(o.kindText, x + 28, y + 34, &f_ui16, col, a);
    dText(o.line1, x + 28, y + 56, &f_mono13, C_TEXT_DIM, a);
    dText(o.line2, x + 28, y + 76, &f_mono13, C_TEXT_DIM, a);
}

static void drawFindArrow()
{
    if (!g_target.valid()) return;
    Vec3 t = skyObjEnu(g_target);
    double ang = acos(fmax(-1.0, fmin(1.0, dot(t, g_cam.fwd)))) * R2D;
    const ObjInfo &info = targetInfo();
    uint32_t col = info.color ? info.color : C_HUD;
    float x, y;
    bool on = g_cam.project(t, x, y, -30);
    if (on && ang < g_cam.fovDeg * 0.42) {
        /* target visible: pulsing ring around it */
        float p = fmodf(g_ms / 900.0f, 1.0f);
        dArc(x, y, (int)(14 + 24 * p), 0, 360, col, (lv_opa_t)(220 * (1 - p)), 2);
        dArc(x, y, 14, 0, 360, col, 230, 2);
        return;
    }
    float dx = (float)dot(t, g_cam.right), dy = -(float)dot(t, g_cam.up);
    float l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-6f) { dx = 0; dy = -1; } else { dx /= l; dy /= l; }
    float R = 150, cx = g_cam.cx, cy = g_cam.cy;
    float ax = cx + dx * R, ay = cy + dy * R;
    float bob = 6 * sinf(g_ms * 0.008f);
    ax += dx * bob; ay += dy * bob;
    float px = -dy, py = dx;
    dTri(ax + dx * 22, ay + dy * 22, ax + px * 14, ay + py * 14, ax - px * 14, ay - py * 14, col, 240);
    dTri(ax + dx * 4, ay + dy * 4, ax + px * 9 - dx * 6, ay + py * 9 - dy * 6, ax - px * 9 - dx * 6, ay - py * 9 - dy * 6,
         C_BG, 200);
    char s[40];
    snprintf(s, sizeof s, "%s  %.0f\xC2\xB0", info.name, ang);
    float lx = cx + dx * (R - 46), ly = cy + dy * (R - 46) - 10;
    int w = textW(s, &f_ui20);
    dRect((int)(lx - w / 2 - 10), (int)ly - 2, w + 20, 28, C_BG, 170, 12);
    dText(s, lx, ly, &f_ui20, col, 255, 1);
}

static void drawAlignBand()
{
    if (!g_align.valid() && !g_alignNorth) return;
    double tAlt = 0;
    if (g_align.valid()) {
        Vec3 t = skyObjEnu(g_align);
        double az;
        enuToAzAlt(t, az, tAlt);
    }
    double az, alt;
    g_cam.boresightAzAlt(az, alt);
    float y = g_cam.cy - (float)((tAlt - alt) * g_cam.f * D2R);
    if (y > 60 && y < SCR_H - 60) {
        for (int x = 50; x < SCR_W - 50; x += 14) dLine(x, y, x + 7, y, C_WARN, 200, 2);
        char s[24];
        snprintf(s, sizeof s, "ALT %.0f\xC2\xB0", tAlt);
        dText(s, 60, y - 22, &f_mono13, C_WARN, 230);
    }
}

static void drawTopBar()
{
    dText(g_hud.clock, SCR_W / 2, 14, &f_orb18, C_TEXT, 255, 1);
    dText(g_hud.status, SCR_W / 2, 38, &f_mono13, g_hud.gpsFix ? C_HUD_DIM : C_WARN, 230, 1);
    /* FOV chip, left; mode chip right */
    dRect(78, 22, 54, 24, C_BG, 160, 12, C_HUD_DIM, 200, 1);
    dText(g_fov, 105, 24, &f_mono16, C_HUD, 255, 1);
    if (g_hud.banner) {
        int w = textW(g_hud.banner, &f_ui20) + 28;
        int y = 64;
        dRect(SCR_W / 2 - w / 2, y, w, g_hud.banner2 ? 54 : 30, C_BG, 210, 14, g_hud.bannerColor, 230, 1);
        dText(g_hud.banner, SCR_W / 2, y + 3, &f_ui20, g_hud.bannerColor, 255, 1);
        if (g_hud.banner2) dText(g_hud.banner2, SCR_W / 2, y + 28, &f_mono13, C_TEXT_DIM, 255, 1);
    }
}

/* ------------------------------------------------------------------ frame */
static void drawScene();

static void drawCb(lv_event_t *e)
{
    L = lv_event_get_layer(e);
    if (!g_cmds) {
        g_cmds = (Cmd *)lv_malloc(sizeof(Cmd) * MAX_CMDS);
        g_text = (char *)lv_malloc(TEXT_POOL);
    }
    if (g_recSerial == g_serial && g_cmds && !g_overflow) {     // same frame, next stripe: replay
        for (int i = 0; i < g_ncmd; i++) exec(g_cmds[i]);
        return;
    }
    g_ncmd = 0;
    g_ntext = 0;
    g_overflow = false;
    g_recording = true;
    drawScene();                                 // (on overflow later stripes redraw the scene instead)
    g_recording = false;
    g_recSerial = g_serial;
    if (g_ncmd > g_skyCmdPeak) g_skyCmdPeak = g_ncmd;
}

static void drawScene()
{
    prepFloatCam();
    g_nrects = 0;
    g_nhud = 0;
    extern bool g_showConst, g_showGrid;
    /* reserve HUD areas so labels stay out of them */
    reserve(0, 0, SCR_W, 60);
    reserve(SCR_W - 76, g_cam.cy - 160, SCR_W, g_cam.cy + 160);
    reserve(SCR_W / 2 - 44, g_cam.cy - 44, SCR_W / 2 + 44, g_cam.cy + 44);
    if (g_lock.valid()) reserve(36, 338, SCR_W - 36, 452);
    reserve(60, SCR_H - 64, SCR_W - 60, SCR_H);
    if (g_hud.banner) reserve(40, 60, SCR_W - 40, g_hud.banner2 ? 122 : 98);

    drawGround();
    if (g_showGrid) drawGrid();
    if (g_showConst) drawConstellations(true);
    drawDso();
    drawStars();
    drawBodies();
    drawSats();
    drawAlignBand();
    drawFindArrow();
    drawReticle();
    double az, alt;
    g_cam.boresightAzAlt(az, alt);
    drawAltLadder(alt);
    drawCompassTape(az, SCR_H - 22);
    if (g_lock.valid()) drawCard(342);
    drawTopBar();
}

lv_obj_t *skyviewCreate(lv_obj_t *parent)
{
    g_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(g_obj);
    lv_obj_set_size(g_obj, SCR_W, SCR_H);
    lv_obj_set_pos(g_obj, 0, 0);
    lv_obj_add_flag(g_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_obj, drawCb, LV_EVENT_DRAW_MAIN, nullptr);
    g_cam.setFov(60);
    return g_obj;
}
