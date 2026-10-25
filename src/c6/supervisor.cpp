#include "supervisor.h"
#include "i2cbus.h"
#include "amoled.h"
#include <esp_attr.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// The loop's own counters, declared at file scope: an extern inside a
// namespace declares a different symbol and does not link.
extern volatile uint32_t diagLoops, diagFrames;

namespace supervisor {

// How long each kind of silence is allowed to last. Both are far longer than
// anything the clock legitimately does - a face redraws at least once a
// second and a pass of loop() is milliseconds - so tripping one of these is
// evidence, not impatience. The slowest honest thing in a pass is the setup
// hotspot serving a page, which is under two seconds.
static const uint32_t LOOP_STALL_MS  = 30000;
static const uint32_t FRAME_STALL_MS = 90000;
static const uint32_t CHECK_MS       = 1000;

static const uint32_t MAGIC = 0x5A11EDU;

// RTC memory, left alone by the startup code, so it reads back after a
// restart. A power cycle leaves it as noise, which the magic catches.
struct Crumb {
    uint32_t magic;
    uint32_t hangs;        // restarts we have ordered since power-on
    uint32_t loops, frames;
    uint32_t upSec;
    uint32_t heap, minHeap;
    uint16_t i2cTo, dmaTo;
    uint8_t  phase;
    uint8_t  frameStall;   // the loop was running, the panel was not
    uint8_t  pending;      // written and not yet reported
};
static RTC_NOINIT_ATTR Crumb crumb;

static volatile uint8_t curPhase = P_BOOT;
static char line1[48] = "", line2[48] = "";
static bool haveReport = false;

static const char *PHASE_NAME[P_COUNT] = {
    "boot", "tz", "time", "portal", "imu", "alarm", "touch", "render", "net", "idle"
};

void phase(Phase p) { curPhase = (uint8_t)p; }

// Heartbeats. Plain stores of millis(): the supervisor only ever compares
// them against its own clock, so a torn read would cost one extra second of
// patience and nothing else.
static volatile uint32_t lastPass = 0, lastDraw = 0;
void alive() { lastPass = millis(); }
void drew()  { lastDraw = millis(); }

// Uptime as something a person can read at a glance on a small screen.
static void upStr(uint32_t sec, char *out, size_t n)
{
    if (sec >= 3600) snprintf(out, n, "%luh%02lum",
                              (unsigned long)(sec / 3600),
                              (unsigned long)((sec % 3600) / 60));
    else             snprintf(out, n, "%lum", (unsigned long)(sec / 60));
}

// Take down everything worth knowing, then restart. Written before the
// restart rather than after, because after is exactly when we would have
// nothing left to write it from.
static void giveUp(bool frameStall)
{
    crumb.magic      = MAGIC;
    crumb.hangs++;
    crumb.loops      = diagLoops;
    crumb.frames     = diagFrames;
    crumb.upSec      = millis() / 1000;
    crumb.heap       = ESP.getFreeHeap();
    crumb.minHeap    = ESP.getMinFreeHeap();
    crumb.i2cTo      = (uint16_t)i2cbus::timeouts();
    crumb.dmaTo      = (uint16_t)amoled::dmaTimeouts();
    crumb.phase      = curPhase;
    crumb.frameStall = frameStall ? 1 : 0;
    crumb.pending    = 1;

    Serial.printf("\n[supervisor] %s stalled in phase %s after %lus - restarting\n",
                  frameStall ? "panel" : "loop",
                  PHASE_NAME[curPhase < P_COUNT ? curPhase : 0],
                  (unsigned long)crumb.upSec);
    Serial.flush();
    esp_restart();
}

static void task(void *)
{
    TickType_t last = xTaskGetTickCount();
    // Everything starts counting from here, not from power-on: setup() has
    // just spent up to 45 seconds on WiFi and NTP without drawing anything.
    alive(); drew();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CHECK_MS));
        uint32_t now = millis();
        if (now - lastPass > LOOP_STALL_MS)  giveUp(false);
        if (now - lastDraw > FRAME_STALL_MS) giveUp(true);
    }
}

// Reset reasons that mean something went wrong, as opposed to a plug being
// pulled or a fresh flash.
static const char *badReset(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_PANIC:    return "panic";
        case ESP_RST_TASK_WDT: return "task wdt";
        case ESP_RST_INT_WDT:  return "int wdt";
        case ESP_RST_WDT:      return "wdt";
        case ESP_RST_BROWNOUT: return "brownout";
        default:               return nullptr;
    }
}

void begin()
{
    esp_reset_reason_t rr = esp_reset_reason();

    if (crumb.magic != MAGIC) {              // power-on: RTC memory is noise
        memset(&crumb, 0, sizeof crumb);
        crumb.magic = MAGIC;
    } else if (crumb.pending) {
        crumb.pending = 0;
        char up[16];
        upStr(crumb.upSec, up, sizeof up);
        snprintf(line1, sizeof line1, "%s stall @%s %s x%lu",
                 crumb.frameStall ? "panel" : "loop",
                 PHASE_NAME[crumb.phase < P_COUNT ? crumb.phase : 0],
                 up, (unsigned long)crumb.hangs);
        snprintf(line2, sizeof line2, "L%lu F%lu i2c%u dma%u min%luk",
                 (unsigned long)crumb.loops, (unsigned long)crumb.frames,
                 crumb.i2cTo, crumb.dmaTo,
                 (unsigned long)(crumb.minHeap / 1024));
        haveReport = true;
    } else if (const char *why = badReset(rr)) {
        // Not our doing - the chip fell over on its own. Worth saying so,
        // because it is a different fault from a freeze and wants a
        // different answer.
        snprintf(line1, sizeof line1, "restarted: %s", why);
        line2[0] = '\0';
        haveReport = true;
    }

    xTaskCreatePinnedToCore(task, "supervisor", 3072, nullptr, 6, nullptr, 0);
}

const char *report1() { return haveReport ? line1 : nullptr; }
const char *report2() { return haveReport && line2[0] ? line2 : nullptr; }
uint32_t    hangs()   { return crumb.magic == MAGIC ? crumb.hangs : 0; }

} // namespace supervisor
