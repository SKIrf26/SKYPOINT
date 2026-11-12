/**
 * SkyPoint - point your watch at the sky and it tells you what's there.
 *
 * Stars, constellations, planets, the Moon (with its real phase), Messier objects and
 * satellites (ISS, Tiangong, Hubble) drawn live as you move the watch.
 *
 * Boards:
 *   LilyGO T-Watch Ultra (ESP32-S3)    board "LilyGo T-Watch-Ultra"; runs standalone or from
 *                                      bmorcelli's Launcher. GPS for time + position.
 *   Waveshare ESP32-C6-Touch-AMOLED-2.06  board "ESP32C6 Dev Module", 4 MB flash, "Huge APP" partitions; standalone.
 *                                      Time + position from Wi-Fi sync or a chosen city.
 * See README.md for libraries and build commands.
 */
#include <Arduino.h>   // brings in sdkconfig (CONFIG_IDF_TARGET_*)
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
#include <LilyGoLib.h>
#include <LV_Helper.h>
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#include <lvgl.h>
#else
#error "SkyPoint supports the LilyGO T-Watch Ultra and the Waveshare ESP32-C6-Touch-AMOLED-2.06."
#endif
#include "device.h"
#include "ui.h"

/* LVGL software drawing + the sky renderer need more than the default 8 KB loop stack */
SET_LOOP_TASK_STACK_SIZE(20 * 1024);

void setup()
{
    devBegin();
    uint32_t t = millis();
    Serial.printf("[boot] uiBegin...  free heap %u\n", (unsigned)ESP.getFreeHeap());
    uiBegin();
    Serial.printf("[boot] uiBegin done in %u ms, free heap %u\n", (unsigned)(millis() - t), (unsigned)ESP.getFreeHeap());
}

void loop()
{
    devLoop();
    uiLoop();
    lv_timer_handler();
    delay(1);
}
