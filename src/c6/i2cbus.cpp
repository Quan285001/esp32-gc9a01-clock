#include "i2cbus.h"
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace i2cbus {

static SemaphoreHandle_t mtx = nullptr;
static bool     started  = false;
static uint32_t nTimeout = 0;      // callers that gave up waiting for the lock
static uint32_t nFail    = 0;      // transfers in a row that got no acknowledge

// Per-transfer timeout. Wire defaults to 50 ms, which is already bounded, but
// every device on this bus answers in well under a millisecond - a transfer
// still going after 20 ms is not slow, it is broken, and waiting out the rest
// of the default only lengthens the stall for whoever is queued behind us.
static const uint16_t XFER_MS = 20;

// A stuck slave holds SDA low waiting to finish a read it thinks is still in
// progress. The way out is to give it the clocks it is waiting for: up to
// nine pulses on SCL with SDA released, until it lets go, then a STOP. This
// only ever runs from a caller that holds the mutex.
static void recover()
{
    Wire.end();
    pinMode(PIN_SDA, INPUT_PULLUP);
    pinMode(PIN_SCL, OUTPUT_OPEN_DRAIN);
    digitalWrite(PIN_SCL, HIGH);
    for (int i = 0; i < 9 && digitalRead(PIN_SDA) == LOW; i++) {
        digitalWrite(PIN_SCL, LOW);  delayMicroseconds(5);
        digitalWrite(PIN_SCL, HIGH); delayMicroseconds(5);
    }
    // STOP: SDA rising while SCL is high.
    pinMode(PIN_SDA, OUTPUT_OPEN_DRAIN);
    digitalWrite(PIN_SDA, LOW);  delayMicroseconds(5);
    digitalWrite(PIN_SCL, HIGH); delayMicroseconds(5);
    digitalWrite(PIN_SDA, HIGH); delayMicroseconds(5);

    Wire.begin(PIN_SDA, PIN_SCL, 400000);
    Wire.setTimeOut(XFER_MS);
    nFail = 0;
}

void begin()
{
    if (!mtx) mtx = xSemaphoreCreateMutex();
    if (started) return;
    started = true;
    Wire.begin(PIN_SDA, PIN_SCL, 400000);
    Wire.setTimeOut(XFER_MS);
}

Hold::Hold(uint32_t waitMs)
{
    held = !mtx || xSemaphoreTake(mtx, pdMS_TO_TICKS(waitMs)) == pdTRUE;
    if (!held) nTimeout++;
}

Hold::~Hold()
{
    if (held && mtx) xSemaphoreGive(mtx);
}

// Every transfer reports back so the failure count means something: a bus
// that has stopped acknowledging anything gets clocked out and re-opened
// rather than being retried forever.
static bool done(bool okay)
{
    if (okay) { nFail = 0; return true; }
    if (++nFail >= 20) recover();
    return false;
}

bool write8(uint8_t addr, uint8_t reg, uint8_t val, uint32_t waitMs)
{
    Hold h(waitMs);
    if (!h.ok()) return false;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return done(Wire.endTransmission() == 0);
}

bool read8(uint8_t addr, uint8_t reg, uint8_t &val, uint32_t waitMs)
{
    Hold h(waitMs);
    if (!h.ok()) return false;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return done(false);
    if (Wire.requestFrom((int)addr, 1) != 1) return done(false);
    val = Wire.read();
    return done(true);
}

bool present(uint8_t addr)
{
    Hold h;
    if (!h.ok()) return false;
    Wire.beginTransmission(addr);
    // Not counted as a failure: probing an address that is not there is a
    // question, not a fault, and startup asks it of several.
    return Wire.endTransmission() == 0;
}

uint32_t timeouts() { return nTimeout; }

} // namespace i2cbus
