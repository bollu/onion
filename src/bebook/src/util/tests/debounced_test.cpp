#include "../debounced.h"

#include <gtest/gtest.h>

TEST(DEBOUNCED, does_not_fire_without_a_poke)
{
    Debounced debounce(100);

    ASSERT_FALSE(debounce(0));
    ASSERT_FALSE(debounce(1000));
    ASSERT_FALSE(debounce(100000));
    ASSERT_FALSE(debounce.is_pending());
}

TEST(DEBOUNCED, fires_once_after_the_quiet_period)
{
    Debounced debounce(100);

    debounce.poke(0);
    ASSERT_TRUE(debounce.is_pending());

    ASSERT_FALSE(debounce(50));
    ASSERT_FALSE(debounce(99));
    ASSERT_TRUE(debounce(100));

    ASSERT_FALSE(debounce.is_pending());
    ASSERT_FALSE(debounce(150));
    ASSERT_FALSE(debounce(10000));
}

TEST(DEBOUNCED, poke_inside_the_window_pushes_the_deadline)
{
    Debounced debounce(100);

    debounce.poke(0);
    ASSERT_FALSE(debounce(50));

    debounce.poke(50);
    ASSERT_FALSE(debounce(100));
    ASSERT_FALSE(debounce(149));
    ASSERT_TRUE(debounce(150));
}

TEST(DEBOUNCED, a_burst_of_pokes_yields_one_fire)
{
    Debounced debounce(100);

    for (uint32_t t = 0; t < 500; t += 10)
    {
        debounce.poke(t);
        ASSERT_FALSE(debounce(t));
    }

    ASSERT_FALSE(debounce(589));
    ASSERT_TRUE(debounce(590));
    ASSERT_FALSE(debounce(700));
}

TEST(DEBOUNCED, rearms_after_firing)
{
    Debounced debounce(100);

    debounce.poke(0);
    ASSERT_TRUE(debounce(100));

    debounce.poke(200);
    ASSERT_FALSE(debounce(250));
    ASSERT_TRUE(debounce(300));
}

TEST(DEBOUNCED, cancel_disarms_without_firing)
{
    Debounced debounce(100);

    debounce.poke(0);
    debounce.cancel();

    ASSERT_FALSE(debounce.is_pending());
    ASSERT_FALSE(debounce(100));
    ASSERT_FALSE(debounce(1000));
}

TEST(DEBOUNCED, flush_fires_on_the_next_call)
{
    Debounced debounce(100000);

    debounce.poke(0);
    ASSERT_FALSE(debounce(500));

    debounce.flush();
    ASSERT_TRUE(debounce(500));
    ASSERT_FALSE(debounce(500));
}

TEST(DEBOUNCED, flush_fires_even_without_a_prior_poke)
{
    Debounced debounce(100);

    debounce.flush();
    ASSERT_TRUE(debounce(0));
    ASSERT_FALSE(debounce(0));
}

TEST(DEBOUNCED, zero_quiet_period_fires_on_the_next_call)
{
    Debounced debounce(0);

    debounce.poke(42);
    ASSERT_TRUE(debounce(42));
    ASSERT_FALSE(debounce(42));
}
