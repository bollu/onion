#include "./article_view.h"

#include "wiki/breadcrumb.h"
#include "wiki/views/article_search_view.h"
#include "wiki/nav_history.h"
#include "wiki/nav_state.h"
#include "wiki/wiki_context.h"

#include "reader/config.h"
#include "reader/system_styling.h"
#include "reader/view_stack.h"
#include "reader/views/popup_view.h"
#include "reader/views/selection_menu.h"
#include "reader/views/word_meaning_view.h"
#include "reader/word_preview.h"
#include "reader/views/token_view/token_view.h"
#include "reader/views/token_view/token_view_styling.h"

#include "filetypes/zim/zim_article_reader.h"
#include "lexicon/lexicon_service.h"
#include "text/font.h"
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

    // The lifecycle, as one value rather than a scatter of flags. See wiki/nav_state.h.
    nav::State nav_state = nav::State::Empty;

    // Set alongside NavigationQueued and consumed once input has unwound. The cause is
    // kept rather than a "was this a back-step?" flag, so the queue says why it exists.
    std::string queued_path;
    DocAddr queued_address = 0;
    nav::Event queued_cause = nav::Event::LinkFollowed;

    std::function<void(const std::string &, DocAddr)> on_change;
    // Owned by main(), which holds the SettingsView singleton the stack expects.
    std::function<void()> on_open_settings;

    void step(nav::Event event) { nav_state = nav::transition(nav_state, event); }

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
    // Reopening after the view retired: the list pushes the same object again, so the
    // machine has to be told before it will accept an article. This is the transition the
    // bool had no way to express, which is why the reading list was a one-shot.
    if (nav::is_finished(state->nav_state))
    {
        state->step(nav::Event::Reopened);
    }

    // `path` may alias the target inside the TokenView destroyed below, so copy it before
    // anything is torn down.
    const std::string target = path;

    auto reader = state->context->open_article(target);
    if (reader == nullptr || !reader->has_content())
    {
        state->step(state->nav_state == nav::State::NavigationQueued
                        ? nav::Event::QueuedNavFailed
                        : nav::Event::OpenFailed);
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

    // An article has links, so X follows the one under the cursor. A means "what does this
    // word mean" here exactly as it does in a book.
    state->token_view->set_follows_links(true);

    // The peek panel gives the article the same word meanings a book has; it already owns
    // the lexicon for the popup, and never fed it before.
    state->token_view->set_on_word_preview([this](const std::string &surface) {
        return summarize_word(state->lexicon, surface);
    });

    state->token_view->set_on_open_word([this](const std::string &surface) {
        state->view_stack.push(std::make_shared<WordMeaningView>(
            surface, state->lexicon, state->sys_styling));
    });

    // Queue rather than navigate. This callback runs inside TokenView::ws_follow_selected,
    // and navigating here would destroy the very TokenView whose method is on the stack --
    // taking this std::function, and the string `link_target` refers to, with it.
    state->token_view->set_on_follow_link([this](const std::string &link_target) {
        queue_navigation(link_target, 0, nav::Event::LinkFollowed);
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

    state->step(state->nav_state == nav::State::NavigationQueued
                    ? nav::Event::QueuedNavSucceeded
                    : nav::Event::OpenSucceeded);
    return true;
}

void ArticleView::queue_navigation(const std::string &path, DocAddr address, nav::Event cause)
{
    state->queued_path = path;
    state->queued_address = address;
    state->queued_cause = cause;
    state->step(cause);
}

// Called once the key handler has unwound, so replacing the TokenView is safe.
void ArticleView::perform_queued_navigation()
{
    if (state->nav_state != nav::State::NavigationQueued)
    {
        return;
    }

    const std::string path = state->queued_path;
    const DocAddr address = state->queued_address;
    const bool going_back = state->queued_cause == nav::Event::BackToPrevious;
    state->queued_path.clear();

    // Going back must not record the article being left, or B would push what it just
    // popped and never unwind.
    if (navigate_to(path, address, !going_back))
    {
        if (going_back)
        {
            state->history.pop();
        }
        return;
    }

    // navigate_to has already returned the machine to Active, so the article we never
    // left stays on screen and, for a back-step, its history entry survives.
    state->view_stack.push(std::make_shared<PopupView>(
        "Voce non disponibile", SYSTEM_FONT, state->sys_styling));
}

bool ArticleView::go_back()
{
    if (!state->history.can_go_back())
    {
        return false;
    }

    // Peek, do not pop: a failed navigation must not consume the entry. It is dropped in
    // perform_queued_navigation only once the article has actually opened.
    const HistoryEntry &entry = state->history.peek();
    queue_navigation(entry.path, entry.address, nav::Event::BackToPrevious);
    return true;
}

void ArticleView::update_title(DocAddr address)
{
    if (state->token_view == nullptr || state->reader == nullptr)
    {
        return;
    }

    const auto &toc = state->reader->get_table_of_contents();
    const auto position = state->reader->get_toc_position(address);

    // The trail rather than the bare title: on a wiki, how you got here is context the
    // title alone loses, and B unwinds exactly this list.
    //
    // Measured against the title bar's own width through the same font it will be drawn
    // with, so the breadcrumb decides what fits rather than being cut afterwards.
    std::vector<std::string> trail;
    for (const auto &entry : state->history.entries())
    {
        trail.push_back(entry.title);
    }
    trail.push_back(state->title);


    text::Font *font = state->sys_styling.get_loaded_font();
    const int avail = state->token_view->title_text_width();
    auto width_of = [font](const std::string &s) {
        int w = 0;
        text::text_size(font, s.c_str(), &w, nullptr);
        return w;
    };

    std::string bar = wiki::breadcrumb(trail, avail, width_of);

    // The section you are in, appended only if it still fits. It cannot go into the trail:
    // breadcrumb() always keeps the last element and drops from the front, so a section at
    // the end would be the last thing dropped rather than the first -- which is how the
    // article's own title came to vanish, leaving only the section on screen. Where you are
    // in the archive matters more than where you are in the article.
    if (position.toc_index < toc.size())
    {
        const std::string &section = toc[position.toc_index].display_name;
        if (!section.empty() && section != state->title)
        {
            const std::string with_section = bar + " \xE2\x80\xBA " + section;  // " › "
            if (width_of(with_section) <= avail)
            {
                bar = with_section;
            }
        }
    }

    state->token_view->set_title(bar);

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
    // Built rather than fixed, because "Indice" is a dead end on lead-section archives:
    // those articles carry no headings, so the menu would offer an entry that can only
    // report that there is nothing to show.
    enum class MenuItem { Toc, Help, TitleBar, Settings, Home };

    std::vector<std::string> labels;
    std::vector<MenuItem> items;

    const bool has_toc = state->reader != nullptr &&
                         !state->reader->get_table_of_contents().empty();
    if (has_toc)
    {
        labels.push_back("Indice");
        items.push_back(MenuItem::Toc);
    }

    labels.push_back("Comandi");
    items.push_back(MenuItem::Help);

    labels.push_back(state->token_view_styling.get_show_title_bar() ? "Nascondi barra"
                                                                   : "Mostra barra");
    items.push_back(MenuItem::TitleBar);

    labels.push_back("Impostazioni");
    items.push_back(MenuItem::Settings);

    if (state->history.can_go_back())
    {
        labels.push_back("Torna all'elenco");
        items.push_back(MenuItem::Home);
    }

    auto menu = std::make_shared<SelectionMenu>(labels, state->sys_styling);
    menu->set_on_selection([this, items](uint32_t index) {
        if (index >= items.size())
        {
            return;
        }
        switch (items[index])
        {
            case MenuItem::Toc:
                open_toc_menu();
                break;
            case MenuItem::Help:
                state->view_stack.push(std::make_shared<PopupView>(
                    "A parole · A apri · X significato · B indietro · START elenco",
                    SYSTEM_FONT, state->sys_styling));
                break;
            case MenuItem::TitleBar:
                state->token_view_styling.set_show_title_bar(
                    !state->token_view_styling.get_show_title_bar());
                break;
            case MenuItem::Settings:
                if (state->on_open_settings)
                {
                    state->on_open_settings();
                }
                break;
            case MenuItem::Home:
                state->step(nav::Event::HomeRequested);
                break;
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
    // Also a backstop: a navigation queued from anywhere other than a key handler still
    // resolves before the next frame is drawn.
    perform_queued_navigation();

    if (!nav::has_article(state->nav_state) || state->token_view == nullptr)
    {
        return false;
    }
    return state->token_view->render(dest_surface, force_render);
}

bool ArticleView::is_done()
{
    return nav::is_finished(state->nav_state);
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
    if (!nav::has_article(state->nav_state) || state->token_view == nullptr)
    {
        return;
    }

    // The word cursor is always live, so there is no mode to route around. B and SELECT are
    // the article's own; everything else, A and X included, goes to the cursor.
    switch (key)
    {
        case SW_BTN_B:
            // Back through the history, then out to the reading list.
            if (!go_back())
            {
                state->step(nav::Event::BackExhausted);
            }
            break;
        case SW_BTN_Y:
        {
            // Find another article by name. Y is free here -- A is the meaning, X follows a
            // link -- and this is the one way to reach an article that nothing links to.
            auto search = std::make_shared<ArticleSearchView>(
                *state->context, state->sys_styling);
            search->set_on_open([this](const std::string &path) {
                // Queued, not immediate: this runs inside the search view's own key
                // handler, and navigating replaces the TokenView underneath it.
                queue_navigation(path, 0, nav::Event::LinkFollowed);
            });
            state->view_stack.push(search);
            break;
        }
        // START (return to the main menu) is handled at the top level in main.cpp. Back
        // to the reading list is B (go_back), which unwinds history then exits.
        case SW_BTN_SELECT:
            open_menu();
            break;
        default:
            state->token_view->on_keypress(key);
            break;
    }

    // Safe here and not before: every TokenView frame has returned, so replacing it
    // cannot pull the ground from under a live callback.
    perform_queued_navigation();
}

void ArticleView::on_keyheld(SDLKey key, uint32_t hold_time_ms)
{
    if (state->token_view != nullptr)
    {
        state->token_view->on_keyheld(key, hold_time_ms);
    }
    perform_queued_navigation();
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

void ArticleView::set_on_open_settings(std::function<void()> callback)
{
    state->on_open_settings = std::move(callback);
}
