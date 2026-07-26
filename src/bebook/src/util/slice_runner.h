#ifndef UTIL_SLICE_RUNNER_H_
#define UTIL_SLICE_RUNNER_H_

#include "util/sliced_job.h"

#include <deque>
#include <memory>

// Runs jobs a slice at a time, once per frame, inside the slack the frame limiter would
// otherwise sleep away.
class SliceRunner
{
public:
    void submit(std::unique_ptr<SlicedJob> job);

    // Drop everything pending. Used when what the jobs were computing stops being wanted --
    // the cursor moved, the article changed -- so their results cannot arrive late and
    // overwrite something newer.
    void clear();

    bool idle() const;

    // One slice. Jobs are served oldest first; a finished one is dropped and the next takes
    // whatever budget is left.
    void run(Budget budget);

private:
    std::deque<std::unique_ptr<SlicedJob>> jobs;
};

#endif
