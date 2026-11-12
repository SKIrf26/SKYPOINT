/**
 * bg.h - tiny background job runner.
 *
 * work() runs on a low-priority worker task (second core on the ESP32-S3), then done()
 * runs back on the UI thread from bgPoll(), so done() may touch LVGL. On the desktop
 * simulator work() runs inline and done() is still deferred to bgPoll().
 * work() must only use thread-safe code: the pure astronomy functions, or private copies.
 */
#pragma once

typedef void (*BgFn)(void *ctx);

bool bgSubmit(BgFn work, BgFn done, void *ctx);   // false if the queue is full
void bgPoll();                                     // call from the UI loop
bool bgIdle();
