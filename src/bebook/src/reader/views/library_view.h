#ifndef LIBRARY_VIEW_H_
#define LIBRARY_VIEW_H_

#include "reader/view.h"

#include <filesystem>
#include <functional>
#include <memory>

class LibraryIndex;
struct SystemStyling;

struct LibraryViewState;

// The home screen: a shelf of recently read books above a grid of everything else,
// both shown by cover.
//
// Indexing a book means opening its zip and decoding a JPEG, which is far too slow to
// do for a whole shelf inside a frame. So the view only ever paints what the index
// already knows, and hands one book at a time to the supplied async runner; each
// completion marks the view dirty and the covers fill in progressively. A cold first
// run therefore shows titles immediately and pictures shortly after, rather than
// blocking on a spinner.
class LibraryView : public View
{
    std::unique_ptr<LibraryViewState> state;

public:
    LibraryView(
        LibraryIndex &index,
        SystemStyling &styling,
        std::function<void(std::function<void()>)> async
    );
    virtual ~LibraryView();

    void set_on_book_selected(std::function<void(const std::filesystem::path &)> callback);

    // Invoked when the user asks for the plain file browser, which remains the way to
    // reach books outside the library directory.
    void set_on_browse_requested(std::function<void()> callback);

    bool render(SDL_Surface *dest, bool force_render) override;
    bool is_done() override;
    void on_keypress(SDLKey key) override;
    void on_keyheld(SDLKey key, uint32_t held_time_ms) override;
    void on_focus() override;
};

#endif
