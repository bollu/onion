#include "util/budget.h"
#include "util/slice_runner.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

TEST(Budget, UnitsAllowExactlyThatManySteps)
{
    Budget b = Budget::units(3);
    EXPECT_FALSE(b.spent());
    EXPECT_FALSE(b.spent());
    EXPECT_FALSE(b.spent());
    EXPECT_TRUE(b.spent()) << "the fourth call is over budget";
    EXPECT_TRUE(b.spent()) << "and it stays over";
}

TEST(Budget, ZeroUnitsAllowsNothing)
{
    Budget b = Budget::units(0);
    EXPECT_TRUE(b.spent());
}

TEST(Budget, UnlimitedNeverExpires)
{
    Budget b = Budget::unlimited();
    for (int i = 0; i < 10000; ++i)
    {
        ASSERT_FALSE(b.spent());
    }
}

TEST(Budget, ADeadlineAlreadyPastStopsWork)
{
    // Checked only every CHECK_INTERVAL units, so an expired deadline takes that many calls
    // to notice. Reading the clock per unit would cost more than it saves.
    Budget b = Budget::until(0);
    int allowed = 0;
    while (!b.spent() && allowed < 1000)
    {
        ++allowed;
    }
    EXPECT_LT(allowed, 1000) << "an expired deadline must stop the job";
    EXPECT_LE(allowed, 64) << "and must notice within a couple of check intervals";
}

namespace
{

// Counts to `target`, one unit per step, recording how many slices it took.
class Counter : public SlicedJob
{
public:
    Counter(int target, int *done_at) : target(target), done_at(done_at) {}

    bool step(Budget &budget) override
    {
        while (count < target)
        {
            if (budget.spent())
            {
                return false;
            }
            ++count;
        }
        if (done_at != nullptr)
        {
            *done_at = count;
        }
        return true;
    }

private:
    int target;
    int count = 0;
    int *done_at;
};

// Reports whether it was destroyed, which is how a dropped job proves it cleans up.
class Tracked : public SlicedJob
{
public:
    explicit Tracked(bool *destroyed) : destroyed(destroyed) {}
    ~Tracked() override { *destroyed = true; }
    bool step(Budget &) override { return false; }  // never finishes

private:
    bool *destroyed;
};

}

TEST(SliceRunner, AJobSurvivesAcrossSlices)
{
    int done_at = 0;
    SliceRunner runner;
    runner.submit(std::unique_ptr<SlicedJob>(new Counter(10, &done_at)));

    // Four units at a time: the job must resume where it left off, not restart.
    runner.run(Budget::units(4));
    EXPECT_FALSE(runner.idle());
    runner.run(Budget::units(4));
    EXPECT_FALSE(runner.idle());
    runner.run(Budget::units(4));

    EXPECT_TRUE(runner.idle()) << "ten units of work inside twelve";
    EXPECT_EQ(done_at, 10);
}

TEST(SliceRunner, OneUnitAtATimeReachesTheSameAnswer)
{
    // The property a hand-rolled state machine gets wrong: sliced and unsliced must agree.
    int sliced = 0;
    SliceRunner a;
    a.submit(std::unique_ptr<SlicedJob>(new Counter(50, &sliced)));
    for (int i = 0; i < 100 && !a.idle(); ++i)
    {
        a.run(Budget::units(1));
    }

    int whole = 0;
    SliceRunner b;
    b.submit(std::unique_ptr<SlicedJob>(new Counter(50, &whole)));
    b.run(Budget::unlimited());

    EXPECT_TRUE(a.idle());
    EXPECT_EQ(sliced, whole);
}

TEST(SliceRunner, JobsRunOldestFirst)
{
    int first = 0, second = 0;
    SliceRunner runner;
    runner.submit(std::unique_ptr<SlicedJob>(new Counter(2, &first)));
    runner.submit(std::unique_ptr<SlicedJob>(new Counter(2, &second)));

    runner.run(Budget::units(2));
    EXPECT_EQ(first, 2) << "the older job finishes before the newer one starts";
    EXPECT_EQ(second, 0);
}

TEST(SliceRunner, ClearingDropsPendingJobs)
{
    // What happens when the cursor moves on: results nobody wants must not arrive late.
    bool destroyed = false;
    SliceRunner runner;
    runner.submit(std::unique_ptr<SlicedJob>(new Tracked(&destroyed)));
    runner.run(Budget::units(1));
    ASSERT_FALSE(destroyed);

    runner.clear();
    EXPECT_TRUE(destroyed) << "a dropped job is destroyed, so it can release what it holds";
    EXPECT_TRUE(runner.idle());
}
