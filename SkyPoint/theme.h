/**
 * theme.h - SkyPoint look & feel. Pure-black AMOLED canvas, cyan HUD, amber horizon.
 * Every colour goes through th_col() so the red "night vision" mode is one switch.
 */
#pragma once
#include <lvgl.h>

LV_FONT_DECLARE(f_mono13);
LV_FONT_DECLARE(f_mono16);
LV_FONT_DECLARE(f_ui16);
LV_FONT_DECLARE(f_ui20);
LV_FONT_DECLARE(f_ui24);
LV_FONT_DECLARE(f_ui30);
LV_FONT_DECLARE(f_orb18);
LV_FONT_DECLARE(f_orb26);
LV_FONT_DECLARE(f_orb40);
LV_FONT_DECLARE(f_orb56);

/* palette (day) */
#define C_BG        0x000000
#define C_HUD       0x5CF2FF   // primary cyan
#define C_HUD_DIM   0x1C6E78
#define C_ACCENT    0xFF9F43   // amber
#define C_ACCENT2   0xFF4F7B   // magenta-red
#define C_TEXT      0xEAF6FF
#define C_TEXT_DIM  0x7F95A6
#define C_CONLINE   0x1E8C99
#define C_CONNAME   0x2C8A96
#define C_GROUND    0x07120F
#define C_HORIZON   0xFF9F43
#define C_GRID      0x0F3440
#define C_CARD      0x06141B
#define C_OK        0x3DFFA2
#define C_WARN      0xFFC247
#define C_DSO       0xB98CFF
#define C_SAT       0x34F5C5

extern bool g_nightMode;

static inline lv_color_t th_col(uint32_t rgb)
{
    if (!g_nightMode) return lv_color_hex(rgb);
    uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    uint32_t l = (r * 54 + g * 183 + b * 19) >> 8;      // luminance
    if (l > 255) l = 255;
    return lv_color_make((uint8_t)l, (uint8_t)(l / 14), 0);
}

/* Display geometry: 410 x 502 AMOLED with rounded corners (~65 px radius). */
#define SCR_W 410
#define SCR_H 502
#define SAFE_R 65
