#include "./accelerating_throttle.h"

#include <cmath>

AcceleratingThrottle::AcceleratingThrottle(
    uint32_t init_delay_ms,
    uint32_t start_interval_ms,
    uint32_t min_interval_ms,
    float tau_ms
)
    : init_delay_ms(init_delay_ms),
      start_interval_ms(start_interval_ms),
      min_interval_ms(min_interval_ms),
      tau_ms(tau_ms),
      last_held_ms(0),
      next_fire_ms(init_delay_ms)
{
}

bool AcceleratingThrottle::operator()(uint32_t held_ms)
{
    bool fire = false;

    if (held_ms <= last_held_ms)
    {
        // Held time went backward (or to 0): the key was released/re-pressed. Re-arm.
        next_fire_ms = init_delay_ms;
    }

    if (held_ms >= next_fire_ms)
    {
        fire = true;

        // Interval eases from start_interval down to the min_interval floor as the hold grows.
        const float decay = std::exp(-static_cast<float>(held_ms) / tau_ms);
        const float span = static_cast<float>(start_interval_ms - min_interval_ms);
        const uint32_t interval = min_interval_ms + static_cast<uint32_t>(span * decay);

        next_fire_ms = held_ms + interval;
    }

    last_held_ms = held_ms;
    return fire;
}
