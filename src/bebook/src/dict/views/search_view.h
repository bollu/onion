#ifndef DICT_SEARCH_VIEW_H_
#define DICT_SEARCH_VIEW_H_

#include "reader/view.h"
#include "lexicon/lexicon_service.h"
#include "util/throttled.h"

#include <string>
#include <vector>

struct SystemStyling;
struct ViewStack;

// The dictionary app's root: an on-screen ASCII keyboard, a search bar, and a live results
// list. Typing (accent-insensitively) searches as you go; a result opens the shared
// WordMeaningView. D-pad up from the keyboard's top row moves into the results; A there opens
// the selected word; B leaves the results (or exits the app from the keyboard).
class SearchView : public View
{
public:
    SearchView(
        const lexicon::LexiconService &lexicon,
        SystemStyling &styling,
        ViewStack &view_stack
    );
    virtual ~SearchView();

    bool render(SDL_Surface *dest, bool force_render) override;
    bool is_done() override;
    void on_keypress(SDLKey key) override;
    void on_keyheld(SDLKey key, uint32_t held_time_ms) override;

    // Set the query programmatically (deep-link / render harness); refreshes the results.
    void set_query(const std::string &q);

private:
    enum class Focus { Keyboard, Results };

    void type_char(char c);
    void backspace();
    void activate_key();
    void refresh_results();
    void open_selected();
    void move_key(int dr, int dc);

    const lexicon::LexiconService &lexicon;
    SystemStyling &styling;
    ViewStack &view_stack;
    uint32_t styling_sub_id;

    std::string query;
    std::vector<lexicon::SearchHit> results;
    int result_index = 0;
    int result_scroll = 0;

    Focus focus = Focus::Keyboard;
    int kb_row = 0;
    int kb_col = 0;

    bool _needs_render = true;
    bool _is_done = false;

    Throttled nav_throttle;
    Throttled backspace_throttle;
};

#endif
