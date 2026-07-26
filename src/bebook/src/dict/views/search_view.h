#ifndef DICT_SEARCH_VIEW_H_
#define DICT_SEARCH_VIEW_H_

#include "reader/view.h"
#include "lexicon/lexicon_service.h"
#include "util/throttled.h"

#include <string>
#include <vector>

struct SystemStyling;
struct ViewStack;

// The dictionary app's only screen: a search bar, a strip of the top few matches, an inline
// detail panel for the active one, and an on-screen ASCII keyboard.
//
// There is no focus to move. The keyboard always has the d-pad and A; L1/R1 cycle the match
// strip and the panel below follows. The previous design split focus between the keyboard
// and a results list, reachable only by pressing UP from the keyboard's top row and with no
// indicator of which pane was live -- so the list showed no cursor while typing even though
// one was selected.
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
    // Everything the panel draws for one match, resolved once when the match changes so
    // rendering is a pure read.
    struct Detail
    {
        std::string headline;                  // "io faccio → fare · presente"
        std::vector<std::string> senses;       // glosses, already numbered
        const lexicon::ConjTable *conj = nullptr;  // into `conj_tables`; null for non-verbs
        int person = -1;                       // highlighted person, or -1
        bool more = false;                     // is the full modal worth opening?
    };

    void type_char(char c);
    void backspace();
    void activate_key();
    void refresh_results();
    void rebuild_detail();
    void cycle_match(int dir);
    void open_selected();
    void move_key(int dr, int dc);

    const lexicon::LexiconService &lexicon;
    SystemStyling &styling;
    ViewStack &view_stack;
    uint32_t styling_sub_id;

    std::string query;
    std::vector<lexicon::SearchHit> results;
    int result_index = 0;

    // Kept alive because Detail::conj points into it.
    std::vector<lexicon::ConjTable> conj_tables;
    Detail detail;

    int kb_row = 0;
    int kb_col = 0;

    bool _needs_render = true;
    bool _is_done = false;

    Throttled nav_throttle;
    Throttled backspace_throttle;
};

#endif
