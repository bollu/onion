#include "util/job_runner.h"

void JobRunner::submit(std::unique_ptr<Job> job)
{
    if (job != nullptr)
    {
        jobs.push_back(std::move(job));
    }
}

void JobRunner::clear()
{
    jobs.clear();
}

bool JobRunner::idle() const
{
    return jobs.empty();
}

void JobRunner::run(Budget budget)
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
