#ifndef WIKI_ARTICLE_SEARCH_VIEW_H_
#define WIKI_ARTICLE_SEARCH_VIEW_H_

#include "reader/view.h"
#include "wiki/article_search.h"

#include <functional>
#include <memory>
#include <string>

struct SystemStyling;
class WikiContext;

// Find an article by typing its name. Reached with Y from the reading list or from an
// article, and modelled on the dictionary app's search screen so the keyboard behaves the
// same way in both: the d-pad and A always drive the keyboard, and the shoulders move
// through the results, so there is no focus to switch and no cursor that goes missing.
class ArticleSearchView : public View
{
public:
    ArticleSearchView(WikiContext &context, SystemStyling &styling);
    virtual ~ArticleSearchView();

    bool render(SDL_Surface *dest, bool force_render) override;
    bool is_done() override;
    bool is_modal() override;
    void on_keypress(SDLKey key) override;
    void on_keyheld(SDLKey key, uint32_t held_time_ms) override;

    // Called with the chosen article's path. The view closes itself first, so the callback
    // is free to replace whatever pushed it.
    void set_on_open(std::function<void(const std::string &)> callback);

    // For the render harness: type a query without a keyboard.
    void set_query(const std::string &q);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif
