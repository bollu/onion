#include "util/budget.h"

#include <SDL/SDL.h>

Budget Budget::until(uint32_t deadline_ms)
{
    Budget b;
    b.kind = Kind::Deadline;
    b.deadline_ms = deadline_ms;
    return b;
}

Budget Budget::units(int n)
{
    Budget b;
    b.kind = Kind::Units;
    b.remaining = n;
    return b;
}

Budget Budget::unlimited()
{
    return Budget();
}

bool Budget::spent()
{
    switch (kind)
    {
        case Kind::Unlimited:
            return false;

        case Kind::Units:
            if (remaining <= 0)
            {
                return true;
            }
            --remaining;
            return false;

        case Kind::Deadline:
            if (++since_check < CHECK_INTERVAL)
            {
                return false;
            }
            since_check = 0;
            // Unsigned comparison against a tick count that wraps every ~49 days: the
            // subtraction is what makes the wrap harmless, where `now >= deadline` would
            // read as "expired" for 49 days after a wrap.
            return static_cast<int32_t>(SDL_GetTicks() - deadline_ms) >= 0;
    }
    return false;
}
