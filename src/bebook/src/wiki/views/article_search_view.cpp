#include "./article_search_view.h"

#include "wiki/wiki_context.h"

#include "reader/config.h"
#include "reader/ui_chrome.h"
#include "util/sdl_font_cache.h"
#include "reader/system_styling.h"

#include "filetypes/zim/zim_file.h"

#include "sys/keymap.h"
#include "sys/screen.h"

#include "text/font.h"

#include "util/sdl_utils.h"
#include "util/throttled.h"

#include <algorithm>
#include <vector>

namespace
{



const int MARGIN_X = 16;
const int MAX_RESULTS = 5;

}

struct ArticleSearchView::Impl
{
    WikiContext &context;
    SystemStyling &styling;
    uint32_t styling_sub_id = 0;

    std::string query;
    std::vector<wiki::ArticleHit> results;
    int selected = 0;

    int kb_row = 0;
    int kb_col = 0;

    bool needs_render = true;
    bool done = false;

    std::function<void(const std::string &)> on_open;

    Throttled nav_throttle{140, 50};
    Throttled backspace_throttle{200, 50};

    Impl(WikiContext &context, SystemStyling &styling)
        : context(context), styling(styling)
    {
    }

    void refresh()
    {
        results.clear();
        selected = 0;
        zim::ZimFile *zim = context.zim_file();
        if (zim != nullptr && !query.empty())
        {
            results = wiki::search_titles(*zim, query, MAX_RESULTS);
        }
        needs_render = true;
    }
};

ArticleSearchView::ArticleSearchView(WikiContext &context, SystemStyling &styling)
    : impl(new Impl(context, styling))
{
    impl->styling_sub_id = styling.subscribe_to_changes([this](SystemStyling::ChangeId) {
        impl->needs_render = true;
    });
}

ArticleSearchView::~ArticleSearchView()
{
    impl->styling.unsubscribe_from_changes(impl->styling_sub_id);
}

bool ArticleSearchView::is_done()
{
    return impl->done;
}

bool ArticleSearchView::is_modal()
{
    return true;
}

void ArticleSearchView::set_on_open(std::function<void(const std::string &)> callback)
{
    impl->on_open = std::move(callback);
}

void ArticleSearchView::set_query(const std::string &q)
{
    impl->query = q;
    impl->refresh();
}

void ArticleSearchView::on_keypress(SDLKey key)
{
    const auto &kb = ui::keyboard_rows();
    switch (key)
    {
        case SW_BTN_UP:
        case SW_BTN_DOWN:
        {
            const int rows = static_cast<int>(kb.size());
            const int dr = (key == SW_BTN_UP) ? -1 : 1;
            impl->kb_row = ((impl->kb_row + dr) % rows + rows) % rows;
            impl->kb_col = std::min(impl->kb_col, static_cast<int>(kb[impl->kb_row].size()) - 1);
            impl->needs_render = true;
            break;
        }
        case SW_BTN_LEFT:
        case SW_BTN_RIGHT:
        {
            const int cols = static_cast<int>(kb[impl->kb_row].size());
            const int dc = (key == SW_BTN_LEFT) ? -1 : 1;
            impl->kb_col = ((impl->kb_col + dc) % cols + cols) % cols;
            impl->needs_render = true;
            break;
        }
        case SW_BTN_A:
            impl->query += kb[impl->kb_row][impl->kb_col];
            impl->refresh();
            break;
        case SW_BTN_B:
            if (!impl->query.empty())
            {
                impl->query.pop_back();
                impl->refresh();
            }
            break;
        // The shoulders move through the results, exactly as they do in the dictionary.
        case SW_BTN_L1:
        case SW_BTN_L2:
        case SW_BTN_R1:
        case SW_BTN_R2:
            if (!impl->results.empty())
            {
                const int n = static_cast<int>(impl->results.size());
                const int d = (key == SW_BTN_L1 || key == SW_BTN_L2) ? -1 : 1;
                impl->selected = ((impl->selected + d) % n + n) % n;
                impl->needs_render = true;
            }
            break;
        case SW_BTN_X:
            if (!impl->results.empty() && impl->on_open)
            {
                const std::string path = impl->results[impl->selected].path;
                // Close first: the callback replaces the article underneath, and a view
                // that is still on the stack when that happens is a view being destroyed
                // from inside its own handler.
                impl->done = true;
                impl->on_open(path);
            }
            break;
        // Y opened this screen from the article, so Y closes it again. A button that only
        // goes one way makes the reader hunt for the way back.
        case SW_BTN_Y:
        case SW_BTN_START:
            impl->done = true;
            break;
        default:
            break;
    }
}

