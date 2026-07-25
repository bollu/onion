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

HistoryEntry NavHistory::pop()
{
    HistoryEntry entry = stack.back();
    stack.pop_back();
    return entry;
}

void NavHistory::update_top_address(DocAddr address)
{
    if (!stack.empty())
    {
        stack.back().address = address;
    }
}

size_t NavHistory::size() const
{
    return stack.size();
}

void NavHistory::clear()
{
    stack.clear();
}
