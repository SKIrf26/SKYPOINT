/**
 * ui.cpp - SkyPoint screens. Swipe left/right between:
 *   SKY      live AR view (skyview.cpp)
 *   TONIGHT  sun/moon times, planets, bright stars and deep-sky objects that are up
 *   ORBIT    next satellite passes with a polar sky plot and countdown
 *   SETUP    heading alignment, display options, time zone, GPS status, exit
 */
#include "ui.h"
#include "theme.h"
#include "device.h"
#include "orient.h"
#include "skyview.h"
#include "sats.h"
#include "catalog_data.h"
#include "bg.h"
#include "sound.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

using namespace astro;

bool g_nightMode = false;
bool g_showConst = true, g_showGrid = false, g_showBelow = true;
uint8_t g_labelDensity = 1;
uint32_t g_uiFrames = 0;   // sky frames drawn (for the serial fps readout)

static const double kFovs[] = { 90, 60, 35, 20 };
static const char *kFovNames[] = { "90\xC2\xB0", "60\xC2\xB0", "35\xC2\xB0", "20\xC2\xB0" };

static lv_obj_t *g_scr, *g_tv, *g_tileSky, *g_tileTonight, *g_tileOrbit, *g_tileGps = nullptr, *g_tileSetup, *g_sky, *g_dots;
/* page order: SKY, TONIGHT, ORBIT, [GPS], SETUP - the GPS page exists only on boards with a receiver */
enum { TILE_SKY = 0, TILE_TONIGHT = 1, TILE_ORBIT = 2 };
static int TILE_GPS = -1, TILE_SETUP = 3, g_nTiles = 4;
static int g_tile = 0;
static uint32_t g_lastFrame = 0, g_lastSkyUpdate = 0, g_lastSlow = 0;
static bool g_findFound = false;
static uint32_t g_findFoundMs = 0;
static double g_lastFindBucket = 999;

/* align flow */
static ObjRef g_alignTarget;
static bool g_alignNorth = false, g_aligning = false;

/* orbit screen state */
static int g_satSel = 0;
static SatPass g_pass[4];
static int g_npass = 0;
static double g_passComputedJd = 0;
static int g_passSat = -1;
static float g_plotAz[64], g_plotEl[64];
static int g_plotN = 0;
static lv_obj_t *g_orbName, *g_orbAge, *g_orbCount, *g_orbCountLbl, *g_orbDetail, *g_orbList, *g_orbPlot, *g_orbBadge,
    *g_orbTleBtnLbl;

/* tonight */
static lv_obj_t *g_tonightList;
static uint32_t g_tonightBuiltMs = 0;
static bool g_tonightBuilt = false;

/* setup */
static lv_obj_t *g_gpsLbl, *g_utcLbl, *g_axisLbl, *g_modeLbl, *g_labelsLbl, *g_alignRow;

/* pass alerts */
static double g_alertedPassJd = 0, g_alertedRiseJd = 0;

/* ------------------------------------------------------------------ helpers */
static int localOffsetMin(double unixUtc)
{
    Settings &s = devSettings();
    int off = s.utcOffsetMin;
    if (!s.autoDst) return off;
    /* US rule: 2nd Sunday of March 02:00 local standard -> 1st Sunday of November 02:00 local daylight */
    int y, mo, d, h, mi, se;
    unixToCivil((int64_t)(unixUtc + off * 60), y, mo, d, h, mi, se);
    auto nthSunday = [](int y, int m, int n) {
        int64_t t = civilToUnix(y, m, 1, 0, 0, 0);
        int dow = (int)(((t / 86400) + 4) % 7);   // 0 = Sunday
        int first = 1 + (7 - dow) % 7;
        return first + 7 * (n - 1);
    };
    int64_t local = (int64_t)(unixUtc + off * 60);
    int64_t start = civilToUnix(y, 3, nthSunday(y, 3, 2), 2, 0, 0);
    int64_t end = civilToUnix(y, 11, nthSunday(y, 11, 1), 1, 0, 0);
    return (local >= start && local < end) ? off + 60 : off;
}

static void fmtClock(char *b, size_t n, double unixUtc)
{
    int y, mo, d, h, mi, s;
    unixToCivil((int64_t)(unixUtc + localOffsetMin(unixUtc) * 60), y, mo, d, h, mi, s);
    snprintf(b, n, "%02d:%02d", h, mi);
}

static void fmtJd(char *b, size_t n, double jd)
{
    if (jd <= 0) { snprintf(b, n, "--:--"); return; }
    fmtClock(b, n, (jd - 2440587.5) * 86400.0);
}

static void fmtDate(char *b, size_t n, double unixUtc)
{
    static const char *mon[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    static const char *wd[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    double lu = unixUtc + localOffsetMin(unixUtc) * 60;
    int y, mo, d, h, mi, s;
    unixToCivil((int64_t)lu, y, mo, d, h, mi, s);
    int dow = (int)((((int64_t)lu / 86400) + 4) % 7);
    snprintf(b, n, "%s %d %s %d", wd[dow], d, mon[mo - 1], y);
}

static lv_obj_t *mkLabel(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t col)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, th_col(col), 0);
    lv_label_set_text(l, t);
    return l;
}

static void tapSound(lv_obj_t *o)
{
    lv_obj_add_event_cb(o, [](lv_event_t *) { sndPlay(SFX_TAP); }, LV_EVENT_CLICKED, nullptr);
}

