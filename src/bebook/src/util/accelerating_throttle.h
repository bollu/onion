#ifndef ACCELERATING_THROTTLE_H_
#define ACCELERATING_THROTTLE_H_

#include <cstdint>

// A held-key repeat gate like Throttled, but with a repeat interval that eases exponentially
// from a slow start toward a fast floor the longer the key is held -- "typematic acceleration".
// Driven by the same cumulative held_ms as Throttled (HeldKeyTracker resets it to 0 on release),
// so a release re-arms the initial delay and motion stops instantly, with no momentum.
//
//   interval(held) = min_interval + (start_interval - min_interval) * exp(-held / tau)
//
// so the first repeats are ~start_interval apart and, after a while, ~min_interval apart.
class AcceleratingThrottle
{
    const uint32_t init_delay_ms;      // delay before the first repeat
    const uint32_t start_interval_ms;  // interval at the start of the repeat (slow)
    const uint32_t min_interval_ms;    // interval floor == max rate (fast)
    const float tau_ms;                // ease-in time constant

    uint32_t last_held_ms;
    uint32_t next_fire_ms;

public:
    AcceleratingThrottle(
        uint32_t init_delay_ms,
        uint32_t start_interval_ms,
        uint32_t min_interval_ms,
        float tau_ms
    );

    // True on the frames a repeat should fire, given the cumulative time the key has been held.
    bool operator()(uint32_t held_ms);
};

#endif