void ArticleSearchView::on_keyheld(SDLKey key, uint32_t held_time_ms)
{
    switch (key)
    {
        case SW_BTN_UP:
        case SW_BTN_DOWN:
        case SW_BTN_LEFT:
        case SW_BTN_RIGHT:
            if (impl->nav_throttle(held_time_ms)) { on_keypress(key); }
            break;
        case SW_BTN_B:
            if (impl->backspace_throttle(held_time_ms)) { on_keypress(key); }
            break;
        default:
            break;
    }
}

bool ArticleSearchView::render(SDL_Surface *dest, bool force_render)
{
    if (!impl->needs_render && !force_render)
    {
        return false;
    }
    impl->needs_render = false;

    text::Font *font = impl->styling.get_loaded_font();
    const auto &theme = impl->styling.get_loaded_color_theme();
    const int line_h = text::font_line_height(font);

    auto mapc = [&](const SDL_Color &c) { return SDL_MapRGB(dest->format, c.r, c.g, c.b); };

    SDL_Rect all = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
    SDL_FillRect(dest, &all, mapc(theme.background));

    const int cx0 = MARGIN_X;
    const int cx1 = SCREEN_WIDTH - MARGIN_X;
    int y = 12;

    // Query, with a caret so an empty box still reads as somewhere to type.
    {
        const int w = ui::blit_text(dest, font, impl->query, cx0, y,
                           impl->query.empty() ? theme.secondary_text : theme.main_text,
                           theme.background);
        SDL_Rect caret = { static_cast<Sint16>(cx0 + w + 2), static_cast<Sint16>(y + 4),
                           2, static_cast<Uint16>(line_h - 8) };
        SDL_FillRect(dest, &caret, mapc(theme.main_text));
        y += line_h + 4;
    }

    SDL_Rect rule = { static_cast<Sint16>(cx0), static_cast<Sint16>(y),
                      static_cast<Uint16>(cx1 - cx0), 1 };
    SDL_FillRect(dest, &rule, mapc(theme.secondary_text));
    y += 1 + 8;

    // Results. The selected one is filled rather than merely coloured, because on some
    // themes the highlight text colour equals the main one and would mark nothing.
    if (impl->results.empty())
    {
        ui::blit_text(dest, font, impl->query.empty() ? "Cerca un articolo" : "Nessun risultato",
             cx0, y, theme.secondary_text, theme.background);
    }
    else
    {
        for (int i = 0; i < static_cast<int>(impl->results.size()); ++i)
        {
            const bool sel = (i == impl->selected);
            if (sel)
            {
                SDL_Rect hl = { static_cast<Sint16>(cx0 - 4), static_cast<Sint16>(y - 1),
                                static_cast<Uint16>(cx1 - cx0 + 8),
                                static_cast<Uint16>(line_h + 2) };
                SDL_FillRect(dest, &hl, mapc(theme.highlight_background));
            }
            ui::blit_text(dest, font,
                 text::elide_to_width(font, impl->results[i].title, cx1 - cx0),
                 cx0, y,
                 sel ? theme.highlight_text : theme.main_text,
                 sel ? theme.highlight_background : theme.background);
            y += line_h;
        }
    }

    // Keyboard, anchored to the bottom so it does not move as results come and go.
    // Keyboard and key hints come from reader/ui_chrome.h, so this screen and the
    // dictionary's cannot drift apart -- they are used minutes apart.
    const ui::KeyboardBox kb_box = ui::keyboard_box(cx0, cx1, line_h, 8);

    ui::draw_key_hint(dest, "A scrivi  B canc.  L/R risultati  X apri  Y torna",
                      cx0, cx1, kb_box.top - line_h,
                      impl->styling.get_font_size(), font, theme);

    ui::draw_keyboard(dest, kb_box, impl->kb_row, impl->kb_col,
                      impl->styling.get_font_size(), font, theme);

    return true;
}
