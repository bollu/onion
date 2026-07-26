#ifndef READER_BOOTSTRAP_VIEW_H_
#define READER_BOOTSTRAP_VIEW_H_

#include "doc_api/doc_addr.h"
#include "reader/view.h"
#include "util/job.h"

struct ReaderBootstrapViewState;
struct SystemStyling;
struct TokenViewStyling;
struct ViewStack;
struct StateStore;

#include <filesystem>
#include <functional>
#include <memory>

// Temporary view to open a book and display loading/error message
class ReaderBootstrapView: public View
{
    std::unique_ptr<ReaderBootstrapViewState> state;

    void load_reader();

public:
    ReaderBootstrapView(
        std::filesystem::path book_path,
        SystemStyling &sys_styling,
        TokenViewStyling &token_view_styling,
        ViewStack &view_stack,
        StateStore &state_store,
        std::function<void(std::function<void()>)> async,
        // Where interruptible background work goes -- the fuzzy suggestion for a word the
        // lexicon does not know. Alongside `async` rather than folded into it: a task runs
        // to completion, a job yields, and the peek needs the second kind.
        std::function<void(std::unique_ptr<Job>)> submit_job,
        // Called on the main thread with the reading position, 0-100.
        std::function<void(int)> on_progress = {}
    );
    virtual ~ReaderBootstrapView();

    bool render(SDL_Surface *dest_surface, bool force_render) override;
    bool is_done() override;
    void on_keypress(SDLKey) override;
};

#endif
