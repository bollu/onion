#ifndef NAV_STATE_H_
#define NAV_STATE_H_

// The article view's navigation lifecycle, as an explicit machine rather than a set of
// booleans. It replaces a bare `bool is_done`, which had nowhere to express three of the
// transitions below -- so all three were bugs: a view that finished could never be
// reopened, a queued navigation had no state to sit in and so ran inside the callback
// whose owner it destroyed, and a failed navigation was indistinguishable from having
// nothing left to go back to.
namespace nav
{

enum class State
{
    // Constructed, or reopened after Done. No article yet, so nothing to draw and no key
    // to handle beyond leaving.
    Empty,

    // Showing an article.
    Active,

    // A target is pending. The swap is deferred to here because following a link is
    // reported from inside TokenView's own key handler, and performing it immediately
    // would destroy the TokenView whose method is still on the stack.
    NavigationQueued,

    // Finished; the view stack should pop it. Not terminal -- see Reopened.
    Done,
};

// Events are pre-resolved: the caller decides whether history remains, so the transition
// itself stays a total function of (state, event) with nothing else to consult. That is
// what makes every row of the table testable without a TokenView or an SDL surface.
enum class Event
{
    OpenSucceeded,
    OpenFailed,
    LinkFollowed,
    BackToPrevious,      // B pressed, history remains
    BackExhausted,       // B pressed, nothing to go back to
    HomeRequested,       // leave for the reading list in one press
    QueuedNavSucceeded,
    QueuedNavFailed,     // stay put and report; the history is untouched
    Reopened,            // pushed onto the stack again after Done
};

// Total: an event with no transition from the current state leaves it unchanged, which is
// a decision rather than an oversight -- stray input must not move the machine.
State transition(State from, Event event);

// True when the view stack should pop this view.
bool is_finished(State state);

// True when there is an article to draw and to route keys to.
bool has_article(State state);

const char *to_string(State state);

}

#endif
