#include "./debounced.h"

Debounced::Debounced(uint32_t quiet_ms)
    : quiet_ms(quiet_ms),
      pending(false),
      deadline_ms(0)
{
}

void Debounced::poke(uint32_t now_ms)
{
    pending = true;
    deadline_ms = now_ms + quiet_ms;
}

bool Debounced::operator()(uint32_t now_ms)
{
    if (!pending || now_ms < deadline_ms)
    {
        return false;
    }

    pending = false;
    return true;
}

bool Debounced::is_pending() const
{
    return pending;
}

void Debounced::cancel()
{
    pending = false;
}

void Debounced::flush()
{
    pending = true;
    deadline_ms = 0;
}
