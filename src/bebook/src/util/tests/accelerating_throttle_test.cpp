#include "util/accelerating_throttle.h"

#include <gtest/gtest.h>

#include <vector>

namespace
{
constexpr uint32_t INIT = 250;
constexpr uint32_t START = 180;
constexpr uint32_t MIN = 40;
constexpr float TAU = 300.0f;
constexpr uint32_t DT = 16;  // ~one frame at 60fps

// Drive a fresh throttle with a held key rising by DT each frame up to `hold_ms`, returning
// the cumulative held_ms values at which it fired.
std::vector<uint32_t> fire_times(AcceleratingThrottle &t, uint32_t hold_ms)
{
    std::vector<uint32_t> fires;
    for (uint32_t held = 0; held <= hold_ms; held += DT)
    {
        if (t(held)) fires.push_back(held);
    }
    return fires;
}
}  // namespace

TEST(AcceleratingThrottle, FirstFireAfterInitialDelay)
{
    AcceleratingThrottle t(INIT, START, MIN, TAU);
    auto fires = fire_times(t, 2000);
    ASSERT_FALSE(fires.empty());
    EXPECT_GE(fires.front(), INIT);
    EXPECT_LT(fires.front(), INIT + DT + START);
}

TEST(AcceleratingThrottle, GapsShrinkTowardFloorAndAccelerate)
{
    AcceleratingThrottle t(INIT, START, MIN, TAU);
    auto fires = fire_times(t, 3000);
    ASSERT_GE(fires.size(), 5u);

    std::vector<uint32_t> gaps;
    for (size_t i = 1; i < fires.size(); ++i)
    {
        gaps.push_back(fires[i] - fires[i - 1]);
    }

    for (uint32_t g : gaps)
    {
        // A measured gap is the scheduled interval rounded up to the next frame sample, so it
        // stays within [min_interval, start_interval + one frame].
        EXPECT_GE(g, MIN);
        EXPECT_LE(g, START + DT);
    }

    // Monotonically non-increasing (allowing one frame of sampling jitter).
    for (size_t i = 1; i < gaps.size(); ++i)
    {
        EXPECT_LE(gaps[i], gaps[i - 1] + DT);
    }

    // It actually accelerated: late repeats are meaningfully tighter than early ones, and the
    // steady-state rate has settled near the floor.
    EXPECT_LT(gaps.back(), gaps.front());
    EXPECT_LE(gaps.back(), MIN + DT);
}

TEST(AcceleratingThrottle, ReleaseReArmsInitialDelay)
{
    AcceleratingThrottle t(INIT, START, MIN, TAU);

    // Hold long enough to reach the fast steady state.
    fire_times(t, 1500);

    // Release: held_ms drops to 0. That frame must not fire and must re-arm the initial delay.
    EXPECT_FALSE(t(0));

    // Press again: the first repeat is once more gated by the full initial delay, not the fast
    // rate the previous hold had ramped to.
    bool fired_early = false;
    for (uint32_t held = DT; held < INIT; held += DT)
    {
        if (t(held)) fired_early = true;
    }
    EXPECT_FALSE(fired_early);
    EXPECT_TRUE(t(INIT));
}