static void toast(const char *msg, uint32_t col = C_HUD, uint32_t ms = 2600)
{
    lv_obj_t *t = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(t);
    lv_obj_set_style_bg_color(t, th_col(C_CARD), 0);
    lv_obj_set_style_bg_opa(t, 235, 0);
    lv_obj_set_style_radius(t, 18, 0);
    lv_obj_set_style_border_color(t, th_col(col), 0);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_pad_hor(t, 18, 0);
    lv_obj_set_style_pad_ver(t, 10, 0);
    lv_obj_set_size(t, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_t *l = mkLabel(t, msg, &f_ui20, col);
    lv_obj_set_style_max_width(l, 300, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_fade_in(t, 150, 0);
    lv_obj_delete_delayed(t, ms);
}

static void gotoTile(int i, bool anim = true)
{
    lv_tileview_set_tile_by_index(g_tv, i, 0, anim ? LV_ANIM_ON : LV_ANIM_OFF);
    g_tile = i;
}

static void startFind(const ObjRef &r)
{
    skyviewSetTarget(r);
    g_findFound = false;
    g_lastFindBucket = 999;
    gotoTile(0);
    ObjInfo o;
    skyObjInfo(r, o);
    char b[64];
    snprintf(b, sizeof b, "Finding %s", o.name);
    toast(b, o.color ? o.color : C_HUD, 1800);
    devHaptic(H_CLICK);
}

/* ------------------------------------------------------------------ SKY tile */
static void skyClick(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
    lv_point_t p = { 0, 0 };
    if (in) lv_indev_get_point(in, &p);
    if (skyviewTarget().valid() && p.y < 130) {           // tap the banner to stop finding
        skyviewSetTarget(ObjRef());
        devHaptic(H_SOFT);
        return;
    }
    if (g_aligning && p.y < 130) {
        g_aligning = false;
        skyviewSetAlignTarget(ObjRef(), false);
        toast("Alignment cancelled", C_TEXT_DIM);
        return;
    }
    Settings &s = devSettings();
    s.fovIndex = (s.fovIndex + 1) % 4;
    skyviewSetFovLabel(kFovNames[s.fovIndex]);
    devHaptic(H_TICK);
    sndPlay(SFX_ZOOM);
    devSaveSettings();
}

static void doAlign(const ObjRef &r, bool north)
{
    Settings &s = devSettings();
    if (north) orientAlignAzimuth(0, s.pointMode, s.axisRot);
    else orientAlignTo(skyObjEnu(r), s.pointMode, s.axisRot);
    orientMarkAlignTime((uint32_t)devUnixTime());
    g_aligning = false;
    skyviewSetAlignTarget(ObjRef(), false);
    char b[64];
    if (north) snprintf(b, sizeof b, "Heading set: north");
    else { ObjInfo o; skyObjInfo(r, o); snprintf(b, sizeof b, "Aligned on %s", o.name); }
    toast(b, C_OK);
    devHaptic(H_LOCK);
    sndPlay(SFX_ALIGN);
}

static void openAlignChooser();
static void syncOrSetup();

static void skyLongPress(lv_event_t *e)
{
    if (g_aligning) { doAlign(g_alignTarget, g_alignNorth); return; }
    ObjRef l = skyviewLock();
    if (l.valid() && orientCalibrated()) { doAlign(l, false); return; }
    openAlignChooser();
}

static void buildSky(lv_obj_t *tile)
{
    g_sky = skyviewCreate(tile);
    lv_obj_add_event_cb(g_sky, skyClick, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_sky, skyLongPress, LV_EVENT_LONG_PRESSED, nullptr);
    skyviewSetFovLabel(kFovNames[devSettings().fovIndex]);
}

/* ------------------------------------------------------------------ align chooser (modal) */
static lv_obj_t *g_modal = nullptr;

static void closeModal()
{
    if (g_modal) { lv_obj_delete(g_modal); g_modal = nullptr; }
}

static lv_obj_t *modalBase(const char *title)
{
    closeModal();
    g_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_modal);
    lv_obj_set_size(g_modal, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(g_modal, th_col(0x000000), 0);
    lv_obj_set_style_bg_opa(g_modal, 245, 0);
    lv_obj_t *t = mkLabel(g_modal, title, &f_orb26, C_HUD);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_t *list = lv_obj_create(g_modal);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, SCR_W - 70, SCR_H - 170);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *c = lv_button_create(g_modal);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 150, 46);
    lv_obj_set_style_radius(c, 23, 0);
    lv_obj_set_style_border_color(c, th_col(C_TEXT_DIM), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_t *cl = mkLabel(c, "CANCEL", &f_ui20, C_TEXT_DIM);
    lv_obj_center(cl);
    lv_obj_add_event_cb(c, [](lv_event_t *) { closeModal(); }, LV_EVENT_CLICKED, nullptr);
    tapSound(c);
    return list;
}

struct AlignChoice { ObjRef r; bool north; };
static AlignChoice g_choices[12];

static lv_obj_t *rowButton(lv_obj_t *list, const char *title, const char *sub, uint32_t col, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(list);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(b, th_col(C_CARD), 0);
    lv_obj_set_style_bg_opa(b, 255, 0);
    lv_obj_set_style_bg_color(b, th_col(0x10303A), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_pad_all(b, 12, 0);
    lv_obj_set_style_pad_left(b, 22, 0);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(b, 4, 0);
    lv_obj_set_style_border_color(b, th_col(col), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *t = mkLabel(b, title, &f_ui24, C_TEXT);
    (void)t;
    if (sub && sub[0]) mkLabel(b, sub, &f_mono13, col);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    tapSound(b);
    return b;
}

static void alignPick(lv_event_t *e)
{
    AlignChoice *c = (AlignChoice *)lv_event_get_user_data(e);
    g_alignTarget = c->r;
    g_alignNorth = c->north;
    g_aligning = true;
    skyviewSetAlignTarget(c->r, c->north);
    skyviewSetTarget(ObjRef());
    closeModal();
    gotoTile(0);
    devHaptic(H_CLICK);
}

static void openAlignChooser()
{
    lv_obj_t *list = modalBase("ALIGN");
    lv_obj_t *hint = mkLabel(list, "Pick something you can see. Centre it in the reticle, then long-press.",
                             &f_ui16, C_TEXT_DIM);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    int n = 0;
    char sub[48];
    /* Moon and planets above the horizon, then the brightest named stars */
    Body order[] = { MOON, VENUS, JUPITER, MARS, SATURN, MERCURY };
    for (Body b : order) {
        Vec3 v = skyBodyEnu(b);
        if (v.z < 0.05 || n >= 10) continue;
        double az, alt;
        enuToAzAlt(v, az, alt);
        g_choices[n] = { ObjRef(), false };
        g_choices[n].r.kind = b == MOON ? OK_MOON : OK_PLANET;
        g_choices[n].r.idx = b;
        snprintf(sub, sizeof sub, "ALT %.0f\xC2\xB0  MAG %.1f", alt, skyBodyState(b).mag);
        rowButton(list, bodyName(b), sub, skyBodyColor(b), alignPick, &g_choices[n]);
        n++;
    }
    for (uint16_t i = 0; i < skyStarCount() && n < 10; i++) {
        if (skyStarMag(i) > 1.6f) break;
        const float *p = skyStarEnu(i);
        if (p[2] < 0.1f || !skyStarName(i)[0]) continue;
        double az, alt;
        enuToAzAlt({ p[0], p[1], p[2] }, az, alt);
        g_choices[n] = { ObjRef(), false };
        g_choices[n].r.kind = OK_STAR;
        g_choices[n].r.idx = i;
        snprintf(sub, sizeof sub, "STAR  ALT %.0f\xC2\xB0  MAG %.1f", alt, skyStarMag(i));
        rowButton(list, skyStarName(i), sub, skyStarColor(i), alignPick, &g_choices[n]);
        n++;
    }
    g_choices[n] = { ObjRef(), true };
    rowButton(list, "Face north", "POINT AT THE NORTH HORIZON", C_ACCENT2, alignPick, &g_choices[n]);
}

/* ------------------------------------------------------------------ TONIGHT tile */
static lv_obj_t *section(lv_obj_t *p, const char *t)
{
    lv_obj_t *l = mkLabel(p, t, &f_mono13, C_HUD_DIM);
    lv_obj_set_style_text_letter_space(l, 3, 0);
    lv_obj_set_style_pad_top(l, 10, 0);
    return l;
}

static ObjRef g_rowRefs[40];
static int g_nRowRefs = 0;

static void findRowCb(lv_event_t *e) { startFind(*(ObjRef *)lv_event_get_user_data(e)); }

static void moonIconDraw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *o = (lv_obj_t *)lv_event_get_current_target(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    float cx = (a.x1 + a.x2) / 2.0f, cy = (a.y1 + a.y2) / 2.0f, R = (a.x2 - a.x1) / 2.0f - 2;
    double ph = skyBodyState(MOON).phase, age = moonAgeDays(skyJd());
    bool waxing = age < 14.765;
    if (skyObserver().lat < 0) waxing = !waxing;
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.radius = LV_RADIUS_CIRCLE; f.color = th_col(0x272D35); f.opa = 255;
    lv_area_t d = { (int32_t)(cx - R), (int32_t)(cy - R), (int32_t)(cx + R), (int32_t)(cy + R) };
    lv_draw_fill(layer, &f, &d);
    double ci = 2 * ph - 1;
    float sx = waxing ? 1 : -1;
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = th_col(0xE9EEF5); l.width = 2; l.opa = 255;
    int n = (int)(R * 2);
    for (int k = 0; k <= n; k++) {
        float v = -1 + 2.0f * k / n, w = sqrtf(fmaxf(0, 1 - v * v));
        float u0 = (float)(-ci * w), u1 = w;
        if (u1 - u0 < 0.02f) continue;
        l.p1.x = cx + R * u0 * sx; l.p1.y = cy + R * v;
        l.p2.x = cx + R * u1 * sx; l.p2.y = cy + R * v;
        lv_draw_line(layer, &l);
    }
}

static void tonightRow(lv_obj_t *list, const ObjRef &r, const char *title, const char *sub, uint32_t col)
{
    if (g_nRowRefs >= 40) return;
    g_rowRefs[g_nRowRefs] = r;
    rowButton(list, title, sub, col, findRowCb, &g_rowRefs[g_nRowRefs]);
    g_nRowRefs++;
}

static void objSub(const ObjRef &r, char *sub, size_t n, const char *prefix)
{
    double az, alt;
    enuToAzAlt(skyObjEnu(r), az, alt);
    char t[8];
    if (alt > 0) {
        fmtJd(t, sizeof t, skySetTime(r));
        snprintf(sub, n, "%sUP %.0f\xC2\xB0 %s  \xC2\xB7  SETS %s", prefix, alt, compassPoint(az), t);
    } else {
        double rt = skyRiseTime(r);
        fmtJd(t, sizeof t, rt);
        if (rt > 0) snprintf(sub, n, "%sRISES %s", prefix, t);
        else snprintf(sub, n, "%sBELOW HORIZON", prefix);
    }
}

/* Tonight rows are computed on the background worker (rise/set searches are heavy double
 * maths), then the page is built from the finished data on the UI thread. */
struct TRow { ObjRef ref; char title[40]; char sub[72]; uint32_t col; uint8_t section; };
struct TonightData { TRow rows[28]; int n; NightInfo night; double unix; };
static TonightData g_tdWork, g_tdShow;
static bool g_tdBusy = false, g_tdHave = false;

static void tdAdd(TonightData &d, uint8_t sec, const ObjRef &r, const char *title, const char *sub, uint32_t col)
{
    if (d.n >= 28) return;
    TRow &w = d.rows[d.n++];
    w.ref = r; w.section = sec; w.col = col;
    snprintf(w.title, sizeof w.title, "%s", title);
    snprintf(w.sub, sizeof w.sub, "%s", sub);
}

static void tonightWork(void *)
{
    TonightData &d = g_tdWork;
    char sub[72], t1[8];
    d.n = 0;
    for (int p = MERCURY; p <= NEPTUNE; p++) {
        ObjRef r;
        r.kind = OK_PLANET; r.idx = p;
        char pre[20];
        snprintf(pre, sizeof pre, "MAG %.1f  \xC2\xB7  ", skyBodyState((Body)p).mag);
        objSub(r, sub, sizeof sub, pre);
        tdAdd(d, 0, r, bodyName((Body)p), sub, skyBodyColor((Body)p));
    }
    if (satsCount() > 0) {
        SatPass ps;
        if (satPassesSafe(0, skyJd(), 10, &ps, 1, nullptr, nullptr, 0)) {
            fmtJd(t1, 8, ps.jdStart);
            snprintf(sub, sizeof sub, "%s  MAX %.0f\xC2\xB0  %s", t1, ps.maxEl, ps.visible ? "VISIBLE" : "NOT VISIBLE");
        } else snprintf(sub, sizeof sub, "NO PASS IN 3 DAYS");
        ObjRef r;
        r.kind = OK_SAT; r.idx = 0;
        tdAdd(d, 1, r, satName(0), sub, C_SAT);
    }
    int shown = 0;
    for (uint16_t i = 0; i < skyStarCount() && shown < 8; i++) {
        if (skyStarMag(i) > 2.0f) break;
        const float *p = skyStarEnu(i);
        if (p[2] < 0.12f || !skyStarName(i)[0]) continue;
        ObjRef r;
        r.kind = OK_STAR; r.idx = i;
        ObjInfo o;
        skyObjInfo(r, o);
        snprintf(sub, sizeof sub, "MAG %.1f  \xC2\xB7  %s  \xC2\xB7  ALT %.0f\xC2\xB0 %s", o.mag,
                 CAT_CONS[CAT_STARS[i].con < CAT_NCONS ? CAT_STARS[i].con : 0].name, o.alt, compassPoint(o.az));
        tdAdd(d, 2, r, o.name, sub, skyStarColor(i));
        shown++;
    }
    shown = 0;
    for (uint8_t i = 0; i < CAT_NDSO && shown < 6; i++) {
        if (!CAT_DSO[i].name[0] || CAT_DSO[i].mag100 > 700) continue;
        Vec3 v = skyDsoEnu(i);
        if (v.z < 0.2) continue;
        ObjRef r;
        r.kind = OK_DSO; r.idx = i;
        ObjInfo o;
        skyObjInfo(r, o);
        snprintf(sub, sizeof sub, "%s  \xC2\xB7  ALT %.0f\xC2\xB0", CAT_DSO[i].type, o.alt);
        tdAdd(d, 3, r, o.name, sub, C_DSO);
        shown++;
    }
}

static void buildTonightContent();
static void tonightDone(void *)
{
    g_tdShow = g_tdWork;
    g_tdHave = true;
    g_tdBusy = false;
    if (g_tile == TILE_TONIGHT || !g_tonightBuilt) buildTonightContent();
}

static void requestTonight(bool force)
{
    if (g_tdBusy) return;
    if (!force && g_tdHave && devUnixTime() - g_tdShow.unix < 60) return;
    g_tdWork.night = skyNight();
    g_tdWork.unix = devUnixTime();
    g_tdBusy = bgSubmit(tonightWork, tonightDone, nullptr);
}

static void buildTonightContent()
{
    if (!g_tonightList) return;
    lv_obj_clean(g_tonightList);
    g_nRowRefs = 0;
    lv_obj_t *list = g_tonightList;
    if (!g_tdHave) {
        mkLabel(list, "Working out tonight's sky...", &f_ui20, C_TEXT_DIM);
        return;
    }
    const TonightData &d = g_tdShow;
    char b[64], t1[8], t2[8], t3[8], t4[8];
    fmtDate(b, sizeof b, d.unix);
    lv_obj_t *dl = mkLabel(list, b, &f_mono16, C_TEXT_DIM);
    lv_obj_set_style_text_letter_space(dl, 2, 0);

    const NightInfo &n = d.night;
    /* sun card */
    lv_obj_t *card = lv_obj_create(list);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, th_col(C_CARD), 0);
    lv_obj_set_style_bg_opa(card, 255, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(card, 6, 0);
    fmtJd(t1, 8, n.sunset); fmtJd(t2, 8, n.duskAstro); fmtJd(t3, 8, n.dawnAstro); fmtJd(t4, 8, n.sunrise);
    const char *lab[4] = { "SUNSET", "DARK", "DAWN", "SUNRISE" };
    const char *val[4] = { t1, t2, t3, t4 };
    uint32_t cols[4] = { C_ACCENT, C_HUD, C_HUD, C_ACCENT };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = lv_obj_create(card);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, LV_PCT(50), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        mkLabel(c, lab[i], &f_mono13, C_TEXT_DIM);
        mkLabel(c, val[i], &f_orb26, cols[i]);
    }

    /* moon card */
    lv_obj_t *mc = lv_obj_create(list);
    lv_obj_remove_style_all(mc);
    lv_obj_set_size(mc, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(mc, th_col(C_CARD), 0);
    lv_obj_set_style_bg_opa(mc, 255, 0);
    lv_obj_set_style_radius(mc, 18, 0);
    lv_obj_set_style_pad_all(mc, 12, 0);
    lv_obj_set_flex_flow(mc, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mc, 14, 0);
    lv_obj_add_flag(mc, LV_OBJ_FLAG_CLICKABLE);
    static ObjRef moonRef;
    moonRef.kind = OK_MOON; moonRef.idx = MOON;
    lv_obj_add_event_cb(mc, findRowCb, LV_EVENT_CLICKED, &moonRef);
    lv_obj_t *icon = lv_obj_create(mc);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, 64, 64);
    lv_obj_add_event_cb(icon, moonIconDraw, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_t *mt = lv_obj_create(mc);
    lv_obj_remove_style_all(mt);
    lv_obj_set_size(mt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mt, LV_FLEX_FLOW_COLUMN);
    mkLabel(mt, n.moonPhaseName ? n.moonPhaseName : "Moon", &f_ui24, C_TEXT);
    snprintf(b, sizeof b, "%.0f%% LIT  \xC2\xB7  AGE %.1f D", n.moonPhase * 100, n.moonAgeDays);
    mkLabel(mt, b, &f_mono13, C_TEXT_DIM);
    fmtJd(t1, 8, n.moonrise); fmtJd(t2, 8, n.moonset);
    snprintf(b, sizeof b, "RISE %s  \xC2\xB7  SET %s", t1, t2);
    mkLabel(mt, b, &f_mono13, C_HUD);

    static const char *secName[] = { "PLANETS", "SPACE STATION", "BRIGHT STARS UP NOW", "DEEP SKY" };
    int lastSec = -1;
    for (int i = 0; i < d.n; i++) {
        const TRow &r = d.rows[i];
        if (r.section != lastSec) { section(list, secName[r.section]); lastSec = r.section; }
        tonightRow(list, r.ref, r.title, r.sub, r.col);
    }
    lv_obj_t *pad = lv_obj_create(list);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, 10, 40);
    g_tonightBuiltMs = lv_tick_get();
    g_tonightBuilt = true;
}

static lv_obj_t *tileHeader(lv_obj_t *tile, const char *t)
{
    lv_obj_t *h = mkLabel(tile, t, &f_orb26, C_HUD);
    lv_obj_set_style_text_letter_space(h, 4, 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 22);
    return h;
}

static lv_obj_t *tileList(lv_obj_t *tile, int top)
{
    lv_obj_t *l = lv_obj_create(tile);
    lv_obj_remove_style_all(l);
    lv_obj_set_size(l, SCR_W - 56, SCR_H - top - 20);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, top);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l, 8, 0);
    lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    return l;
}

