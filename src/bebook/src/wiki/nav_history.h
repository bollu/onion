#ifndef NAV_HISTORY_H_
#define NAV_HISTORY_H_

#include "doc_api/doc_addr.h"

#include <cstdint>
#include <string>
#include <vector>

struct HistoryEntry
{
    std::string path;
    std::string title;
    DocAddr address;
};

// Where B goes. Deliberately not modelled as ViewStack depth: ViewStack has no pop, views
// remove themselves by reporting done, and a stack of ArticleViews would pin one
// never-evicting line buffer per article visited.
class NavHistory
{
public:
    explicit NavHistory(size_t max_depth = 64);

    // Records where we were, before navigating away.
    void push(HistoryEntry entry);

    bool can_go_back() const;

    // The most recent entry, left in place. Undefined unless can_go_back(). Navigation
    // peeks first and pops only once the article has opened, so a failed back-step costs
    // a message rather than the entry.
    const HistoryEntry &peek() const;

    // Removes and returns the most recent entry. Undefined unless can_go_back().
    HistoryEntry pop();

    // Keeps the top entry's address current as the reader scrolls.
    void update_top_address(DocAddr address);

    size_t size() const;
    void clear();

private:
    const size_t max_depth;
    std::vector<HistoryEntry> stack;
};

#endif
