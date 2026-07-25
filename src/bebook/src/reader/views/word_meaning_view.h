#ifndef WORD_MEANING_VIEW_H_
#define WORD_MEANING_VIEW_H_

#include "reader/view.h"
#include "lexicon/lexicon_service.h"
#include "util/throttled.h"

#include <memory>
#include <string>
#include <vector>

struct SystemStyling;

// Modal dictionary popup for a single selected word. Built from a surface form and a
// LexiconService: resolves the form to its lemma(s), gathers It->En / It->It senses and
// (for verbs) conjugation tables, and presents them as tabs. Pushed on the view stack by
// ReaderView when the reader's word-select mode fires its open callback.
//
//   surface -> lemma            (header)
//   verb . imperfect . io       (morphology of the active analysis)
//   <-  Imperfetto  ->  (4/7)   (tab cycler: It->En, It->It, then one per tense)
//   ...body for the active tab, scrollable...
//   L/R tabs   up/down scroll   B back
//
// A form can be ambiguous (e.g. "sono" = essere 1sg-pres and 3pl-pres); X cycles the
// active analysis. B closes and returns to word-select mode.
class WordMeaningView : public View
{
public:
    WordMeaningView(
        const std::string &surface,
        const lexicon::LexiconService &lexicon,
        SystemStyling &styling
    );
    virtual ~WordMeaningView();

    bool render(SDL_Surface *dest, bool force_render) override;
    bool is_done() override;
    bool is_modal() override;
    void on_keypress(SDLKey key) override;
    void on_keyheld(SDLKey key, uint32_t held_time_ms) override;

private:
    enum class TabKind { ItEn, ItIt, Conj };
    struct Tab { TabKind kind; std::string title; int conj_index; };

    struct Analysis
    {
        lexicon::LemmaEntry lemma;
        std::vector<lexicon::Sense> it_en;
        std::vector<lexicon::Sense> it_it;
        std::vector<lexicon::ConjTable> conj;
    };

    // (Re)point the popup at `surface`: gather analyses + tabs, or, if nothing is found,
    // fuzzy suggestions. Used both at construction and when a suggestion is picked.
    void load(const std::string &surface);
    void rebuild_tabs();

    // One block of the active tab's body. A `prose` item is a paragraph (a numbered gloss or
    // a message) that render() wraps + hyphenates and left-aligns. A `conjugation` item is a
    // (pronoun, form) pair drawn as two aligned columns. A `blank` item is a separator line.
    struct BodyItem
    {
        enum class Kind { Prose, Conjugation, Blank } kind;
        std::string a;  // prose text, or the pronoun
        std::string b;  // the conjugated form
    };
    std::vector<BodyItem> body_items() const;

    void move_tab(int dir);
    void scroll_body(int dir);

    // Held so a picked fuzzy suggestion can be looked up (load()) without leaving the popup.
    // The owner (ReaderView) keeps the service for the popup's lifetime.
    const lexicon::LexiconService &lexicon;
    SystemStyling &styling;
    uint32_t styling_sub_id;

    std::string surface;
    std::vector<Analysis> analyses;
    int active_analysis = 0;   // index into analyses, or -1 when the word was not found
    std::vector<Tab> tabs;
    int active_tab = 0;
    int body_scroll = 0;       // first visible body row

    // When the word is unknown: closest known lemmas ("Forse cercavi:"), and the cursor over
    // them. A on a suggestion re-points the popup at it via load().
    std::vector<std::string> suggestions;
    int suggestion_index = 0;

    bool _is_done = false;
    bool _needs_render = true;

    Throttled scroll_throttle;
};

#endif
