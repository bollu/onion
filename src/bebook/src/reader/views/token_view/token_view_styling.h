#ifndef TOKEN_VIEW_STYLING_H_
#define TOKEN_VIEW_STYLING_H_

#include "reader/progress_reporting.h"

#include <functional>
#include <memory>
#include <string>

struct TokenViewStylingState;

class TokenViewStyling
{
    std::unique_ptr<TokenViewStylingState> state;
    void notify_subscribers() const;

public:
    TokenViewStyling(bool justify, bool hyphenate);
    virtual ~TokenViewStyling();

    // The title bar is no longer optional and progress is no longer a choice: the bar
    // carries the breadcrumb, which is the reader's position in the archive, and the two
    // margin bars show book *and* chapter progress at once. Both settings existed to pick
    // one of two things to show; showing both made the question go away.

    // Justification. Changing either of these invalidates the laid-out lines, so the
    // view re-runs the paragraph breaker rather than just repainting.
    bool get_justify() const;
    void set_justify(bool justify);

    bool get_hyphenate() const;
    void set_hyphenate(bool hyphenate);

    // Subscribe to any changes
    uint32_t subscribe_to_changes(std::function<void()> callback);
    void unsubscribe_from_changes(uint32_t sub_id);
};

#endif
