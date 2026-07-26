#include "./nav_history.h"

#include <utility>

NavHistory::NavHistory(size_t max_depth)
    : max_depth(max_depth == 0 ? 1 : max_depth)
{
}

void NavHistory::push(HistoryEntry entry)
{
    stack.push_back(std::move(entry));

    // Drop from the bottom: browsing for an hour should cost bounded memory, and the
    // oldest hop is the one least likely to be wanted.
    if (stack.size() > max_depth)
    {
        stack.erase(stack.begin(), stack.begin() + (stack.size() - max_depth));
    }
}

bool NavHistory::can_go_back() const
{
    return !stack.empty();
}

const HistoryEntry &NavHistory::peek() const
{
    return stack.back();
}

HistoryEntry NavHistory::pop()
{
    HistoryEntry entry = stack.back();
    stack.pop_back();
    return entry;
}

size_t NavHistory::size() const
{
    return stack.size();
}

const std::vector<HistoryEntry> &NavHistory::entries() const
{
    return stack;
}

void NavHistory::clear()
{
    stack.clear();
    forward.clear();
}

void NavHistory::push_forward(HistoryEntry entry)
{
    forward.push_back(std::move(entry));

    // Same bound as the back stack, and for the same reason.
    if (forward.size() > max_depth)
    {
        forward.erase(forward.begin(), forward.begin() + (forward.size() - max_depth));
    }
}

bool NavHistory::can_go_forward() const
{
    return !forward.empty();
}

const HistoryEntry &NavHistory::peek_forward() const
{
    return forward.back();
}

HistoryEntry NavHistory::pop_forward()
{
    HistoryEntry entry = forward.back();
    forward.pop_back();
    return entry;
}

void NavHistory::clear_forward()
{
    forward.clear();
}
