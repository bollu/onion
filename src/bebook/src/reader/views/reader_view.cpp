#include "./reader_view.h"

#include "./selection_menu.h"
#include "./word_meaning_view.h"
#include "./token_view/token_view.h"
#include "./token_view/token_view_styling.h"

#include "lexicon/lexicon_service.h"
#include "reader/config.h"
#include "reader/system_styling.h"
#include "reader/view_stack.h"
#include "reader/word_preview.h"

#include "doc_api/doc_reader.h"
#include "sys/keymap.h"
#include "sys/screen.h"
#include "util/sdl_font_cache.h"

#include <iostream>
#include <memory>

struct ReaderViewState
{
    bool is_done = false;

    std::function<void(DocAddr)> on_change_address;

    std::string filename;
    std::shared_ptr<DocReader> reader;
    SystemStyling &sys_styling;
    TokenViewStyling &token_view_styling;
    uint32_t token_view_styling_sub_id;

    ViewStack &view_stack;

    // Opened once and shared by every meaning popup. Read-only; if the DB is missing it is
    // simply !ok() and lookups return empty, so a missing dictionary degrades to an empty
    // popup rather than a crash.
    lexicon::LexiconService lexicon;

    std::unique_ptr<TokenView> token_view;

    ReaderViewState(std::filesystem::path path, DocAddr seek_address, std::shared_ptr<DocReader> reader, SystemStyling &sys_styling, TokenViewStyling &token_view_styling, uint32_t token_view_styling_sub_id, ViewStack &view_stack)
        : filename(path.filename()),
          reader(reader),
          sys_styling(sys_styling),
          token_view_styling(token_view_styling),
          token_view_styling_sub_id(token_view_styling_sub_id),
          view_stack(view_stack),
          lexicon(LEXICON_DB_PATH),
          token_view(std::make_unique<TokenView>(
              reader,
              seek_address,
              sys_styling,
              token_view_styling
          ))
    {
    }

    ~ReaderViewState()
    {
    }
};

namespace
{

DocAddr get_current_address(const ReaderViewState &state)
{
    if (state.token_view)
    {
        return state.token_view->get_address();
    }

    return 0;
}

void open_toc_menu(ReaderView &reader_view, ReaderViewState &state)
{
    const auto &toc = state.reader->get_table_of_contents();
    if (toc.empty())
    {
        return;
    }

    // setup toc entries & callbacks
    std::vector<std::string> menu_names;
    for (const auto &toc_item: toc)
    {
        std::string indent(toc_item.indent_level * 2, ' ');
        menu_names.push_back(indent + toc_item.display_name);
    }

    auto current_toc_index = state.reader->get_toc_position(get_current_address(state)).toc_index;
    auto toc_select_menu = std::make_shared<SelectionMenu>(
        menu_names,
        state.sys_styling
    );
    toc_select_menu->set_on_selection([&reader_view, last_toc_index=current_toc_index](uint32_t toc_index) {
        if (toc_index != last_toc_index)
        {
            reader_view.seek_to_toc_index(toc_index);
        }
    });
    toc_select_menu->set_close_on_select();

    // select current toc item
    if (current_toc_index < toc.size())
    {
        toc_select_menu->set_cursor_pos(current_toc_index);
    }

    toc_select_menu->set_default_on_keypress([](SDLKey key, SelectionMenu &toc) {
        if (key == SW_BTN_SELECT)
        {
            toc.close();
        }
    });

    state.view_stack.push(toc_select_menu);
}

} // namespace

ReaderView::ReaderView(
    std::filesystem::path path,
    std::shared_ptr<DocReader> reader,
    DocAddr seek_address,
    SystemStyling &sys_styling,
    TokenViewStyling &token_view_styling,
    ViewStack &view_stack
) : state(std::make_unique<ReaderViewState>(
        path,
        seek_address,
        reader,
        sys_styling,
        token_view_styling,
        token_view_styling.subscribe_to_changes([this]() {
            update_token_view_title(get_current_address(*state));
        }),
        view_stack
    ))
{
    update_token_view_title(seek_address);

    // Update title info on scroll
    state->token_view->set_on_scroll([this](DocAddr address) {
        update_token_view_title(address);

        if (state->on_change_address)
        {
            state->on_change_address(address);
        }
    });

    // Open the dictionary popup when the word-select highlight is confirmed with A.
    state->token_view->set_on_open_word([this](const std::string &surface) {
        state->view_stack.push(std::make_shared<WordMeaningView>(
            surface, state->lexicon, state->sys_styling
        ));
    });

    // Feed the peek panel a meaning for the word under the cursor.
    state->token_view->set_on_word_preview([this](const std::string &surface) {
        return summarize_word(state->lexicon, surface);
    });
}

ReaderView::~ReaderView()
{
    state->token_view_styling.unsubscribe_from_changes(
        state->token_view_styling_sub_id
    );
}

void ReaderView::update_token_view_title(DocAddr address)
{
    const auto &toc = state->reader->get_table_of_contents();
    auto toc_position = state->reader->get_toc_position(address);

    if (toc_position.toc_index < toc.size())
    {
        state->token_view->set_title(toc[toc_position.toc_index].display_name);
    }
    else
    {
        state->token_view->set_title(state->filename);
    }

    uint32_t progress_percent = (
        state->token_view_styling.get_progress_reporting() == ProgressReporting::CHAPTER_PERCENT ?
        toc_position.progress_percent :
        state->reader->get_global_progress_percent(address)
    );
    state->token_view->set_title_progress(progress_percent);
}

bool ReaderView::render(SDL_Surface *dest_surface, bool force_render)
{
    return state->token_view->render(dest_surface, force_render);
}

bool ReaderView::is_done()
{
    return state->is_done;
}

void ReaderView::on_keypress(SDLKey key)
{
    // START (return to the main menu) is handled at the top level in main.cpp, so it is not
    // seen here. The word cursor is always live, so there is no mode to route around: keys
    // the book itself does not claim go straight to it.
    switch (key) {
        case SW_BTN_SELECT:
            open_toc_menu(*this, *state);
            break;
        case SW_BTN_B:
            // Idempotent: B only unwinds the dictionary popup. With nothing open there is
            // nothing to back out of, and it never leaves the book -- START does.
            break;
        default:
            state->token_view->on_keypress(key);
            break;
    }
}

void ReaderView::on_keyheld(SDLKey key, uint32_t hold_time_ms)
{
    state->token_view->on_keyheld(key, hold_time_ms);
}

void ReaderView::set_on_change_address(std::function<void(DocAddr)> callback)
{
    state->on_change_address = callback;
}

void ReaderView::seek_to_toc_index(uint32_t toc_index)
{
    auto address = state->reader->get_toc_item_address(toc_index);
    seek_to_address(address);
}

void ReaderView::seek_to_address(DocAddr address)
{
    if (state->token_view)
    {
        state->token_view->seek_to_address(address);
        update_token_view_title(address);

        if (state->on_change_address)
        {
            state->on_change_address(address);
        }
    }
}
