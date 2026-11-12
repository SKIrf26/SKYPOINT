#include "bg.h"
#include <stddef.h>

struct Job { BgFn work, done; void *ctx; };

#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

static QueueHandle_t g_todo = nullptr, g_done = nullptr;
static volatile int g_pending = 0;

static void worker(void *)
{
    Job j;
    for (;;) {
        if (xQueueReceive(g_todo, &j, portMAX_DELAY) == pdTRUE) {
            if (j.work) j.work(j.ctx);
            xQueueSend(g_done, &j, portMAX_DELAY);
            vTaskDelay(1);
        }
    }
}

static void start()
{
    if (g_todo) return;
    g_todo = xQueueCreate(6, sizeof(Job));
    g_done = xQueueCreate(6, sizeof(Job));
    /* Idle priority: FreeRTOS time-slices it with the idle task, so a job that computes for
     * several seconds never starves IDLE and never trips the task watchdog (5 s, panics). */
#if CONFIG_FREERTOS_UNICORE
    xTaskCreate(worker, "bgjobs", 10240, nullptr, tskIDLE_PRIORITY, nullptr);
#else
    xTaskCreatePinnedToCore(worker, "bgjobs", 10240, nullptr, tskIDLE_PRIORITY, nullptr, 0);
#endif
}

bool bgSubmit(BgFn work, BgFn done, void *ctx)
{
    start();
    Job j = { work, done, ctx };
    if (xQueueSend(g_todo, &j, 0) != pdTRUE) return false;
    g_pending++;
    return true;
}

void bgPoll()
{
    if (!g_done) return;
    Job j;
    while (xQueueReceive(g_done, &j, 0) == pdTRUE) {
        g_pending--;
        if (j.done) j.done(j.ctx);
    }
}

bool bgIdle() { return g_pending == 0; }

#else   /* desktop: run inline, defer done() */
static Job g_q[8];
static int g_n = 0;

bool bgSubmit(BgFn work, BgFn done, void *ctx)
{
    if (g_n >= 8) return false;
    if (work) work(ctx);
    g_q[g_n++] = { work, done, ctx };
    return true;
}

void bgPoll()
{
    Job q[8];
    int n = g_n;
    for (int i = 0; i < n; i++) q[i] = g_q[i];
    g_n = 0;
    for (int i = 0; i < n; i++)
        if (q[i].done) q[i].done(q[i].ctx);
}

bool bgIdle() { return g_n == 0; }
#endif
