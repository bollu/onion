#ifndef DEBOUNCED_H_
#define DEBOUNCED_H_

#include <cstdint>

// Trailing edge to Throttled's leading edge: fires once the pokes stop, not once they start.
class Debounced
{
    const uint32_t quiet_ms;

    bool pending;
    uint32_t deadline_ms;

public:

    explicit Debounced(uint32_t quiet_ms);

    // Something changed; (re)arm, pushing any existing deadline out.
    void poke(uint32_t now_ms);

    // True exactly once per armed period, once quiet_ms has passed with no further poke.
    bool operator()(uint32_t now_ms);

    bool is_pending() const;

    // Disarm without firing.
    void cancel();

    // Arm with an already-expired deadline, so the next call fires. For shutdown.
    void flush();
};

#endif
