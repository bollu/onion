#ifndef UTIL_BUDGET_H_
#define UTIL_BUDGET_H_

#include <cstdint>

// How much work a job may do before it must yield.
//
// Two modes, deliberately. A deadline is what ships: the frame loop already sleeps away the
// slack between a frame's real cost and its allowance, so that slack is spent here instead
// and the frame rate is unchanged by construction. A unit count is what tests use, so a test
// never depends on a clock and "one unit at a time" is expressible exactly.
class Budget
{
public:
    // Wall-clock deadline, in the same milliseconds SDL_GetTicks returns.
    static Budget until(uint32_t deadline_ms);

    // Exactly `n` units, whatever they cost. For tests.
    static Budget units(int n);

    // No limit: run to completion. For the blocking callers that still want an answer now.
    static Budget unlimited();

    // Call between indivisible steps, never inside one. Consumes a unit and returns true
    // when the job must stop.
    bool spent();

private:
    enum class Kind { Deadline, Units, Unlimited };

    Kind kind = Kind::Unlimited;
    uint32_t deadline_ms = 0;
    int remaining = 0;

    // Reading the clock is not free on this device, so a deadline is only checked every so
    // many units. The interval is small enough that a unit's worst case cannot overshoot a
    // frame by anything a reader would see.
    static const int CHECK_INTERVAL = 32;
    int since_check = 0;
};

#endif
