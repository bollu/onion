#include "./article_view.h"

#include "wiki/nav_history.h"
#include "wiki/wiki_context.h"

#include "reader/config.h"
#include "reader/system_styling.h"
#include "reader/view_stack.h"
#include "reader/views/popup_view.h"
#include "reader/views/selection_menu.h"
#include "reader/views/word_meaning_view.h"
#include "reader/views/token_view/token_view.h"
#include "reader/views/token_view/token_view_styling.h"

#include "filetypes/zim/zim_article_reader.h"
#include "lexicon/lexicon_service.h"
#include "sys/keymap.h"

#include <utility>

struct ArticleViewState
{
    std::shared_ptr<WikiContext> context;
    SystemStyling &sys_styling;
    TokenViewStyling &token_view_styling;
    uint32_t token_view_styling_sub_id = 0;
    ViewStack &view_stack;

    // Opened once and shared by every meaning popup. A missing DB is simply !ok(), so the
    // dictionary degrades to an empty popup rather than a crash.
    lexicon::LexiconService lexicon;

    NavHistory history;

    std::shared_ptr<zim::ZimArticleReader> reader;
    std::unique_ptr<TokenView> token_view;

    std::string path;
    std::string title;

    bool is_done = false;
    std::function<void(const std::string &, DocAddr)> on_change;

    ArticleViewState(std::shared_ptr<WikiContext> context,
                     SystemStyling &sys_styling,
                     TokenViewStyling &token_view_styling,
                     ViewStack &view_stack)
        : context(std::move(context)),
          sys_styling(sys_styling),
          token_view_styling(token_view_styling),
          view_stack(view_stack),
          lexicon(LEXICON_DB_PATH)
    {
    }
};

ArticleView::ArticleView(std::shared_ptr<WikiContext> context,
                         const std::string &path,
                         DocAddr address,
                         SystemStyling &sys_styling,
                         TokenViewStyling &token_view_styling,
                         ViewStack &view_stack)
    : state(std::make_unique<ArticleViewState>(std::move(context), sys_styling,
                                               token_view_styling, view_stack))
{
    state->token_view_styling_sub_id = token_view_styling.subscribe_to_changes([this]() {
        update_title(current_address());
    });

    navigate_to(path, address, false);
}

ArticleView::~ArticleView()
{
    state->token_view_styling.unsubscribe_from_changes(state->token_view_styling_sub_id);
}

bool ArticleView::navigate_to(const std::string &path, DocAddr address, bool record_history)
{
    auto reader = state->context->open_article(path);
    if (reader == nullptr)
    {
        return false;
    }

    if (record_history && state->token_view != nullptr)
    {
        state->history.push(HistoryEntry{state->path, state->title, current_address()});
    }

    state->reader = reader;
    state->path = reader->path();
    state->title = reader->title();

    state->token_view = std::make_unique<TokenView>(
        reader, address, state->sys_styling, state->token_view_styling);

    // A follows links here; the dictionary moves to X. See TokenView::ws_handle_key.
    state->token_view->set_link_mode(true);

    state->token_view->set_on_open_word([this](const std::string &surface) {
        state->view_stack.push(std::make_shared<WordMeaningView>(
            surface, state->lexicon, state->sys_styling));
    });

    state->token_view->set_on_follow_link([this](const std::string &target) {
        if (!navigate_to(target))
        {
            state->view_stack.push(std::make_shared<PopupView>(
                "Voce non disponibile", SYSTEM_FONT, state->sys_styling));
        }
    });

    state->token_view->set_on_scroll([this](DocAddr at) {
        update_title(at);
        if (state->on_change)
        {
            state->on_change(state->path, at);
        }
    });

    update_title(address);
    if (state->on_change)
    {
        state->on_change(state->path, address);
    }

    return true;
}

bool ArticleView::go_back()
{
    if (!state->history.can_go_back())
    {
        return false;
    }

    const HistoryEntry entry = state->history.pop();
    return navigate_to(entry.path, entry.address, false);
}

