#include "./token_view_styling.h"

#include <unordered_map>

struct TokenViewStylingState
{
    bool justify;
    bool hyphenate;

    uint32_t next_subscriber_id = 1;
    std::unordered_map<uint32_t, std::function<void()>> subscribers;

    TokenViewStylingState(bool justify, bool hyphenate)
        : justify(justify),
          hyphenate(hyphenate)
    {}
};

TokenViewStyling::TokenViewStyling(bool justify, bool hyphenate)
    : state(std::make_unique<TokenViewStylingState>(justify, hyphenate))
{
}

TokenViewStyling::~TokenViewStyling()
{
}

void TokenViewStyling::notify_subscribers() const
{
    for (auto &sub: state->subscribers)
    {
        sub.second();
    }
}

bool TokenViewStyling::get_justify() const
{
    return state->justify;
}

void TokenViewStyling::set_justify(bool justify)
{
    if (state->justify != justify)
    {
        state->justify = justify;
        notify_subscribers();
    }
}

bool TokenViewStyling::get_hyphenate() const
{
    return state->hyphenate;
}

void TokenViewStyling::set_hyphenate(bool hyphenate)
{
    if (state->hyphenate != hyphenate)
    {
        state->hyphenate = hyphenate;
        notify_subscribers();
    }
}

uint32_t TokenViewStyling::subscribe_to_changes(std::function<void()> callback)
{
    uint32_t sub_id = state->next_subscriber_id++;
    state->subscribers[sub_id] = callback;
    return sub_id;
}

void TokenViewStyling::unsubscribe_from_changes(uint32_t sub_id)
{
    state->subscribers.erase(sub_id);
}
