#ifndef UTIL_SLICED_JOB_H_
#define UTIL_SLICED_JOB_H_

#include "util/budget.h"

// Work that can be done a slice at a time, so a long computation never costs a frame.
//
// A hand-rolled state machine rather than a coroutine because the device toolchain is GCC
// 8.3 and the build is -std=c++17; coroutines are C++20 and need GCC 10. The shape is the
// same either way -- a job's phases are exactly the suspend points a coroutine would
// generate -- so if the toolchain moves, an implementation becomes a generator without this
// interface changing.
//
// The rule that keeps it safe: a job must not hold anything across a yield that something
// else could invalidate, and must release what it owns in its destructor. A job is dropped
// the moment its result stops being wanted, mid-slice and without warning.
class SlicedJob
{
public:
    virtual ~SlicedJob() = default;

    // Do work until `budget` is spent. Returns true when the job is finished and should be
    // dropped, false when it yielded and wants another slice. Implementations check the
    // budget between indivisible steps and never inside one.
    virtual bool step(Budget &budget) = 0;
};

#endif
