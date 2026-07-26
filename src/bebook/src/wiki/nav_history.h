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

    size_t size() const;

    // The trail, oldest first, for the breadcrumb. Read-only: navigation still goes through
    // push/pop, so this cannot desynchronise from where B will actually take you.
    const std::vector<HistoryEntry> &entries() const;
    void clear();

    // Forward, in the ordinary browser sense: stepping back remembers what you stepped out
    // of, and following any new link forgets it. Exists because walking a breadcrumb with
    // L1/R1 only makes sense in both directions -- back alone is what B already does.
    //
    // The caller records the departure, because only it knows the address it is leaving at.
    void push_forward(HistoryEntry entry);
    bool can_go_forward() const;
    const HistoryEntry &peek_forward() const;
    HistoryEntry pop_forward();

    // Following a new link invalidates the forward trail, exactly as in a browser.
    void clear_forward();

private:
    const size_t max_depth;
    std::vector<HistoryEntry> stack;
    std::vector<HistoryEntry> forward;
};

#endif
