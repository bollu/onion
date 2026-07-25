#include "./link_runs.h"

#include <algorithm>

std::vector<LinkRun> slice_link_runs(const std::vector<LinkRun> &runs, uint32_t offset, uint32_t length)
{
    std::vector<LinkRun> out;
    if (length == 0)
    {
        return out;
    }

    const uint32_t slice_end = offset + length;
    for (const auto &run : runs)
    {
        const uint32_t run_end = run.offset + run.length;
        const uint32_t from = std::max(run.offset, offset);
        const uint32_t to = std::min(run_end, slice_end);
        if (from < to)
        {
            out.push_back(LinkRun{from - offset, to - from, run.target});
        }
    }

    return out;
}

void shift_link_runs(std::vector<LinkRun> &runs, uint32_t delta)
{
    for (auto &run : runs)
    {
        run.offset += delta;
    }
}

const LinkRun *link_run_overlapping(const std::vector<LinkRun> &runs, uint32_t start, uint32_t end)
{
    if (start >= end)
    {
        return nullptr;
    }

    for (const auto &run : runs)
    {
        if (run.length == 0)
        {
            continue;
        }
        if (start < run.offset + run.length && run.offset < end)
        {
            return &run;
        }
    }

    return nullptr;
}
