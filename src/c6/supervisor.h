// ---------------------------------------------------------------------------
//  Notices when the clock has stopped, and remembers why across the restart.
//
//  Arduino leaves loop() unwatched - loopTaskWDTEnabled is false and the task
//  watchdog only ever sees the idle tasks, which keep running happily while
//  loop() sits blocked on a semaphore. So a render loop that stops has
//  nothing to catch it: the panel holds its last frame, the time on the glass
//  quietly becomes wrong, and the clock stays that way until it is unplugged.
//  That is exactly the failure that has been showing up here after hours.
//
//  The awkward part of chasing it was that the diagnostics were on the
//  display, which is the one thing a freeze takes away - by the time there is
//  something to read, there is no way to read it. So this keeps its notes in
//  RTC memory, which survives a restart, and reports them on the way back up.
//
//  A supervisor task watches two counters. If loop() stops going round, or
//  goes round without ever drawing, it writes down where the loop was, how
//  long it had been up and what the counters said, and restarts. Coming back
//  takes a few seconds and the RTC seeds the time, so the cost of being wrong
//  is small - much smaller than a clock showing 8:36 at half past eleven.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

namespace supervisor {

// Where loop() had got to. Ordered as the loop runs, so a report reads as a
// position in the pass rather than a bare number.
enum Phase : uint8_t {
    P_BOOT = 0,
    P_TZ,        // applying a timezone the weather task asked for
    P_TIME,      // reading the clock
    P_PORTAL,    // the setup hotspot's web server
    P_IMU,       // orientation, over I2C
    P_ALARM,     // an alarm screen owns the frame
    P_TOUCH,     // draining gestures
    P_RENDER,    // drawing a face
    P_NET,       // the once-a-second link and sync check
    P_IDLE,      // end of a pass, nothing outstanding
    P_COUNT
};

// Starts the watch. Call once, at the end of setup(), so the long waits for
// WiFi and NTP are not mistaken for a hang.
void begin();

// Where the loop is now. One store; safe to call as often as it is useful.
void phase(Phase p);

// The loop completed a pass. This is the heartbeat the supervisor watches.
void alive();

// Something reached the panel. Kept separate from alive() because the two
// fail differently: a loop that stops going round is blocked on something,
// while a loop that goes round without ever drawing is a display that has
// stopped taking frames. Both leave the same stale picture on the glass, and
// only these two counters tell them apart afterwards.
void drew();

// What happened last time, or nullptr if this was a clean start. Two short
// lines meant for the diagnostics panel; valid for the life of the run.
const char *report1();
const char *report2();

// How many times we have restarted ourselves since the last power-on.
uint32_t hangs();

} // namespace supervisor