void ArticleView::update_title(DocAddr address)
{
    if (state->token_view == nullptr || state->reader == nullptr)
    {
        return;
    }

    const auto &toc = state->reader->get_table_of_contents();
    const auto position = state->reader->get_toc_position(address);

    // The article title, not the section: on a wiki, which article you are in matters more
    // than which heading, and the section is one SELECT press away.
    state->token_view->set_title(state->title);

    const uint32_t percent =
        (state->token_view_styling.get_progress_reporting() == ProgressReporting::CHAPTER_PERCENT
         && position.toc_index < toc.size())
            ? position.progress_percent
            : state->reader->get_global_progress_percent(address);

    state->token_view->set_title_progress(static_cast<int>(percent));
}

void ArticleView::open_toc_menu()
{
    const auto &toc = state->reader->get_table_of_contents();
    if (toc.empty())
    {
        state->view_stack.push(std::make_shared<PopupView>(
            "Nessun indice", SYSTEM_FONT, state->sys_styling));
        return;
    }

    std::vector<std::string> names;
    names.reserve(toc.size());
    for (const auto &item : toc)
    {
        names.push_back(std::string(item.indent_level * 2, ' ') + item.display_name);
    }

    const auto current = state->reader->get_toc_position(current_address()).toc_index;

    auto menu = std::make_shared<SelectionMenu>(names, state->sys_styling);
    menu->set_on_selection([this](uint32_t index) {
        if (state->token_view != nullptr && state->reader != nullptr)
        {
            state->token_view->seek_to_address(state->reader->get_toc_item_address(index));
        }
    });
    menu->set_close_on_select();
    if (current < toc.size())
    {
        menu->set_cursor_pos(current);
    }
    menu->set_default_on_keypress([](SDLKey key, SelectionMenu &menu) {
        if (key == SW_BTN_SELECT)
        {
            menu.close();
        }
    });

    state->view_stack.push(menu);
}

void ArticleView::open_menu()
{
    const std::vector<std::string> entries = {"Indice", "Impostazioni"};

    auto menu = std::make_shared<SelectionMenu>(entries, state->sys_styling);
    menu->set_on_selection([this](uint32_t index) {
        if (index == 0)
        {
            open_toc_menu();
        }
    });
    menu->set_close_on_select();
    menu->set_default_on_keypress([](SDLKey key, SelectionMenu &menu) {
        if (key == SW_BTN_SELECT)
        {
            menu.close();
        }
    });

    state->view_stack.push(menu);
}

bool ArticleView::render(SDL_Surface *dest_surface, bool force_render)
{
    if (state->token_view == nullptr)
    {
        return false;
    }
    return state->token_view->render(dest_surface, force_render);
}

bool ArticleView::is_done()
{
    return state->is_done;
}

void ArticleView::on_focus()
{
    if (state->token_view != nullptr)
    {
        state->token_view->on_focus();
    }
}

void ArticleView::on_keypress(SDLKey key)
{
    if (state->token_view == nullptr)
    {
        return;
    }

    // Word select owns the keyboard while active, so A follows a link and X opens the
    // dictionary rather than reaching the handlers below.
    if (state->token_view->is_word_select_active())
    {
        state->token_view->on_keypress(key);
        return;
    }

    switch (key)
    {
        case SW_BTN_B:
            // Back through the history, then out of the app.
            if (!go_back())
            {
                state->is_done = true;
            }
            break;
        case SW_BTN_A:
            state->token_view_styling.set_show_title_bar(
                !state->token_view_styling.get_show_title_bar());
            break;
        case SW_BTN_SELECT:
            open_menu();
            break;
        default:
            state->token_view->on_keypress(key);
            break;
    }
}

void ArticleView::on_keyheld(SDLKey key, uint32_t hold_time_ms)
{
    if (state->token_view != nullptr)
    {
        state->token_view->on_keyheld(key, hold_time_ms);
    }
}

const std::string &ArticleView::current_path() const
{
    return state->path;
}

const std::string &ArticleView::current_title() const
{
    return state->title;
}

DocAddr ArticleView::current_address() const
{
    return state->token_view != nullptr ? state->token_view->get_address() : 0;
}

void ArticleView::set_on_change(std::function<void(const std::string &, DocAddr)> callback)
{
    state->on_change = std::move(callback);
}
