/**
 * skyview.h - the live AR sky renderer (one full-screen LVGL object, custom draw).
 */
#pragma once
#include <lvgl.h>
#include "sky.h"

struct SkyHud {
    char clock[12];        // "21:42"
    char status[56];       // "GPS 9 · 87%"
    bool gpsFix;
    bool calibrated;
    bool timeValid;
    bool locationValid;
    const char *banner;    // optional top banner text (nullptr = none)
    uint32_t bannerColor;
    const char *banner2;   // second banner line
};

lv_obj_t *skyviewCreate(lv_obj_t *parent);
void skyviewSetCamera(const Camera &c);
const Camera &skyviewCamera();
void skyviewSetHud(const SkyHud &h);
void skyviewSetTime(uint32_t ms);       // animation clock

/* Lock-on: object nearest the reticle. Returns true when the lock changed. */
bool skyviewUpdateLock();
ObjRef skyviewLock();
void skyviewClearLock();

/* Find mode: guide the user to a target. */
void skyviewSetTarget(const ObjRef &t);
ObjRef skyviewTarget();
const ObjInfo &skyviewTargetInfo();   // cached, refreshed twice a second
double skyviewTargetDistanceDeg();      // angle between boresight and target

/* Align mode: show the altitude band for a target while calibrating. */
void skyviewSetAlignTarget(const ObjRef &t, bool northMode);
void skyviewSetFovLabel(const char *s);
