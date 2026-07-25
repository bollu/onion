#ifndef TOKEN_VIEW_H_
#define TOKEN_VIEW_H_

#include "reader/view.h"
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

    // Word-selection ("dictionary picker") mode. Entered with a shoulder button while
    // reading; a highlight moves word by word and A opens the selected word's meaning.
    void ws_enter();
    void ws_exit();
    void ws_handle_key(SDLKey key);
    // Move the highlight by whole words (dir -1/+1), crossing lines and scrolling as
    // needed. `land_last` picks which word to land on when stepping onto a new line.
    void ws_move_word(int dir);
    void ws_move_line(int dir);
    // Walk `dir` to the nearest line that has words, scrolling if needed but bounded to
    // ~one page so a press over a large image can't jump far; restores the scroll position
    // and returns false if no word is found within the budget. Updates ws_line on success.
    bool ws_walk(int dir);
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
    void set_title_progress(int percent);

    void set_on_scroll(std::function<void(DocAddr)> callback);

    // True while the word-selection highlight is active. The owning view should route all
    // input here (including A/B) while this holds, so the mode owns the keyboard.
    bool is_word_select_active() const;

    // Called with the selected surface form when the user asks for a word's meaning: A in
    // a book, X once link mode is on. The owner uses this to open the meaning popup. The
    // mode stays active.
    void set_on_open_word(std::function<void(const std::string &)> callback);

    // Link mode gives A to link following and moves the dictionary to X. Off by default,
    // so books keep A for the dictionary.
    void set_link_mode(bool enabled);

    // Called with a link target when A is pressed on a highlighted word that overlaps one.
    void set_on_follow_link(std::function<void(const std::string &)> callback);
};

#endif