static void buildTonight(lv_obj_t *tile)
{
    tileHeader(tile, "TONIGHT");
    g_tonightList = tileList(tile, 66);
    g_tonightBuilt = false;            // filled on first visit: keeps boot fast on slow chips
    mkLabel(g_tonightList, "Working out tonight's sky...", &f_ui20, C_TEXT_DIM);
}

/* ------------------------------------------------------------------ ORBIT tile */
static void plotDraw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *o = (lv_obj_t *)lv_event_get_current_target(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    float cx = (a.x1 + a.x2) / 2.0f, cy = (a.y1 + a.y2) / 2.0f, R = (a.x2 - a.x1) / 2.0f - 18;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = (int32_t)cx; arc.center.y = (int32_t)cy; arc.start_angle = 0; arc.end_angle = 360;
    for (int k = 0; k < 3; k++) {
        arc.radius = (uint16_t)(R * (3 - k) / 3.0f);
        arc.color = th_col(k == 0 ? C_HUD : C_HUD_DIM);
        arc.width = 1; arc.opa = k == 0 ? 220 : 140;
        lv_draw_arc(layer, &arc);
    }
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = th_col(C_HUD_DIM); l.opa = 120; l.width = 1;
    l.p1.x = cx - R; l.p1.y = cy; l.p2.x = cx + R; l.p2.y = cy; lv_draw_line(layer, &l);
    l.p1.x = cx; l.p1.y = cy - R; l.p2.x = cx; l.p2.y = cy + R; lv_draw_line(layer, &l);
    static const char *card[] = { "N", "E", "S", "W" };
    lv_draw_label_dsc_t t;
    lv_draw_label_dsc_init(&t);
    t.font = &f_orb18; t.text_local = 1;
    for (int k = 0; k < 4; k++) {
        float ang = k * 90 * (float)D2R;
        float x = cx + sinf(ang) * (R + 11), y = cy - cosf(ang) * (R + 11);
        t.text = card[k];
        t.color = th_col(k == 0 ? C_ACCENT2 : C_ACCENT);
        lv_area_t ar = { (int32_t)x - 10, (int32_t)y - 11, (int32_t)x + 10, (int32_t)y + 11 };
        t.align = LV_TEXT_ALIGN_CENTER;
        lv_draw_label(layer, &t, &ar);
    }
    auto toXY = [&](float az, float el, float &x, float &y) {
        float r = R * (90 - fmaxf(0, el)) / 90.0f;
        x = cx + sinf(az * (float)D2R) * r; y = cy - cosf(az * (float)D2R) * r;
    };
    if (g_plotN > 1) {
        l.width = 3; l.round_start = l.round_end = 1;
        for (int k = 1; k < g_plotN; k++) {
            float x0, y0, x1, y1;
            toXY(g_plotAz[k - 1], g_plotEl[k - 1], x0, y0);
            toXY(g_plotAz[k], g_plotEl[k], x1, y1);
            l.p1.x = x0; l.p1.y = y0; l.p2.x = x1; l.p2.y = y1;
            l.color = th_col(C_SAT);
            l.opa = (lv_opa_t)(90 + 165 * k / g_plotN);
            lv_draw_line(layer, &l);
        }
        lv_draw_fill_dsc_t f;
        lv_draw_fill_dsc_init(&f);
        f.radius = LV_RADIUS_CIRCLE;
        float x, y;
        toXY(g_plotAz[0], g_plotEl[0], x, y);
        f.color = th_col(C_OK); f.opa = 255;
        lv_area_t d = { (int32_t)x - 5, (int32_t)y - 5, (int32_t)x + 5, (int32_t)y + 5 };
        lv_draw_fill(layer, &f, &d);
        toXY(g_plotAz[g_plotN - 1], g_plotEl[g_plotN - 1], x, y);
        f.color = th_col(C_ACCENT2);
        d = { (int32_t)x - 4, (int32_t)y - 4, (int32_t)x + 4, (int32_t)y + 4 };
        lv_draw_fill(layer, &f, &d);
    }
    /* live position */
    SatNow s;
    if (satCompute(g_satSel, skyJd(), s) && s.el > 0) {
        float x, y;
        toXY((float)s.az, (float)s.el, x, y);
        float p = 0.5f + 0.5f * sinf(lv_tick_get() * 0.008f);
        lv_draw_fill_dsc_t f;
        lv_draw_fill_dsc_init(&f);
        f.radius = LV_RADIUS_CIRCLE; f.color = th_col(C_SAT); f.opa = 60;
        int rr = (int)(9 + 5 * p);
        lv_area_t d = { (int32_t)x - rr, (int32_t)y - rr, (int32_t)x + rr, (int32_t)y + rr };
        lv_draw_fill(layer, &f, &d);
        f.opa = 255;
        d = { (int32_t)x - 4, (int32_t)y - 4, (int32_t)x + 4, (int32_t)y + 4 };
        lv_draw_fill(layer, &f, &d);
    }
}

/* Pass predictions run on the background worker with a private copy of the propagator. */
struct OrbitJob { int sat; double jd; SatPass pass[4]; int n; float az[64], el[64]; };
static OrbitJob g_oj;
static bool g_ojBusy = false, g_ojQueued = false;
static void refreshOrbit(bool rebuildList);

static void orbitWork(void *)
{
    g_oj.n = satPassesSafe(g_oj.sat, g_oj.jd, 10, g_oj.pass, 4, g_oj.az, g_oj.el, 64);
}

static void computePasses(bool force);
static void orbitDone(void *)
{
    g_npass = g_oj.n;
    memcpy(g_pass, g_oj.pass, sizeof(g_pass));
    memcpy(g_plotAz, g_oj.az, sizeof(g_plotAz));
    memcpy(g_plotEl, g_oj.el, sizeof(g_plotEl));
    g_plotN = g_npass ? 64 : 0;
    g_passSat = g_oj.sat;
    g_passComputedJd = g_oj.jd;
    g_ojBusy = false;
    if (g_ojQueued) { g_ojQueued = false; computePasses(true); }
    refreshOrbit(true);
}

static void computePasses(bool force)
{
    double jd = skyJd();
    bool fresh = g_passSat == g_satSel && g_npass > 0 && jd < g_pass[0].jdStop && jd - g_passComputedJd < 0.25;
    if (!force && fresh) return;
    if (g_ojBusy) { g_ojQueued = true; return; }
    g_oj.sat = g_satSel;
    g_oj.jd = jd;
    g_ojBusy = bgSubmit(orbitWork, orbitDone, nullptr);
}

static void refreshOrbit(bool rebuildList)
{
    if (!g_orbName) return;
    computePasses(false);                       // starts a background search if stale
    lv_label_set_text(g_orbName, satName(g_satSel));
    char b[96];
    snprintf(b, sizeof b, "NORAD %u  \xC2\xB7  TLE %.1f DAYS OLD", (unsigned)satNorad(g_satSel), satTleAgeDays(g_satSel, skyJd()));
    lv_label_set_text(g_orbAge, b);
    double jd = skyJd();
    if (g_passSat != g_satSel || (g_ojBusy && g_npass == 0)) {
        lv_label_set_text(g_orbCountLbl, "PREDICTING PASSES");
        lv_label_set_text(g_orbCount, "...");
        lv_label_set_text(g_orbDetail, "");
        lv_label_set_text(g_orbBadge, "");
        g_plotN = 0;
    } else if (g_npass == 0) {
        lv_label_set_text(g_orbCountLbl, "NO PASS FOUND");
        lv_label_set_text(g_orbCount, "--:--:--");
        lv_label_set_text(g_orbDetail, "");
        lv_label_set_text(g_orbBadge, "");
    } else {
        SatPass &p = g_pass[0];
        if (jd >= p.jdStart && jd <= p.jdStop) {
            SatNow s;
            satCompute(g_satSel, jd, s);
            lv_label_set_text(g_orbCountLbl, "OVERHEAD NOW");
            snprintf(b, sizeof b, "%.0f\xC2\xB0 %s", s.el, compassPoint(s.az));
            lv_label_set_text(g_orbCount, b);
            lv_obj_set_style_text_color(g_orbCount, th_col(C_OK), 0);
        } else {
            long sec = (long)((p.jdStart - jd) * 86400);
            if (sec < 0) sec = 0;
            snprintf(b, sizeof b, "%02ld:%02ld:%02ld", sec / 3600, sec / 60 % 60, sec % 60);
            lv_label_set_text(g_orbCountLbl, "NEXT PASS IN");
            lv_label_set_text(g_orbCount, b);
            lv_obj_set_style_text_color(g_orbCount, th_col(sec < 300 ? C_WARN : C_TEXT), 0);
        }
        char t1[8], t2[8], t3[8];
        fmtJd(t1, 8, p.jdStart); fmtJd(t2, 8, p.jdMax); fmtJd(t3, 8, p.jdStop);
        snprintf(b, sizeof b, "RISE %s %s  \xC2\xB7  MAX %.0f\xC2\xB0 %s  \xC2\xB7  SET %s %s", t1, compassPoint(p.azStart),
                 p.maxEl, t2, t3, compassPoint(p.azStop));
        lv_label_set_text(g_orbDetail, b);
        lv_label_set_text(g_orbBadge, p.visible ? "VISIBLE TO THE EYE" : "NOT VISIBLE  \xC2\xB7  DAYLIGHT OR SHADOW");
        lv_obj_set_style_text_color(g_orbBadge, th_col(p.visible ? C_OK : C_TEXT_DIM), 0);
    }
    if (rebuildList) {
        lv_obj_clean(g_orbList);
        for (int k = 1; k < g_npass; k++) {
            char t1[8];
            fmtJd(t1, 8, g_pass[k].jdStart);
            double lu = (g_pass[k].jdStart - 2440587.5) * 86400;
            char dd[24];
            fmtDate(dd, sizeof dd, lu);
            snprintf(b, sizeof b, "%s  %s   MAX %.0f\xC2\xB0  %s", dd, t1, g_pass[k].maxEl, g_pass[k].visible ? "VIS" : "");
            lv_obj_t *l = mkLabel(g_orbList, b, &f_mono13, g_pass[k].visible ? C_OK : C_TEXT_DIM);
            (void)l;
        }
    }
    const char *tm = devTleMsg();
    if (g_orbTleBtnLbl) lv_label_set_text(g_orbTleBtnLbl, devTleState() == TLE_RUNNING ? "UPDATING..." : "UPDATE");
    (void)tm;
}

