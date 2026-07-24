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

    void rebuild_tabs();
    // The active tab's body as centred paragraphs: one per gloss (numbered), one per
    // conjugation row ("io  amo"), an empty string as a blank separator. render() lays
    // each out with the shared text engine so long glosses wrap and hyphenate.
    std::vector<std::string> body_paragraphs() const;
    void move_tab(int dir);
    void scroll_body(int dir);

    const lexicon::LexiconService &lexicon;
    SystemStyling &styling;
    uint32_t styling_sub_id;

    std::string surface;
    std::vector<Analysis> analyses;
    int active_analysis = 0;   // index into analyses, or -1 when the word was not found
    std::vector<Tab> tabs;
    int active_tab = 0;
    int body_scroll = 0;       // first visible body row

    bool _is_done = false;
    bool _needs_render = true;

    Throttled scroll_throttle;
};

#endif
