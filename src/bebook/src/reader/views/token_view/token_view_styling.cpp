#include "./token_view_styling.h"

#include <unordered_map>

struct TokenViewStylingState
{
    bool show_title_bar;
    ProgressReporting progress_reporting;
    bool justify;
    bool hyphenate;

    uint32_t next_subscriber_id = 1;
    std::unordered_map<uint32_t, std::function<void()>> subscribers;

    TokenViewStylingState(bool show_title_bar, ProgressReporting progress_reporting, bool justify, bool hyphenate)
        : show_title_bar(show_title_bar),
          progress_reporting(progress_reporting),
          justify(justify),
          hyphenate(hyphenate)
    {}
};

TokenViewStyling::TokenViewStyling(bool show_title_bar, ProgressReporting progress_reporting, bool justify, bool hyphenate)
    : state(std::make_unique<TokenViewStylingState>(show_title_bar, progress_reporting, justify, hyphenate))
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

bool TokenViewStyling::get_show_title_bar() const
{
    return state->show_title_bar;
}

void TokenViewStyling::set_show_title_bar(bool show_title_bar)
{
    if (state->show_title_bar != show_title_bar)
    {
        state->show_title_bar = show_title_bar;
        notify_subscribers();
    }
}

ProgressReporting TokenViewStyling::get_progress_reporting() const
{
    return state->progress_reporting;
}

void TokenViewStyling::set_progress_reporting(ProgressReporting progress_reporting)
{
    if (state->progress_reporting != progress_reporting)
    {
        state->progress_reporting = progress_reporting;
        notify_subscribers();
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