static void orbitSatCycle(lv_event_t *e)
{
    if (satsCount() < 2) return;
    g_satSel = (g_satSel + 1) % satsCount();
    g_npass = 0;
    computePasses(true);
    refreshOrbit(true);
    devHaptic(H_TICK);
}

static lv_obj_t *pillButton(lv_obj_t *p, const char *t, uint32_t col, lv_event_cb_t cb, lv_obj_t **lbl = nullptr)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 148, 46);
    lv_obj_set_style_radius(b, 23, 0);
    lv_obj_set_style_bg_color(b, th_col(col), 0);
    lv_obj_set_style_bg_opa(b, 40, 0);
    lv_obj_set_style_bg_opa(b, 110, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, th_col(col), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_t *l = mkLabel(b, t, &f_ui20, col);
    lv_obj_center(l);
    if (lbl) *lbl = l;
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    tapSound(b);
    return b;
}

static void buildOrbit(lv_obj_t *tile)
{
    tileHeader(tile, "ORBIT");
    lv_obj_t *l = tileList(tile, 64);
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_orbName = mkLabel(l, "ISS", &f_ui30, C_SAT);
    lv_obj_add_flag(g_orbName, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_orbName, orbitSatCycle, LV_EVENT_CLICKED, nullptr);
    g_orbAge = mkLabel(l, "", &f_mono13, C_TEXT_DIM);
    g_orbCountLbl = mkLabel(l, "NEXT PASS IN", &f_mono13, C_HUD_DIM);
    lv_obj_set_style_text_letter_space(g_orbCountLbl, 3, 0);
    lv_obj_set_style_pad_top(g_orbCountLbl, 6, 0);
    g_orbCount = mkLabel(l, "--:--:--", &f_orb40, C_TEXT);
    g_orbPlot = lv_obj_create(l);
    lv_obj_remove_style_all(g_orbPlot);
    lv_obj_set_size(g_orbPlot, 250, 250);
    lv_obj_add_event_cb(g_orbPlot, plotDraw, LV_EVENT_DRAW_MAIN, nullptr);
    g_orbBadge = mkLabel(l, "", &f_mono13, C_OK);
    lv_obj_set_style_text_letter_space(g_orbBadge, 1, 0);
    g_orbDetail = mkLabel(l, "", &f_ui16, C_TEXT);
    lv_obj_set_width(g_orbDetail, LV_PCT(100));
    lv_label_set_long_mode(g_orbDetail, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(g_orbDetail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *row = lv_obj_create(l);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(row, 8, 0);
    pillButton(row, "TRACK", C_SAT, [](lv_event_t *) {
        ObjRef r; r.kind = OK_SAT; r.idx = (uint16_t)g_satSel; startFind(r);
    });
    pillButton(row, "UPDATE", C_HUD, [](lv_event_t *) { syncOrSetup(); }, &g_orbTleBtnLbl);
    section(l, "LATER PASSES");
    g_orbList = lv_obj_create(l);
    lv_obj_remove_style_all(g_orbList);
    lv_obj_set_size(g_orbList, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_orbList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_orbList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_orbList, 4, 0);
    lv_obj_t *pad = lv_obj_create(l);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, 10, 40);
    g_passSat = -1;                    // passes are computed on first visit
}

/* ------------------------------------------------------------------ Wi-Fi + location modals */
static char g_scan[12][33];
static int g_nscan = 0;
static char g_pickedSsid[33];


/* Big-key password keyboard (after the Wardriver's resistive-friendly design):
 * 15 large keys per page - letters a-o / p-z, numbers, two symbol pages - plus a mode row. */
static char g_pw[65];
static int g_pwLen = 0;
static uint8_t g_kbMode = 0, g_kbPage = 0;   // mode: 0 letters, 1 numbers, 2 symbols
static bool g_kbUpper = false;
static lv_obj_t *g_kbKeys, *g_kbAct, *g_pwLbl;
static char g_keyText[15][3];
static const char *g_keyMap[19];
static const char *const kKbPages[3][2] = {
    { "abcdefghijklmno", "pqrstuvwxyz.-_@" },
    { "1234567890.-_@#", "1234567890.-_@#" },
    { "!$%&*()=+?/:;'\"", ",<>[]{}\\|^~`#@." },
};
static const char *g_actMap[] = { "SPACE", "Aa", "a-o", "123", "#+=", "DEL", "" };

static void kbRefresh()
{
    const char *keys = kKbPages[g_kbMode][g_kbPage];
    int k = 0;
    for (int i = 0; i < 15; i++) {
        char c = keys[i];
        if (g_kbMode == 0 && g_kbUpper && c >= 'a' && c <= 'z') c -= 32;
        g_keyText[i][0] = c; g_keyText[i][1] = 0;
        g_keyMap[k++] = g_keyText[i];
        if (i % 5 == 4 && i < 14) g_keyMap[k++] = "\n";
    }
    g_keyMap[k] = "";
    lv_buttonmatrix_set_map(g_kbKeys, g_keyMap);
    static char pg[8];
    if (g_kbMode == 0) snprintf(pg, sizeof pg, "%s", g_kbPage ? "p-z" : "a-o");
    else if (g_kbMode == 2) snprintf(pg, sizeof pg, "%s", g_kbPage ? "SYM2" : "SYM1");
    else snprintf(pg, sizeof pg, "--");
    g_actMap[2] = pg;
    g_actMap[1] = g_kbUpper ? "ABC" : "abc";
    lv_buttonmatrix_set_map(g_kbAct, g_actMap);
    lv_buttonmatrix_clear_button_ctrl_all(g_kbAct, LV_BUTTONMATRIX_CTRL_CHECKED);
    if (g_kbMode == 0 && g_kbUpper) lv_buttonmatrix_set_button_ctrl(g_kbAct, 1, LV_BUTTONMATRIX_CTRL_CHECKED);
    if (g_kbMode == 1) lv_buttonmatrix_set_button_ctrl(g_kbAct, 3, LV_BUTTONMATRIX_CTRL_CHECKED);
    if (g_kbMode == 2) lv_buttonmatrix_set_button_ctrl(g_kbAct, 4, LV_BUTTONMATRIX_CTRL_CHECKED);
    if (g_kbMode == 1) lv_buttonmatrix_set_button_ctrl(g_kbAct, 2, LV_BUTTONMATRIX_CTRL_DISABLED);
    else lv_buttonmatrix_clear_button_ctrl(g_kbAct, 2, LV_BUTTONMATRIX_CTRL_DISABLED);
}

static void pwShow()
{
    char b[80];
    const char *t = g_pw;
    if (g_pwLen > 22) t = g_pw + g_pwLen - 22;          // keep the end (where you type) in view
    snprintf(b, sizeof b, "%s%s|", g_pwLen > 22 ? "\xC2\xB7\xC2\xB7" : "", t);
    lv_label_set_text(g_pwLbl, g_pwLen ? b : "|");
}

static void kbKeyCb(lv_event_t *e)
{
    lv_obj_t *m = (lv_obj_t *)lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(m);
    const char *t = lv_buttonmatrix_get_button_text(m, id);
    if (!t || !t[0] || g_pwLen >= 63) return;
    g_pw[g_pwLen++] = t[0];
    g_pw[g_pwLen] = 0;
    pwShow();
    sndPlay(SFX_KEY);
}

static void kbActCb(lv_event_t *e)
{
    lv_obj_t *m = (lv_obj_t *)lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(m);
    switch (id) {
    case 0: if (g_pwLen < 63) { g_pw[g_pwLen++] = ' '; g_pw[g_pwLen] = 0; } break;
    case 1: if (g_kbMode != 0) { g_kbMode = 0; g_kbPage = 0; } else g_kbUpper = !g_kbUpper; break;
    case 2: if (g_kbMode != 1) g_kbPage ^= 1; break;
    case 3: g_kbMode = g_kbMode == 1 ? 0 : 1; g_kbPage = 0; break;
    case 4: g_kbMode = g_kbMode == 2 ? 0 : 2; g_kbPage = 0; break;
    case 5: if (g_pwLen) g_pw[--g_pwLen] = 0; break;
    }
    kbRefresh();
    pwShow();
    sndPlay(SFX_KEY);
}

static void kbStyle(lv_obj_t *m, const lv_font_t *f, int gap)
{
    lv_obj_set_style_bg_opa(m, 0, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_pad_row(m, gap, 0);
    lv_obj_set_style_pad_column(m, gap, 0);
    lv_obj_set_style_bg_color(m, th_col(C_CARD), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(m, 255, LV_PART_ITEMS);
    lv_obj_set_style_radius(m, 14, LV_PART_ITEMS);
    lv_obj_set_style_border_width(m, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(m, th_col(C_HUD_DIM), LV_PART_ITEMS);
    lv_obj_set_style_text_font(m, f, LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, th_col(C_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, th_col(C_HUD), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(m, th_col(0x00141A), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(m, th_col(0x0E3A44), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(m, th_col(C_HUD), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(m, th_col(C_HUD), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_opa(m, 70, LV_PART_ITEMS | LV_STATE_DISABLED);
}

static void wifiSave(lv_event_t *)
{
    devWifiSetCreds(g_pickedSsid, g_pw);
    closeModal();
    devStartSync();
    sndPlay(SFX_OK);
    toast(devHasGps() ? "Saved. Updating orbits..." : "Saved. Syncing clock, place and orbits...", C_OK, 3000);
}

static void wifiPick(lv_event_t *e)
{
    const char *ssid = (const char *)lv_event_get_user_data(e);
    strlcpy(g_pickedSsid, ssid, sizeof g_pickedSsid);
    closeModal();
    g_pw[0] = 0; g_pwLen = 0; g_kbMode = 0; g_kbPage = 0; g_kbUpper = false;
    g_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_modal);
    lv_obj_set_size(g_modal, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(g_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_modal, 255, 0);

    lv_obj_t *t = mkLabel(g_modal, g_pickedSsid, &f_ui20, C_HUD);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_t *box = lv_obj_create(g_modal);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 330, 46);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_bg_color(box, th_col(C_CARD), 0);
    lv_obj_set_style_bg_opa(box, 255, 0);
    lv_obj_set_style_radius(box, 23, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, th_col(C_HUD), 0);
    g_pwLbl = mkLabel(box, "|", &f_ui24, C_TEXT);
    lv_obj_align(g_pwLbl, LV_ALIGN_LEFT_MID, 18, 0);

    g_kbKeys = lv_buttonmatrix_create(g_modal);
    lv_obj_set_size(g_kbKeys, 370, 222);
    lv_obj_align(g_kbKeys, LV_ALIGN_TOP_MID, 0, 98);
    kbStyle(g_kbKeys, &f_ui30, 7);
    lv_obj_add_event_cb(g_kbKeys, kbKeyCb, LV_EVENT_VALUE_CHANGED, nullptr);

    g_kbAct = lv_buttonmatrix_create(g_modal);
    lv_obj_set_size(g_kbAct, 370, 50);
    lv_obj_align(g_kbAct, LV_ALIGN_TOP_MID, 0, 328);
    kbStyle(g_kbAct, &f_ui16, 5);
    lv_obj_add_event_cb(g_kbAct, kbActCb, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *row = lv_obj_create(g_modal);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 290, 50);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 388);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *cb = lv_button_create(row);
    lv_obj_remove_style_all(cb);
    lv_obj_set_size(cb, 136, 48);
    lv_obj_set_style_radius(cb, 24, 0);
    lv_obj_set_style_border_width(cb, 1, 0);
    lv_obj_set_style_border_color(cb, th_col(C_TEXT_DIM), 0);
    lv_obj_center(mkLabel(cb, "CANCEL", &f_ui20, C_TEXT_DIM));
    lv_obj_add_event_cb(cb, [](lv_event_t *) { closeModal(); }, LV_EVENT_CLICKED, nullptr);
    tapSound(cb);
    lv_obj_t *sb = lv_button_create(row);
    lv_obj_remove_style_all(sb);
    lv_obj_set_size(sb, 136, 48);
    lv_obj_set_style_radius(sb, 24, 0);
    lv_obj_set_style_bg_color(sb, th_col(C_OK), 0);
    lv_obj_set_style_bg_opa(sb, 50, 0);
    lv_obj_set_style_bg_opa(sb, 120, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(sb, 1, 0);
    lv_obj_set_style_border_color(sb, th_col(C_OK), 0);
    lv_obj_center(mkLabel(sb, "CONNECT", &f_ui20, C_OK));
    lv_obj_add_event_cb(sb, wifiSave, LV_EVENT_CLICKED, nullptr);
    kbRefresh();
    pwShow();
}

static void openWifiSetup()
{
    toast("Scanning for Wi-Fi...", C_HUD, 1500);
    lv_refr_now(NULL);
    g_nscan = devWifiScan(g_scan, 12);
    lv_obj_t *list = modalBase("WI-FI");
    if (!g_nscan) mkLabel(list, "No networks found", &f_ui20, C_TEXT_DIM);
    for (int i = 0; i < g_nscan; i++)
        rowButton(list, g_scan[i], !strcmp(g_scan[i], devWifiSsid()) ? "SAVED" : "", C_HUD, wifiPick, g_scan[i]);
}

struct City { const char *name; float lat, lon; };
static const City kCities[] = {
    { "Chicago", 41.878f, -87.630f },   { "New York", 40.713f, -74.006f },   { "Los Angeles", 34.052f, -118.244f },
    { "Denver", 39.739f, -104.990f },   { "Houston", 29.760f, -95.370f },    { "Dallas", 32.777f, -96.797f },
    { "Atlanta", 33.749f, -84.388f },   { "Miami", 25.762f, -80.192f },      { "Seattle", 47.606f, -122.332f },
    { "Phoenix", 33.448f, -112.074f },  { "Minneapolis", 44.978f, -93.265f }, { "Toronto", 43.653f, -79.383f },
    { "Mexico City", 19.433f, -99.133f }, { "London", 51.507f, -0.128f },   { "Paris", 48.857f, 2.352f },
    { "Berlin", 52.520f, 13.405f },     { "Madrid", 40.417f, -3.704f },      { "Rome", 41.903f, 12.496f },
    { "Cairo", 30.044f, 31.236f },      { "Dubai", 25.205f, 55.271f },       { "Mumbai", 19.076f, 72.878f },
    { "Singapore", 1.352f, 103.820f },  { "Tokyo", 35.676f, 139.650f },      { "Sydney", -33.869f, 151.209f },
    { "Auckland", -36.848f, 174.763f }, { "Sao Paulo", -23.551f, -46.633f }, { "Buenos Aires", -34.604f, -58.382f },
    { "Johannesburg", -26.204f, 28.047f },
};

static void cityPick(lv_event_t *e)
{
    const City *c = (const City *)lv_event_get_user_data(e);
    devSetManualLocation(c->lat, c->lon, c->name);
    closeModal();
    skyUpdate(devUnixTime(), devObserver());
    char b[48];
    snprintf(b, sizeof b, "Location: %s", c->name);
    toast(b, C_OK);
}

static void openLocation()
{
    lv_obj_t *list = modalBase("LOCATION");
    if (!devHasGps())
        rowButton(list, "Use Wi-Fi location", devWifiHasCreds() ? "APPROXIMATE, FROM YOUR INTERNET ADDRESS" : "SET UP WI-FI FIRST",
                  C_HUD, [](lv_event_t *) {
                      closeModal();
                      if (!devWifiHasCreds()) { openWifiSetup(); return; }
                      devSetManualLocation(devObserver().lat, devObserver().lon, nullptr);
                      devStartSync();
                      toast("Syncing over Wi-Fi...", C_HUD);
                  }, nullptr);
    char sub[40];
    for (const City &c : kCities) {
        snprintf(sub, sizeof sub, "%.2f\xC2\xB0, %.2f\xC2\xB0", c.lat, c.lon);
        rowButton(list, c.name, sub, C_ACCENT, cityPick, (void *)&c);
    }
}

static void syncOrSetup()
{
    if (!devWifiHasCreds()) { openWifiSetup(); return; }
    devStartSync();
    toast(devTleMsg(), C_HUD);
}

/* ------------------------------------------------------------------ SETUP tile */
static lv_obj_t *setRow(lv_obj_t *list, const char *title, lv_obj_t **valLbl, const char *val)
{
    lv_obj_t *r = lv_obj_create(list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), 58);
    lv_obj_set_style_bg_color(r, th_col(C_CARD), 0);
    lv_obj_set_style_bg_opa(r, 255, 0);
    lv_obj_set_style_radius(r, 16, 0);
    lv_obj_set_style_pad_hor(r, 16, 0);
    lv_obj_t *t = mkLabel(r, title, &f_ui20, C_TEXT);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
    tapSound(r);
    if (valLbl) {
        *valLbl = mkLabel(r, val, &f_mono16, C_HUD);
        lv_obj_align(*valLbl, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    return r;
}

static void addSwitch(lv_obj_t *row, bool on, lv_event_cb_t cb)
{
    lv_obj_t *s = lv_switch_create(row);
    lv_obj_set_size(s, 56, 30);
    lv_obj_align(s, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(s, th_col(0x1A2A33), 0);
    lv_obj_set_style_bg_color(s, th_col(C_HUD), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s, th_col(0xEAF6FF), LV_PART_KNOB);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    tapSound(s);
}

static bool swOn(lv_event_t *e) { return lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED); }

static void updateSetupLabels()
{
    Settings &s = devSettings();
    char b[32];
    int off = s.utcOffsetMin;
    snprintf(b, sizeof b, "UTC%c%d:%02d", off < 0 ? '-' : '+', abs(off) / 60, abs(off) % 60);
    if (g_utcLbl) lv_label_set_text(g_utcLbl, b);
    snprintf(b, sizeof b, "%d\xC2\xB0", s.axisRot * 90);
    if (g_axisLbl) lv_label_set_text(g_axisLbl, b);
    if (g_modeLbl) lv_label_set_text(g_modeLbl, s.pointMode ? "POINTER" : "VIEWFINDER");
    static const char *ld[] = { "FEW", "NORMAL", "LOTS" };
    if (g_labelsLbl) lv_label_set_text(g_labelsLbl, ld[s.labels % 3]);
}

static void refreshGpsLabel()
{
    if (g_alignRow) {
        lv_obj_t *sub = lv_obj_get_child(g_alignRow, 1);
        if (sub) {
            char b[48];
            if (orientCalibrated()) {
                uint32_t age = orientAlignAgeSec((uint32_t)devUnixTime());
                snprintf(b, sizeof b, "ALIGNED %u MIN AGO  \xC2\xB7  TAP TO REDO", (unsigned)(age / 60));
            } else snprintf(b, sizeof b, "NOT ALIGNED YET  \xC2\xB7  TAP TO START");
            lv_label_set_text(sub, b);
            lv_obj_set_style_text_color(sub, th_col(orientCalibrated() ? C_OK : C_WARN), 0);
        }
    }
    if (!g_gpsLbl) return;
    char b[200];
    devStatusDetail(b, sizeof b);
    lv_label_set_text(g_gpsLbl, b);
}

static void buildSetup(lv_obj_t *tile)
{
    tileHeader(tile, "SETUP");
    lv_obj_t *l = tileList(tile, 66);
    Settings &s = devSettings();
    lv_obj_t *r;

    g_alignRow = rowButton(l, "Align heading", "NOT ALIGNED YET", C_WARN, [](lv_event_t *) { openAlignChooser(); }, nullptr);
    rowButton(l, "Location", devHasGps() ? "GPS, OR PICK A CITY WHEN INDOORS" : "PICK A CITY OR USE WI-FI", C_ACCENT,
              [](lv_event_t *) { openLocation(); }, nullptr);
    rowButton(l, "Wi-Fi", devWifiHasCreds() ? devWifiSsid() : "NOT SET UP", C_HUD, [](lv_event_t *) { openWifiSetup(); }, nullptr);
    rowButton(l, devHasGps() ? "Update orbits" : "Sync over Wi-Fi",
              devHasGps() ? "ISS, TIANGONG, HUBBLE FROM CELESTRAK" : "CLOCK, LOCATION AND SATELLITE ORBITS", C_SAT,
              [](lv_event_t *) { syncOrSetup(); }, nullptr);

    r = setRow(l, "Pointing", &g_modeLbl, "");
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, [](lv_event_t *) {
        Settings &s = devSettings(); s.pointMode ^= 1; devSaveSettings(); updateSetupLabels();
        toast(s.pointMode ? "Point the 12 o'clock edge" : "Look through the watch", C_HUD);
    }, LV_EVENT_CLICKED, nullptr);

    r = setRow(l, "Night vision", nullptr, nullptr);
    addSwitch(r, s.night, [](lv_event_t *e) {
        devSettings().night = swOn(e); devSaveSettings(); g_nightMode = devSettings().night; uiRebuild();
    });
    r = setRow(l, "Constellations", nullptr, nullptr);
    addSwitch(r, s.constellations, [](lv_event_t *e) { devSettings().constellations = g_showConst = swOn(e); devSaveSettings(); });
    r = setRow(l, "Alt/az grid", nullptr, nullptr);
    addSwitch(r, s.grid, [](lv_event_t *e) { devSettings().grid = g_showGrid = swOn(e); devSaveSettings(); });
    r = setRow(l, "See through Earth", nullptr, nullptr);
    addSwitch(r, s.belowHorizon, [](lv_event_t *e) { devSettings().belowHorizon = g_showBelow = swOn(e); devSaveSettings(); });
    r = setRow(l, "Labels", &g_labelsLbl, "");
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, [](lv_event_t *) {
        Settings &s = devSettings(); s.labels = (s.labels + 1) % 3; g_labelDensity = s.labels; devSaveSettings(); updateSetupLabels();
    }, LV_EVENT_CLICKED, nullptr);
    r = setRow(l, "Pass alerts", nullptr, nullptr);
    addSwitch(r, s.passAlerts, [](lv_event_t *e) { devSettings().passAlerts = swOn(e); devSaveSettings(); });

    /* time zone with -/+ */
    r = setRow(l, "Time zone", &g_utcLbl, "");
    lv_obj_align(g_utcLbl, LV_ALIGN_RIGHT_MID, -92, 0);
    for (int k = 0; k < 2; k++) {
        lv_obj_t *bt = lv_button_create(r);
        lv_obj_remove_style_all(bt);
        lv_obj_set_size(bt, 40, 40);
        lv_obj_align(bt, LV_ALIGN_RIGHT_MID, k ? 0 : -46, 0);
        lv_obj_set_style_radius(bt, 20, 0);
        lv_obj_set_style_border_color(bt, th_col(C_HUD), 0);
        lv_obj_set_style_border_width(bt, 1, 0);
        lv_obj_set_style_bg_color(bt, th_col(C_HUD), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(bt, 80, LV_STATE_PRESSED);
        lv_obj_t *bl = mkLabel(bt, k ? "+" : "-", &f_ui24, C_HUD);
        lv_obj_center(bl);
        lv_obj_add_event_cb(bt, [](lv_event_t *e) {
            int dir = (int)(intptr_t)lv_event_get_user_data(e);
            Settings &s = devSettings();
            s.utcOffsetMin += dir * 60;
            if (s.utcOffsetMin > 14 * 60) s.utcOffsetMin = -12 * 60;
            if (s.utcOffsetMin < -12 * 60) s.utcOffsetMin = 14 * 60;
            devSaveSettings(); updateSetupLabels();
        }, LV_EVENT_CLICKED, (void *)(intptr_t)(k ? 1 : -1));
    }
    r = setRow(l, "US daylight saving", nullptr, nullptr);
    addSwitch(r, s.autoDst, [](lv_event_t *e) { devSettings().autoDst = swOn(e); devSaveSettings(); });

    r = setRow(l, "Sensor axis", &g_axisLbl, "");
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, [](lv_event_t *) {
        Settings &s = devSettings(); s.axisRot = (s.axisRot + 1) & 3; devSaveSettings(); updateSetupLabels();
    }, LV_EVENT_CLICKED, nullptr);

    r = setRow(l, "Volume", nullptr, nullptr);
    lv_obj_t *vs = lv_slider_create(r);
    lv_obj_set_size(vs, 180, 10);
    lv_obj_align(vs, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_slider_set_range(vs, 0, 100);
    lv_slider_set_value(vs, s.volume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(vs, th_col(0x1A2A33), 0);
    lv_obj_set_style_bg_color(vs, th_col(C_SAT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vs, th_col(0xEAF6FF), LV_PART_KNOB);
    lv_obj_add_event_cb(vs, [](lv_event_t *e) {
        devSettings().volume = (uint8_t)lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
        sndSetVolume(devSettings().volume);
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(vs, [](lv_event_t *) { devSaveSettings(); sndPlay(SFX_OK); }, LV_EVENT_RELEASED, nullptr);
    if (devHasHaptics()) {
        r = setRow(l, "Vibration", nullptr, nullptr);
        addSwitch(r, s.haptics, [](lv_event_t *e) { devSettings().haptics = swOn(e); devSaveSettings(); if (devSettings().haptics) devHaptic(H_CLICK); });
    }

    r = setRow(l, "Bright", nullptr, nullptr);
    lv_obj_t *sl = lv_slider_create(r);
    lv_obj_set_size(sl, 180, 10);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_slider_set_range(sl, 20, 255);
    lv_slider_set_value(sl, s.brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, th_col(0x1A2A33), 0);
    lv_obj_set_style_bg_color(sl, th_col(C_HUD), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, th_col(0xEAF6FF), LV_PART_KNOB);
    lv_obj_add_event_cb(sl, [](lv_event_t *e) {
        devSettings().brightness = (uint8_t)lv_slider_get_value((lv_obj_t *)lv_event_get_target(e)); devApplyBrightness();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(sl, [](lv_event_t *) { devSaveSettings(); }, LV_EVENT_RELEASED, nullptr);

    section(l, "STATUS");
    g_gpsLbl = mkLabel(l, "", &f_mono13, C_TEXT_DIM);
    lv_obj_set_style_text_line_space(g_gpsLbl, 4, 0);
    refreshGpsLabel();

    if (devHasLauncher())
        rowButton(l, "Exit to Launcher", "REBOOTS INTO THE LAUNCHER MENU", C_ACCENT2, [](lv_event_t *) { devExitToLauncher(); }, nullptr);
    else
        rowButton(l, "Restart", "", C_ACCENT2, [](lv_event_t *) { devExitToLauncher(); }, nullptr);
    section(l, "ABOUT");
    lv_obj_t *ab = mkLabel(l, "SkyPoint 1.0\nStars: HYG v4.1 (CC BY-SA 4.0)\nFigures: Stellarium (GPL)\n"
                              "Orbits: SGP4, CelesTrak elements\nFonts: Orbitron, Rajdhani, Share Tech Mono (OFL)",
                           &f_mono13, C_TEXT_DIM);
    lv_obj_set_style_text_line_space(ab, 3, 0);
    lv_obj_t *pad = lv_obj_create(l);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, 10, 50);
    updateSetupLabels();
}

/* ------------------------------------------------------------------ GPS tile */
static lv_obj_t *g_gpsPill, *g_gpsPillLbl, *g_gpsSummary, *g_gpsPlot, *g_gpsStat[3], *g_gpsCoord;
static GpsSat g_gsat[40];
static int g_ngsat = 0;
static bool g_gpsFixPrev = false;
static uint32_t g_searchSince = 0, g_lockFlashMs = 0;

static uint32_t cn0Col(int cn0)
{
    if (cn0 >= 40) return C_OK;
    if (cn0 >= 30) return C_HUD;
    if (cn0 >= 20) return C_WARN;
    return C_TEXT_DIM;
}

static void gpsPlotDraw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *o = (lv_obj_t *)lv_event_get_current_target(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    float cx = (a.x1 + a.x2) / 2.0f, cy = (a.y1 + a.y2) / 2.0f, R = (a.y2 - a.y1) / 2.0f - 16;
    GpsDetail d;
    devGpsDetail(d);
    uint32_t now = lv_tick_get();

    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = (int32_t)cx; arc.center.y = (int32_t)cy; arc.start_angle = 0; arc.end_angle = 360; arc.width = 1;
    for (int k = 0; k < 3; k++) {
        arc.radius = (uint16_t)(R * (3 - k) / 3.0f);
        arc.color = th_col(k == 0 ? C_HUD : C_HUD_DIM);
        arc.opa = k == 0 ? 220 : 120;
        lv_draw_arc(layer, &arc);
    }
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = th_col(C_HUD_DIM); l.opa = 100; l.width = 1;
    l.p1.x = cx - R; l.p1.y = cy; l.p2.x = cx + R; l.p2.y = cy; lv_draw_line(layer, &l);
    l.p1.x = cx; l.p1.y = cy - R; l.p2.x = cx; l.p2.y = cy + R; lv_draw_line(layer, &l);

    /* radar sweep while searching; a single bright pulse ring right after locking */
    if (!d.fix && d.online) {
        float ang = fmodf(now * 0.12f, 360.0f);
        for (int k = 0; k < 14; k++) {
            float t = (ang - k * 2.5f) * (float)D2R;
            l.p1.x = cx; l.p1.y = cy;
            l.p2.x = cx + sinf(t) * R; l.p2.y = cy - cosf(t) * R;
            l.color = th_col(C_WARN); l.width = 2; l.opa = (lv_opa_t)(150 - k * 10);
            lv_draw_line(layer, &l);
        }
    }
    if (g_lockFlashMs && now - g_lockFlashMs < 900) {
        float p = (now - g_lockFlashMs) / 900.0f;
        arc.radius = (uint16_t)(R * p); arc.width = 3; arc.color = th_col(C_OK); arc.opa = (lv_opa_t)(255 * (1 - p));
        lv_draw_arc(layer, &arc);
    }

    lv_draw_label_dsc_t t;
    lv_draw_label_dsc_init(&t);
    t.font = &f_orb18; t.text_local = 1; t.align = LV_TEXT_ALIGN_CENTER;
    static const char *card[] = { "N", "E", "S", "W" };
    for (int k = 0; k < 4; k++) {
        float ang = k * 90 * (float)D2R;
        float x = cx + sinf(ang) * (R + 10), y = cy - cosf(ang) * (R + 10);
        t.text = card[k];
        t.color = th_col(k == 0 ? C_ACCENT2 : C_ACCENT);
        lv_area_t ar = { (int32_t)x - 10, (int32_t)y - 11, (int32_t)x + 10, (int32_t)y + 11 };
        lv_draw_label(layer, &t, &ar);
    }

    /* satellites: filled = used in the fix, hollow = in view; colour = signal strength */
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.radius = LV_RADIUS_CIRCLE;
    for (int i = 0; i < g_ngsat; i++) {
        const GpsSat &s = g_gsat[i];
        float r = R * (90 - s.el) / 90.0f;
        float x = cx + sinf(s.az * (float)D2R) * r, y = cy - cosf(s.az * (float)D2R) * r;
        uint32_t col = cn0Col(s.cn0);
        if (s.used) {
            f.color = th_col(col); f.opa = 50;
            lv_area_t g = { (int32_t)x - 11, (int32_t)y - 11, (int32_t)x + 11, (int32_t)y + 11 };
            lv_draw_fill(layer, &f, &g);
            f.opa = 255;
            lv_area_t c = { (int32_t)x - 6, (int32_t)y - 6, (int32_t)x + 6, (int32_t)y + 6 };
            lv_draw_fill(layer, &f, &c);
        } else {
            lv_draw_arc_dsc_t ring;
            lv_draw_arc_dsc_init(&ring);
            ring.center.x = (int32_t)x; ring.center.y = (int32_t)y; ring.radius = 5; ring.width = 2;
            ring.start_angle = 0; ring.end_angle = 360; ring.color = th_col(col); ring.opa = 200;
            lv_draw_arc(layer, &ring);
        }
    }
}

static lv_obj_t *statTile(lv_obj_t *row, const char *title)
{
    lv_obj_t *c = lv_obj_create(row);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 104, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, th_col(C_CARD), 0);
    lv_obj_set_style_bg_opa(c, 255, 0);
    lv_obj_set_style_radius(c, 14, 0);
    lv_obj_set_style_pad_ver(c, 8, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    mkLabel(c, title, &f_mono13, C_TEXT_DIM);
    return mkLabel(c, "--", &f_ui20, C_TEXT);
}

static void refreshGps()
{
    if (!g_gpsPill) return;
    GpsDetail d;
    bool ok = devGpsDetail(d);
    g_ngsat = devGpsSats(g_gsat, 40);
    uint32_t col;
    const char *state;
    char b[72];
    if (!ok) { state = "NO RECEIVER"; col = C_ACCENT2; }
    else if (d.fix) { state = "LOCKED"; col = C_OK; }
    else { state = "SEARCHING"; col = C_WARN; }
    lv_label_set_text(g_gpsPillLbl, state);
    lv_obj_set_style_text_color(g_gpsPillLbl, th_col(col), 0);
    lv_obj_set_style_border_color(g_gpsPill, th_col(col), 0);
    lv_obj_set_style_bg_color(g_gpsPill, th_col(col), 0);
    /* breathing pill while searching, steady when locked */
    float br = d.fix ? 1.0f : 0.5f + 0.5f * sinf(lv_tick_get() * 0.004f);
    lv_obj_set_style_bg_opa(g_gpsPill, (lv_opa_t)(d.fix ? 45 : 20 + 30 * br), 0);
    lv_obj_set_style_border_opa(g_gpsPill, (lv_opa_t)(120 + 135 * br), 0);

    if (!ok) snprintf(b, sizeof b, "receiver not detected");
    else if (d.fix) snprintf(b, sizeof b, "%d of %d satellites in use", d.used, d.inView);
    else {
        uint32_t sec = (lv_tick_get() - g_searchSince) / 1000;
        snprintf(b, sizeof b, "%d in view  \xC2\xB7  searching %u:%02u  \xC2\xB7  go outside", d.inView, (unsigned)(sec / 60),
                 (unsigned)(sec % 60));
    }
    lv_label_set_text(g_gpsSummary, b);

    /* accuracy (HDOP x ~2.5 m typical range error), altitude, time to first fix */
    if (d.fix && d.hdop > 0) {
        snprintf(b, sizeof b, "\xC2\xB1%.0f m", fmax(1.0, d.hdop * 2.5));
        lv_label_set_text(g_gpsStat[0], b);
        lv_obj_set_style_text_color(g_gpsStat[0], th_col(d.hdop < 1.5 ? C_OK : (d.hdop < 4 ? C_HUD : C_WARN)), 0);
        snprintf(b, sizeof b, "%.0f m", d.altM);
        lv_label_set_text(g_gpsStat[1], b);
    } else {
        lv_label_set_text(g_gpsStat[0], "--");
        lv_obj_set_style_text_color(g_gpsStat[0], th_col(C_TEXT_DIM), 0);
        lv_label_set_text(g_gpsStat[1], "--");
    }
    if (d.ttffMs) {
        snprintf(b, sizeof b, "%u:%02u", (unsigned)(d.ttffMs / 60000), (unsigned)(d.ttffMs / 1000 % 60));
        lv_label_set_text(g_gpsStat[2], b);
    } else lv_label_set_text(g_gpsStat[2], "--");

    Observer o = devObserver();
    if (d.fix || o.valid) {
        double la = d.fix ? d.lat : o.lat, lo = d.fix ? d.lon : o.lon;
        snprintf(b, sizeof b, "%.4f\xC2\xB0 %c   %.4f\xC2\xB0 %c%s", fabs(la), la >= 0 ? 'N' : 'S', fabs(lo), lo >= 0 ? 'E' : 'W',
                 d.fix ? "" : "  (LAST)");
    } else snprintf(b, sizeof b, "NO POSITION YET");
    lv_label_set_text(g_gpsCoord, b);
}

static void buildGps(lv_obj_t *tile)
{
    tileHeader(tile, "GPS");
    g_gpsPill = lv_obj_create(tile);
    lv_obj_remove_style_all(g_gpsPill);
    lv_obj_set_size(g_gpsPill, 220, 52);
    lv_obj_set_style_radius(g_gpsPill, 26, 0);
    lv_obj_set_style_border_width(g_gpsPill, 2, 0);
    lv_obj_align(g_gpsPill, LV_ALIGN_TOP_MID, 0, 66);
    g_gpsPillLbl = mkLabel(g_gpsPill, "SEARCHING", &f_orb26, C_WARN);
    lv_obj_set_style_text_letter_space(g_gpsPillLbl, 2, 0);
    lv_obj_center(g_gpsPillLbl);
    g_gpsSummary = mkLabel(tile, "", &f_ui16, C_TEXT_DIM);
    lv_obj_align(g_gpsSummary, LV_ALIGN_TOP_MID, 0, 124);
    g_gpsPlot = lv_obj_create(tile);
    lv_obj_remove_style_all(g_gpsPlot);
    lv_obj_set_size(g_gpsPlot, 260, 210);
    lv_obj_align(g_gpsPlot, LV_ALIGN_TOP_MID, 0, 152);
    lv_obj_add_event_cb(g_gpsPlot, gpsPlotDraw, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_t *row = lv_obj_create(tile);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, SCR_W - 60, LV_SIZE_CONTENT);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 366);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_gpsStat[0] = statTile(row, "ACCURACY");
    g_gpsStat[1] = statTile(row, "ALTITUDE");
    g_gpsStat[2] = statTile(row, "TIME TO FIX");
    g_gpsCoord = mkLabel(tile, "", &f_mono16, C_HUD);
    lv_obj_align(g_gpsCoord, LV_ALIGN_TOP_MID, 0, 434);
    refreshGps();
}

/* ------------------------------------------------------------------ page dots */
static void dotsDraw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.radius = LV_RADIUS_CIRCLE;
    for (int i = 0; i < g_nTiles; i++) {
        int x = SCR_W / 2 - (g_nTiles - 1) * 9 + i * 18, y = SCR_H - 14;
        f.color = th_col(i == g_tile ? C_HUD : C_HUD_DIM);
        f.opa = i == g_tile ? 255 : 160;
        int r = i == g_tile ? 4 : 3;
        lv_area_t a = { x - r, y - r, x + r, y + r };
        lv_draw_fill(layer, &f, &a);
    }
}

static void tileChanged(lv_event_t *e)
{
    lv_obj_t *t = lv_tileview_get_tile_active(g_tv);
    int idx = t == g_tileSky ? TILE_SKY : (t == g_tileTonight ? TILE_TONIGHT : (t == g_tileOrbit ? TILE_ORBIT : (t == g_tileGps ? TILE_GPS : TILE_SETUP)));
    if (idx != g_tile) { devHaptic(H_TICK); sndPlay(SFX_PAGE); }
    g_tile = idx;
    if (idx == TILE_GPS) refreshGps();
    if (idx == TILE_TONIGHT) {
        if (!g_tonightBuilt && g_tdHave) buildTonightContent();
        requestTonight(false);                  // refresh in the background if older than a minute
    }
    if (idx == TILE_ORBIT) refreshOrbit(true);  // shows cached passes now, re-predicts in background if stale
    if (idx == TILE_SETUP) refreshGpsLabel();
    if (g_dots) {
        if (idx == 0) lv_obj_add_flag(g_dots, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(g_dots, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(g_dots);
    }
}

/* ------------------------------------------------------------------ build */
static void buildAll()
{
    Settings &s = devSettings();
    g_nightMode = s.night;
    g_showConst = s.constellations;
    g_showGrid = s.grid;
    g_showBelow = s.belowHorizon;
    g_labelDensity = s.labels;
    g_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(g_scr);
    lv_obj_set_style_bg_color(g_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_scr, 255, 0);
    lv_obj_set_style_text_color(g_scr, th_col(C_TEXT), 0);
    g_tv = lv_tileview_create(g_scr);
    lv_obj_set_size(g_tv, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(g_tv, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_tv, 255, 0);
    lv_obj_set_scrollbar_mode(g_tv, LV_SCROLLBAR_MODE_OFF);
    g_tileSky = lv_tileview_add_tile(g_tv, 0, 0, LV_DIR_RIGHT);
    g_tileTonight = lv_tileview_add_tile(g_tv, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    g_tileOrbit = lv_tileview_add_tile(g_tv, 2, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    if (devHasGps()) { TILE_GPS = 3; TILE_SETUP = 4; g_nTiles = 5; }
    else { TILE_GPS = -1; TILE_SETUP = 3; g_nTiles = 4; }
    if (TILE_GPS > 0) g_tileGps = lv_tileview_add_tile(g_tv, TILE_GPS, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    g_tileSetup = lv_tileview_add_tile(g_tv, TILE_SETUP, 0, LV_DIR_LEFT);
    buildSky(g_tileSky);
    buildTonight(g_tileTonight);
    buildOrbit(g_tileOrbit);
    if (g_tileGps) buildGps(g_tileGps);
    buildSetup(g_tileSetup);
    lv_obj_add_event_cb(g_tv, tileChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    g_dots = lv_obj_create(g_scr);
    lv_obj_remove_style_all(g_dots);
    lv_obj_set_size(g_dots, SCR_W, 30);
    lv_obj_align(g_dots, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(g_dots, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_dots, dotsDraw, LV_EVENT_DRAW_MAIN, nullptr);
    if (g_tile == 0) lv_obj_add_flag(g_dots, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(g_scr);
    lv_tileview_set_tile_by_index(g_tv, g_tile, 0, LV_ANIM_OFF);
}


/* ------------------------------------------------------------------ intro: warp-speed splash */
static lv_obj_t *g_intro = nullptr;
static uint32_t g_introStart = 0;
static lv_timer_t *g_introTimer = nullptr;

static void introDraw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    float t = (lv_tick_get() - g_introStart) / 1000.0f;
    float cx = SCR_W / 2.0f, cy = SCR_H / 2.0f - 20;
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.round_start = l.round_end = 1;
    uint32_t seed = 12345;
    auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 8 & 0xFFFF) / 65535.0f; };
    float speed = t < 1.0f ? t * t : 1.0f + (t - 1.0f) * 0.3f;
    for (int i = 0; i < 90; i++) {
        float ang = rnd() * 6.2832f, d0 = rnd(), sp = 0.5f + rnd();
        float r = fmodf(d0 * 260 + speed * 420 * sp, 330) + 8;
        float len = 6 + speed * 46 * sp * (r / 330);
        float ca = cosf(ang), sa = sinf(ang);
        l.p1.x = cx + ca * r; l.p1.y = cy + sa * r;
        l.p2.x = cx + ca * (r + len); l.p2.y = cy + sa * (r + len);
        l.width = r > 160 ? 2 : 1;
        l.color = th_col(i % 7 == 0 ? C_ACCENT : (i % 3 ? C_HUD : 0xFFFFFF));
        l.opa = (lv_opa_t)fminf(255, r * 1.4f);
        lv_draw_line(layer, &l);
    }
    /* expanding scope ring */
    lv_draw_arc_dsc_t a;
    lv_draw_arc_dsc_init(&a);
    a.center.x = (int32_t)cx; a.center.y = (int32_t)cy; a.width = 2; a.color = th_col(C_HUD);
    float k = fminf(1, t / 0.9f);
    a.radius = (uint16_t)(30 + 60 * k);
    a.start_angle = 0 + t * 120; a.end_angle = a.start_angle + 300 * k;
    a.opa = 220;
    lv_draw_arc(layer, &a);
    /* title reveal */
    const char *title = "SKYPOINT";
    int n = (int)fminf(8, fmaxf(0, (t - 0.25f) * 14));
    char buf[10];
    memcpy(buf, title, n);
    buf[n] = 0;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = &f_orb40; d.color = th_col(C_TEXT); d.text = buf; d.text_local = 1; d.letter_space = 4;
    d.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t ar = { 0, (int32_t)cy + 110, SCR_W - 1, (int32_t)cy + 160 };
    lv_draw_label(layer, &d, &ar);
    if (t > 0.9f) {
        d.font = &f_mono13; d.color = th_col(C_HUD); d.text = "POINT  \xC2\xB7  LOCK  \xC2\xB7  DISCOVER"; d.letter_space = 3;
        d.opa = (lv_opa_t)fminf(255, (t - 0.9f) * 600);
        lv_area_t a2 = { 0, (int32_t)cy + 162, SCR_W - 1, (int32_t)cy + 182 };
        lv_draw_label(layer, &d, &a2);
    }
}

static void introTick(lv_timer_t *)
{
    if (!g_intro) return;
    uint32_t el = lv_tick_get() - g_introStart;
    if (el > 2000) {
        lv_timer_delete(g_introTimer);
        g_introTimer = nullptr;
        lv_obj_fade_out(g_intro, 350, 0);
        lv_obj_delete_delayed(g_intro, 380);
        g_intro = nullptr;
        return;
    }
    lv_obj_invalidate(g_intro);
}

static void showIntro()
{
    g_intro = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_intro);
    lv_obj_set_size(g_intro, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(g_intro, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_intro, 255, 0);
    lv_obj_add_flag(g_intro, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_intro, introDraw, LV_EVENT_DRAW_MAIN, nullptr);
    g_introStart = lv_tick_get();
    g_introTimer = lv_timer_create(introTick, 30, nullptr);
    sndPlay(SFX_BOOT);
}

void uiRebuild()
{
    lv_obj_t *old = g_scr;
    closeModal();
    g_orbName = nullptr; g_gpsPill = nullptr; g_tileGps = nullptr; g_gpsLbl = g_utcLbl = g_axisLbl = g_modeLbl = g_labelsLbl = g_alignRow = nullptr; g_orbTleBtnLbl = nullptr;
    buildAll();
    if (old) lv_obj_delete(old);
}

void uiBegin()
{
    skyBegin();
    skyUpdate(devUnixTime(), devObserver());
    buildAll();
    showIntro();
}

void uiButton(int kind)
{
    if (kind == 1) {           // click: back to the sky / next page
        closeModal();
        gotoTile(g_tile == 0 ? 1 : 0);
    } else if (kind == 2) {    // double click: night vision
        devSettings().night = !devSettings().night;
        devSaveSettings();
        g_nightMode = devSettings().night;
        uiRebuild();
    }
}

/* ------------------------------------------------------------------ per-frame */
static void updateHud()
{
    static char banner[64], banner2[64];
    SkyHud h;
    memset(&h, 0, sizeof(h));
    double now = devUnixTime();
    fmtClock(h.clock, sizeof h.clock, now);
    GpsStatus g = devGps();
    devStatusLine(h.status, sizeof h.status);
    h.gpsFix = devHasGps() ? g.fix : devObserver().valid;
    h.calibrated = orientCalibrated();
    h.timeValid = devTimeValid();
    h.locationValid = devObserver().valid;
    if (g_aligning) {
        ObjInfo o;
        if (g_alignNorth) snprintf(banner, sizeof banner, "AIM AT NORTH HORIZON");
        else { skyObjInfo(g_alignTarget, o); snprintf(banner, sizeof banner, "AIM AT %s", o.name); }
        snprintf(banner2, sizeof banner2, "LONG-PRESS WHEN CENTRED  \xC2\xB7  TAP HERE TO CANCEL");
        h.banner = banner; h.banner2 = banner2; h.bannerColor = C_WARN;
    } else if (skyviewTarget().valid()) {
        const ObjInfo &o = skyviewTargetInfo();
        if (g_findFound) snprintf(banner, sizeof banner, "FOUND %s", o.name);
        else snprintf(banner, sizeof banner, "FIND %s  %.0f\xC2\xB0", o.name, skyviewTargetDistanceDeg());
        snprintf(banner2, sizeof banner2, o.alt < 0 ? "BELOW THE HORIZON  \xC2\xB7  TAP TO STOP" : "TAP HERE TO STOP");
        h.banner = banner; h.banner2 = banner2; h.bannerColor = g_findFound ? C_OK : (o.color ? o.color : C_HUD);
    } else if (!devObserver().valid) {
        h.banner = devHasGps() ? "WAITING FOR GPS" : "LOCATION NOT SET"; h.banner2 = devNoLocationHint();
        h.bannerColor = C_WARN;
    } else if (!orientCalibrated()) {
        h.banner = "HEADING NOT ALIGNED"; h.banner2 = "LONG-PRESS TO ALIGN ON THE MOON, A PLANET OR A STAR";
        h.bannerColor = C_WARN;
    }
    skyviewSetHud(h);
}

static void findLogic(uint32_t ms)
{
    ObjRef t = skyviewTarget();
    if (!t.valid()) return;
    double d = skyviewTargetDistanceDeg();
    double bucket = floor(d / 10);
    if (d < 2.5) {
        if (!g_findFound) { g_findFound = true; g_findFoundMs = ms; devHaptic(H_LOCK); sndPlay(SFX_FOUND); }
        else if (ms - g_findFoundMs > 5000) { skyviewSetTarget(ObjRef()); g_findFound = false; }
    } else {
        if (g_findFound && d > 6) g_findFound = false;
        if (bucket < g_lastFindBucket && d < 40) { devHaptic(H_TICK); sndPlay(SFX_TICK); }   // tick each 10 deg closer
    }
    g_lastFindBucket = bucket;
}

/* Visible-pass alerts: the next ISS pass is looked up in the background. */
static SatPass g_alertPass;
static double g_alertAt = 0;
static bool g_alertBusy = false;
static SatPass g_alertWork;
static double g_alertWorkJd = 0;

static void alertWork(void *) { if (!satPassesSafe(0, g_alertWorkJd, 10, &g_alertWork, 1, nullptr, nullptr, 0)) g_alertWork.valid = false; }
static void alertDone(void *) { g_alertPass = g_alertWork; g_alertAt = g_alertWorkJd; g_alertBusy = false; }

static void passAlerts()
{
    if (!devSettings().passAlerts || satsCount() == 0) return;
    double jd = skyJd();
    bool stale = g_alertAt == 0 || (g_alertPass.valid && jd > g_alertPass.jdStop) || jd - g_alertAt > 0.1;
    if (stale && !g_alertBusy) {
        g_alertWorkJd = jd;
        g_alertBusy = bgSubmit(alertWork, alertDone, nullptr);
    }
    const SatPass &p = g_alertPass;
    if (!p.valid || !p.visible) return;
    double toStart = (p.jdStart - jd) * 1440;
    char b[64];
    if (toStart < 2 && toStart > 0 && g_alertedPassJd != p.jdStart) {
        g_alertedPassJd = p.jdStart;
        snprintf(b, sizeof b, "%s rises in 2 min from the %s", satName(0), compassPoint(p.azStart));
        toast(b, C_SAT, 5000);
        devHaptic(H_ALERT);
        sndPlay(SFX_ALERT);
    }
    if (toStart <= 0 && jd < p.jdStop && g_alertedRiseJd != p.jdStart) {
        g_alertedRiseJd = p.jdStart;
        ObjRef r; r.kind = OK_SAT; r.idx = 0;
        startFind(r);
        devHaptic(H_ALERT);
        sndPlay(SFX_ALERT);
    }
}

void uiLoop()
{
    uint32_t ms = lv_tick_get();
    Settings &s = devSettings();
    bgPoll();
    static bool prefetched = false;
    if (!prefetched && ms > 2500) {              // warm the caches so the first swipe is instant
        prefetched = true;
        requestTonight(true);
        computePasses(true);
    }
    if (ms - g_lastSkyUpdate > 100) {          // 10 Hz: stars and satellites glide instead of stepping
        skyUpdate(devUnixTime(), devObserver());
        g_lastSkyUpdate = ms;
    }
    if (ms - g_lastFrame < 16) return;          // draw as fast as frames finish, up to ~60 fps
    float dt = (ms - g_lastFrame) / 1000.0f;
    g_lastFrame = ms;
    float q[4];
    if (devQuat(q)) orientFeed(q, dt);
    float a[3];
    if (devAccel(a)) orientFeedAccel(a);
    if (g_tile == 0) {
        skyviewSetCamera(orientCamera(s.pointMode, s.axisRot, kFovs[s.fovIndex % 4]));
        skyviewSetTime(ms);
        if (skyviewUpdateLock() && skyviewLock().valid()) { devHaptic(H_TICK); sndPlay(SFX_LOCK); }
        findLogic(ms);
        updateHud();
        lv_obj_invalidate(g_sky);
        g_uiFrames++;
    } else if (g_tile == TILE_GPS && g_gpsPlot) {
        static uint32_t lastG = 0;
        if (ms - lastG > 250) { refreshGps(); lastG = ms; }   // pill breathing + numbers
        lv_obj_invalidate(g_gpsPlot);                         // radar sweep
    } else if (g_tile == TILE_ORBIT && g_orbPlot && g_passSat >= 0) {
        static uint32_t last = 0;
        if (ms - last > 1000) { refreshOrbit(false); last = ms; }
        lv_obj_invalidate(g_orbPlot);
    }
    if (ms - g_lastSlow > 1000) {
        g_lastSlow = ms;
        passAlerts();
        if (g_tile == TILE_SETUP) refreshGpsLabel();
        /* GPS lock / loss: tell the user wherever they are */
        if (devHasGps()) {
            GpsDetail d;
            devGpsDetail(d);
            if (d.fix && !g_gpsFixPrev) {
                char b[48];
                snprintf(b, sizeof b, "GPS locked  \xC2\xB7  %d satellites", d.used);
                toast(b, C_OK, 2500);
                devHaptic(H_LOCK);
                sndPlay(SFX_GPS);
                g_lockFlashMs = ms;
            }
            if (!d.fix && g_gpsFixPrev) g_searchSince = ms;
            g_gpsFixPrev = d.fix;
        }
        static TleState lastTle = TLE_IDLE;
        TleState ts = devTleState();
        if (ts != lastTle && (ts == TLE_OK || ts == TLE_FAIL)) {
            toast(devTleMsg(), ts == TLE_OK ? C_OK : C_ACCENT2, 3500);
            sndPlay(ts == TLE_OK ? SFX_OK : SFX_ERROR);
            g_npass = 0; g_passSat = -1;
            computePasses(true);
            refreshOrbit(true);
            requestTonight(true);
            g_alertAt = 0;
        }
        lastTle = ts;
    }
}

#ifndef ARDUINO
#include "misc/lv_event_private.h"
/* simulator hook: open the password keyboard for a screenshot */
void uiDebugKeyboard(const char *ssid, const char *typed, int mode)
{
    static char s[33];
    strlcpy(s, ssid, sizeof s);
    lv_obj_t *dummy = lv_obj_create(lv_layer_top());
    lv_event_t e;
    memset(&e, 0, sizeof e);
    e.user_data = s;
    e.current_target = e.original_target = dummy;
    wifiPick(&e);
    lv_obj_delete(dummy);
    strlcpy(g_pw, typed, sizeof g_pw);
    g_pwLen = (int)strlen(g_pw);
    g_kbMode = (uint8_t)mode;
    kbRefresh();
    pwShow();
}
#endif
