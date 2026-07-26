#include "util/slice_runner.h"

void SliceRunner::submit(std::unique_ptr<SlicedJob> job)
{
    if (job != nullptr)
    {
        jobs.push_back(std::move(job));
    }
}

void SliceRunner::clear()
{
    jobs.clear();
}

bool SliceRunner::idle() const
{
    return jobs.empty();
}

void SliceRunner::run(Budget budget)
{
    while (!jobs.empty())
    {
        if (!jobs.front()->step(budget))
        {
            // Yielded rather than finished: the budget is gone, so nothing after it in the
            // queue would get any either.
            break;
        }
        jobs.pop_front();
    }
}
