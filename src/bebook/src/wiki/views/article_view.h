#ifndef ARTICLE_VIEW_H_
#define ARTICLE_VIEW_H_

#include "doc_api/doc_addr.h"
#include "reader/view.h"
#include "util/sliced_job.h"
#include "reader/views/token_view/token_view.h"
#include "wiki/nav_state.h"

#include <functional>
#include <memory>
#include <string>

class SystemStyling;
class TokenViewStyling;
class ViewStack;
class WikiContext;
struct ArticleViewState;

// The reading surface. A fork of ReaderView rather than a parameterisation of it: that one
// hard-codes A to the title bar, B to closing the book and SELECT to the TOC, none of
// which is what a wiki wants.
//
// Exactly one of these lives on the view stack for the whole session; navigation swaps the
// TokenView in place rather than pushing, so browsing thirty articles does not pin thirty
// line buffers.
class ArticleView : public View
{
public:
    ArticleView(std::shared_ptr<WikiContext> context,
                const std::string &path,
                DocAddr address,
                SystemStyling &sys_styling,
                TokenViewStyling &token_view_styling,
                ViewStack &view_stack);
    ~ArticleView();

    bool render(SDL_Surface *dest_surface, bool force_render) override;
    bool is_done() override;
    void on_keypress(SDLKey key) override;
    void on_keyheld(SDLKey key, uint32_t hold_time_ms) override;
    void on_focus() override;

    // `record_history` is false when restoring, so going back does not push what we came
    // from and trap B in a two-article loop.
    bool navigate_to(const std::string &path, DocAddr address = 0, bool record_history = true);
    bool go_back();

    const std::string &current_path() const;
    const std::string &current_title() const;
    DocAddr current_address() const;

    // Fires on navigation and on scroll, for the Game Switcher tile and resume state.
    // Where background work goes. The view builds the job, because it owns the lexicon;
    // main() routes it, because it owns the frame budget. Neither has to know the other's
    // half.
    void set_job_submitter(std::function<void(std::unique_ptr<SlicedJob>)> callback);

    void set_on_change(std::function<void(const std::string &path, DocAddr)> callback);

    // Opens the settings screen. main() owns the SettingsView, which is a long-lived
    // singleton rather than something this view can construct.
    void set_on_open_settings(std::function<void()> callback);

private:
    std::unique_ptr<ArticleViewState> state;

    // Records where to go and moves the machine to NavigationQueued. The move itself waits
    // for perform_queued_navigation, because following a link is reported from inside the
    // TokenView that the move destroys.
    void queue_navigation(const std::string &path, DocAddr address, nav::Event cause,
                          bool is_forward = false);

    // Retrace a step undone by B. False when there is nothing to go forward to.
    bool go_forward();
    void perform_queued_navigation();

    void update_title(DocAddr address);
    void open_menu();
    void open_toc_menu();
};

#endif
