// ---------------------------------------------------------------------------
//  The board's one I2C bus, shared by everything that hangs off it.
//
//  Touch (0x38) is polled from its own task while the codec (0x18), the
//  TCA9554 expander (0x20), the RTC (0x51) and the IMU (0x6A) are driven from
//  the render loop and the audio task, so every transfer has to take the
//  lock. Wire itself is not re-entrant.
//
//  Nothing waits for the bus indefinitely. That is the whole point of the
//  timeouts below: a single wedged transaction - a slave holding SDA down, a
//  transfer that never completes - used to block the mutex holder forever,
//  and then every other task behind it. The render loop polls the IMU through
//  here every 150 ms, so it went too, and with it the display, the gestures
//  and the alarms. A clock that has lost the accelerometer for one frame is
//  worth having; one that has stopped is not.
//
//  Pins are IO18/IO8 per the board's GPIO table.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

namespace i2cbus {

static const int PIN_SDA = 18, PIN_SCL = 8;

// How long a caller will wait for the bus. Short for anything on the render
// loop - a missed sample costs nothing and a stalled frame costs everything -
// and longer for the touch task, which has nothing else to do.
static const uint32_t WAIT_RENDER = 40;
static const uint32_t WAIT_TASK   = 120;

// Idempotent: the first caller sets the bus up, the rest just get the lock.
void begin();

// Scoped try-lock. Check ok() before touching the bus; a false means somebody
// else has it and is not letting go, and the right answer is to skip this
// turn rather than to queue up behind them.
struct Hold {
    explicit Hold(uint32_t waitMs = WAIT_TASK);
    ~Hold();
    bool ok() const { return held; }
private:
    bool held;
};

// Single-register helpers, each taking the lock itself. They answer false if
// the bus was busy as well as if the device did not acknowledge - a caller
// that cares about the difference should use Hold directly.
bool write8(uint8_t addr, uint8_t reg, uint8_t val, uint32_t waitMs = WAIT_TASK);
bool read8 (uint8_t addr, uint8_t reg, uint8_t &val, uint32_t waitMs = WAIT_TASK);
bool present(uint8_t addr);

// How many times a caller gave up waiting. Should stay at zero; anything else
// means the bus is being held too long by somebody, which is what used to
// take the whole clock down with it.
uint32_t timeouts();

} // namespace i2cbus
