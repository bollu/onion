#include "./article_view.h"

#include "wiki/nav_history.h"
#include "wiki/nav_state.h"
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

    // A follows links here; the dictionary moves to X. See TokenView::WordSelectKeys.
    state->token_view->set_word_select_keys(TokenView::WordSelectKeys::FollowOnA);

    // Shown in the bottom bar while a word is selected, which is the one moment the
    // bindings matter more than the article title.
    state->token_view->set_word_select_hint("A segui   X significato   B esci");

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

    // Word select owns the keyboard while active, so A follows a link and X opens the
    // dictionary rather than reaching the handlers below.
    if (state->token_view->is_word_select_active())
    {
        state->token_view->on_keypress(key);
    }
    else
    {
        switch (key)
        {
            case SW_BTN_B:
                // Back through the history, then out to the reading list.
                if (!go_back())
                {
                    state->step(nav::Event::BackExhausted);
                }
                break;
            case SW_BTN_A:
                // A is the primary action, so it starts word select -- the feature the
                // whole app is built around. It used to toggle the title bar, which on a
                // page full of underlined links reads as the app breaking.
                state->token_view->enter_word_select();
                break;
            case SW_BTN_START:
                // Out to the reading list in one press, rather than unwinding by hand.
                state->step(nav::Event::HomeRequested);
                break;
            case SW_BTN_SELECT:
                open_menu();
                break;
            default:
                state->token_view->on_keypress(key);
                break;
        }
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
