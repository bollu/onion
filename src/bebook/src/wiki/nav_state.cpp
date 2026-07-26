#include "./nav_state.h"

namespace nav
{

State transition(State from, Event event)
{
    switch (from)
    {
        case State::Empty:
            switch (event)
            {
                case Event::OpenSucceeded: return State::Active;
                // Nothing opened, so there is nothing to show and no way back: retire
                // rather than sit on the stack swallowing input, which is what the
                // bool version did.
                case Event::OpenFailed:    return State::Done;
                default:                   return from;
            }

        case State::Active:
            switch (event)
            {
                case Event::LinkFollowed:   return State::NavigationQueued;
                case Event::BackToPrevious: return State::NavigationQueued;
                case Event::BackExhausted:  return State::Done;
                case Event::HomeRequested:  return State::Done;
                // Reopening an already-active view is a no-op, not a reset: it happens
                // whenever the list pushes the view it is already showing.
                default:                    return from;
            }

        case State::NavigationQueued:
            switch (event)
            {
                case Event::QueuedNavSucceeded: return State::Active;
                // Stay on the article we never left. The history is untouched, so a
                // transient read error costs a message rather than the whole back stack.
                case Event::QueuedNavFailed:    return State::Active;
                default:                        return from;
            }

        case State::Done:
            switch (event)
            {
                case Event::Reopened: return State::Empty;
                default:              return from;
            }
    }

    return from;
}

bool is_finished(State state)
{
    return state == State::Done;
}

bool has_article(State state)
{
    return state == State::Active || state == State::NavigationQueued;
}

const char *to_string(State state)
{
    switch (state)
    {
        case State::Empty:            return "Empty";
        case State::Active:           return "Active";
        case State::NavigationQueued: return "NavigationQueued";
        case State::Done:             return "Done";
    }
    return "?";
}

}
