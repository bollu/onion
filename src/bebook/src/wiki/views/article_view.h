#ifndef ARTICLE_VIEW_H_
#define ARTICLE_VIEW_H_

#include "doc_api/doc_addr.h"
#include "reader/view.h"

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
    void set_on_change(std::function<void(const std::string &path, DocAddr)> callback);

private:
    std::unique_ptr<ArticleViewState> state;

    void update_title(DocAddr address);
    void open_menu();
    void open_toc_menu();
};

#endif
