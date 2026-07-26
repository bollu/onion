#ifndef TOKEN_VIEW_H_
#define TOKEN_VIEW_H_

#include "reader/view.h"
#include "reader/word_preview.h"
#include "doc_api/doc_addr.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct DocReader;
struct SystemStyling;
struct TokenViewState;
struct TokenViewStyling;

class TokenView: public View
{
    std::unique_ptr<TokenViewState> state;

    void scroll(int num_lines);

    // The word cursor. There is no mode to enter: it exists from the moment the document
    // opens, and the page scrolls under it to keep it centred (see ws_camera.h).
    void ws_seed();
    void ws_handle_key(SDLKey key);
    // Scroll so the cursor returns to the middle row. Called after every cursor move.
    void ws_recentre();
    // Move the cursor half a page (dir -1/+1); the camera then follows.
    void ws_page(int dir);
    // Move the highlight by whole words (dir -1/+1), crossing lines and scrolling as
    // needed. `land_last` picks which word to land on when stepping onto a new line.
    void ws_move_word(int dir);
    void ws_move_line(int dir);
    // Walk `dir` to the nearest line that has words, scrolling if needed but bounded to
    // ~one page so a press over a large image can't jump far; restores the scroll position
    // and returns false if no word is found within the budget. Updates ws_line on success.
    bool ws_walk(int dir);
    // The word under the cursor, rejoined when a discretionary hyphen split it across two
    // lines. What the dictionary and the peek are given.
    std::string ws_selected_surface() const;
    // The text line and word span under the highlight; false if there is none.
    bool ws_selected_span(const struct TextLine **out_line, struct WordSpan *out_span) const;
    void ws_open_selected();
    void ws_follow_selected();
    // Scroll by `n` lines and report how many lines actually moved (0 at book ends).
    int scroll_reporting(int n);

public:
    TokenView(
        std::shared_ptr<DocReader> reader,
        DocAddr address,
        SystemStyling &sys_styling,
        TokenViewStyling &token_view_styling
    );
    virtual ~TokenView();

    bool render(SDL_Surface *dest_surface, bool force_render) override;
    bool is_done() override;
    void on_keypress(SDLKey key) override;
    void on_keyheld(SDLKey key, uint32_t held_time_ms) override;

    DocAddr get_address() const;
    void seek_to_address(DocAddr address);

    void set_title(const std::string &title);

    // Room the title actually has, once the progress percent and battery glyph on the right
    // have taken theirs. For callers that compose the title to fit -- the wiki breadcrumb
    // decides what to drop, and needs the same number render will crop to.
    int title_text_width() const;
    // Where the reader is, as two percentages: through the whole document, and through the
    // current chapter or section. Drawn as the two margin bars, not as text.
    void set_progress(int global_percent, int chapter_percent);

    // Replaces the title in the bottom bar. Empty leaves the title showing.
    void set_word_select_hint(const std::string &hint);

    void set_on_scroll(std::function<void(DocAddr)> callback);

    // Called with the word under the cursor when the reader asks for its meaning (A). The
    // owner uses this to open the meaning popup.
    void set_on_open_word(std::function<void(const std::string &)> callback);

    // Called with the word under the cursor to produce the two-zone peek shown in the
    // permanent panel above the text, so meanings can be skimmed without opening the full
    // popup. Invoked only when the cursor lands on a different word.
    void set_on_word_preview(std::function<WordPreview(const std::string &)> callback);

    // Asked for a fuzzy suggestion when a word resolves to nothing. Fire-and-forget: the
    // owner submits the work somewhere that can be interrupted, and calls the reply callback
    // if and when an answer arrives. The view never blocks on it -- this is the whole reason
    // it is a callback and not a lookup, since doing it inline froze the cursor.
    //
    // Only asked once the cursor has settled, so sweeping a line of proper nouns asks for
    // nothing.
    using SuggestReply = std::function<void(const std::string &surface, const std::string &)>;
    void set_on_word_unknown(std::function<void(const std::string &, SuggestReply)> callback);

    // Whether this document has links to follow. A always means "what does this mean"; X
    // follows a link, and exists only where there are links. Configuration rather than
    // state -- set once per document.
    void set_follows_links(bool follows);

    // Called with a link target when X is pressed on a word that overlaps one.
    void set_on_follow_link(std::function<void(const std::string &)> callback);

    // Called once when the cursor comes to rest on a link, before any button is pressed --
    // the owner's chance to have the target ready by the time X arrives. Advisory: the
    // reader may never press it, so whatever this triggers must be droppable and must not
    // cost a frame. Sweeping past links announces nothing.
    void set_on_link_dwell(std::function<void(const std::string &)> callback);

    // Claim L1/R1 (-1 is back, +1 forward). Unclaimed they nudge the page a few lines,
    // which is what a book wants; the wiki claims them to walk its breadcrumb. L2/R2 always
    // jump half a page and are not claimable.
    void set_on_shoulder_hop(std::function<void(int)> callback);
};

#endif
