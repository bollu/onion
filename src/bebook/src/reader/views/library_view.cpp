#include "./library_view.h"

#include "reader/config.h"
#include "reader/library_index.h"
#include "reader/system_styling.h"
#include "sys/keymap.h"
#include "sys/screen.h"
#include "text/font.h"
#include "text/styled_text.h"
#include "util/sdl_font_cache.h"
#include "util/sdl_image_cache.h"
#include "util/sdl_utils.h"
#include "util/throttled.h"

#include "extern/rotozoom/SDL_rotozoom.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace
{

constexpr int SHELF_COVER_H = 132;
constexpr int GRID_COVER_H = 116;
constexpr int GRID_COLUMNS = 4;
constexpr int PADDING = 16;

// Books with no usable cover still need a recognisable shape on the shelf, so they get a
// plate of the right proportions with the title set into it. 2:3 is the common trade
// paperback ratio and what most epub covers approximate.
constexpr float COVER_ASPECT = 2.0f / 3.0f;

int cover_width_for(int height)
{
    return static_cast<int>(height * COVER_ASPECT + 0.5f);
}

} // namespace

struct LibraryViewState
{
    LibraryIndex &index;
    SystemStyling &styling;
    const uint32_t styling_sub_id;
    std::function<void(std::function<void()>)> async;

    std::function<void(const std::filesystem::path &)> on_book_selected;
    std::function<void()> on_browse_requested;

    bool needs_render = true;
    bool is_done = false;

    // Flat ordering of what is on screen: the recents shelf first, then the grid.
    std::vector<LibraryEntry> shelf;
    std::vector<LibraryEntry> grid;

    size_t cursor = 0;
    int grid_scroll_row = 0;

    // Decoded covers, keyed by file path. Bounded by the shared image cache rather than
    // grown without limit, since a large library would otherwise hold every cover.
    SDLImageCache covers;

    // Paths currently being indexed, so a slow book is not queued twice.
    std::set<std::string> indexing;
    size_t outstanding = 0;

    Throttled scroll_throttle;

    LibraryViewState(
        LibraryIndex &index,
        SystemStyling &styling,
        std::function<void(std::function<void()>)> async
    )
        : index(index),
          styling(styling),
          styling_sub_id(styling.subscribe_to_changes([this](SystemStyling::ChangeId) {
              needs_render = true;
          })),
          async(std::move(async)),
          scroll_throttle(250, 100)
    {
    }

    ~LibraryViewState()
    {
        styling.unsubscribe_from_changes(styling_sub_id);
    }

    size_t total_items() const { return shelf.size() + grid.size(); }

    const LibraryEntry *item_at(size_t i) const
    {
        if (i < shelf.size())
        {
            return &shelf[i];
        }
        i -= shelf.size();
        return i < grid.size() ? &grid[i] : nullptr;
    }

    void refresh_entries()
    {
        shelf = index.recents(GRID_COLUMNS);

        grid.clear();
        for (const auto &entry : index.entries())
        {
            const bool on_shelf = std::any_of(
                shelf.begin(), shelf.end(),
                [&](const LibraryEntry &s) { return s.path == entry.path; }
            );
            if (!on_shelf)
            {
                grid.push_back(entry);
            }
        }

        // Alphabetical by title, which is what a reader scanning a shelf expects; the
        // recents row already covers "what was I reading".
        std::sort(grid.begin(), grid.end(), [](const LibraryEntry &a, const LibraryEntry &b) {
            return a.title < b.title;
        });

        cursor = std::min(cursor, total_items() ? total_items() - 1 : 0);
        needs_render = true;
    }

    // Queues at most one book per call, so a cold library indexes steadily in the
    // background instead of flooding the task queue with hundreds of zip opens.
    void pump_indexing()
    {
        if (outstanding > 0 || !async)
        {
            return;
        }

        for (const auto &path : index.stale_paths())
        {
            if (indexing.count(path.string()))
            {
                continue;
            }

            indexing.insert(path.string());
            ++outstanding;

            async([this, path]() {
                index.index_one(path);
                --outstanding;
                refresh_entries();
            });
            return;
        }
    }

    SDL_Surface *cover_for(const LibraryEntry &entry, int target_h)
    {
        const std::string key = entry.path.string() + "@" + std::to_string(target_h);
        if (SDL_Surface *cached = covers.get_image(key))
        {
            return cached;
        }

        if (!entry.has_cover)
        {
            return nullptr;
        }

        const auto path = index.cover_path(entry);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            return nullptr;
        }

        surface_unique_ptr loaded{SDL_LoadBMP(path.string().c_str())};
        if (!loaded)
        {
            // Covers are cached as PNG, which SDL cannot read; go through the same
            // decoder the reader uses for inline images.
            FILE *fp = fopen(path.string().c_str(), "rb");
            if (!fp)
            {
                return nullptr;
            }
            std::vector<char> data;
            fseek(fp, 0, SEEK_END);
            data.resize(static_cast<size_t>(ftell(fp)));
            fseek(fp, 0, SEEK_SET);
            if (fread(data.data(), 1, data.size(), fp) != data.size())
            {
                fclose(fp);
                return nullptr;
            }
            fclose(fp);

            loaded = load_surface_from_ptr(
                data.data(), static_cast<uint32_t>(data.size()), "png", get_render_surface_format()
            );
        }

        if (!loaded)
        {
            return nullptr;
        }

        if (loaded->h != target_h)
        {
            const double scale = static_cast<double>(target_h) / loaded->h;
            surface_unique_ptr scaled{zoomSurface(loaded.get(), scale, scale, 1)};
            if (scaled)
            {
                loaded = std::move(scaled);
            }
        }

        covers.put_image(key, std::move(loaded));
        return covers.get_image(key);
    }
};

LibraryView::LibraryView(
    LibraryIndex &index,
    SystemStyling &styling,
    std::function<void(std::function<void()>)> async
) : state(std::make_unique<LibraryViewState>(index, styling, std::move(async)))
{
    state->refresh_entries();
}

LibraryView::~LibraryView()
{
}

void LibraryView::set_on_book_selected(std::function<void(const std::filesystem::path &)> callback)
{
    state->on_book_selected = std::move(callback);
}

void LibraryView::set_on_browse_requested(std::function<void()> callback)
{
    state->on_browse_requested = std::move(callback);
}

namespace
{

void draw_rect(SDL_Surface *dest, int x, int y, int w, int h, SDL_Color color)
{
    SDL_Rect rect = {
        static_cast<Sint16>(x), static_cast<Sint16>(y),
        static_cast<Uint16>(std::max(0, w)), static_cast<Uint16>(std::max(0, h))
    };
    SDL_FillRect(dest, &rect, SDL_MapRGB(dest->format, color.r, color.g, color.b));
}

// Truncates to fit, appending an ellipsis. Measured with the shaper, so the result is
// correct for proportional faces rather than assuming a character width.
std::string elide(const text::Font *font, const std::string &s, int max_width)
{
    if (text::fixed_round(text::text_width(font, s.c_str(), static_cast<uint32_t>(s.size()))) <= max_width)
    {
        return s;
    }

    const std::string ellipsis = "\xE2\x80\xA6";
    std::string out;
    for (size_t i = 0; i < s.size();)
    {
        // Advance a whole UTF-8 sequence so a multi-byte character is never split.
        size_t next = i + 1;
        while (next < s.size() && (s[next] & 0xC0) == 0x80)
        {
            ++next;
        }

        const std::string candidate = s.substr(0, next) + ellipsis;
        if (text::fixed_round(text::text_width(font, candidate.c_str(), static_cast<uint32_t>(candidate.size()))) > max_width)
        {
            break;
        }
        out = candidate;
        i = next;
    }
    return out.empty() ? ellipsis : out;
}

void draw_cover(
    SDL_Surface *dest,
    LibraryViewState &state,
    const LibraryEntry &entry,
    int x,
    int y,
    int height,
    bool selected,
    const ColorTheme &theme,
    text::Font *label_font
)
{
    const int width = cover_width_for(height);

    if (selected)
    {
        draw_rect(dest, x - 3, y - 3, width + 6, height + 6, theme.highlight_background);
    }

    SDL_Surface *cover = state.cover_for(entry, height);
    if (cover)
    {
        SDL_Rect dst = {static_cast<Sint16>(x + (width - cover->w) / 2), static_cast<Sint16>(y), 0, 0};
        SDL_BlitSurface(cover, nullptr, dest, &dst);
    }
    else
    {
        // Placeholder plate with the title set into it, so an unindexed or coverless
        // book is still identifiable and the grid does not reflow once covers arrive.
        draw_rect(dest, x, y, width, height, theme.secondary_text);
        draw_rect(dest, x + 1, y + 1, width - 2, height - 2, theme.background);

        const int inner = width - 12;
        int text_y = y + 14 + text::font_ascent(label_font);
        std::string remaining = entry.title;
        while (!remaining.empty() && text_y < y + height - 6)
        {
            std::string line = elide(label_font, remaining, inner);
            text::draw_text(
                dest, label_font, line.c_str(), x + 6, text_y,
                theme.secondary_text, theme.background
            );
            // The ellipsis means the rest cannot be shown; one plate holds one line.
            break;
        }
    }

    // Progress bar along the foot of the cover, the quickest read of "where am I".
    if (entry.progress_percent > 0)
    {
        const int bar_h = 3;
        draw_rect(dest, x, y + height - bar_h, width, bar_h, theme.background);
        draw_rect(
            dest, x, y + height - bar_h,
            width * std::min(entry.progress_percent, 100) / 100, bar_h,
            theme.highlight_background
        );
    }
}

} // namespace

bool LibraryView::render(SDL_Surface *dest, bool force_render)
{
    state->pump_indexing();

    if (!state->needs_render && !force_render)
    {
        return false;
    }
    state->needs_render = false;

    const auto &theme = state->styling.get_loaded_color_theme();
    text::Font *font = state->styling.get_loaded_font();
    text::Font *small = cached_load_font(
        state->styling.get_font_name(),
        std::max<uint32_t>(MIN_FONT_SIZE, state->styling.get_font_size() * 3 / 4),
        FontLoadErrorOpt::NoThrow
    );
    if (!small)
    {
        small = font;
    }

    draw_rect(dest, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, theme.background);

    const int line_h = text::font_line_height(small);
    int y = PADDING;

    if (state->total_items() == 0)
    {
        text::draw_text(
            dest, font, "No books found", PADDING, y + text::font_ascent(font),
            theme.secondary_text, theme.background
        );
        const std::string where = std::filesystem::path(DEFAULT_BROWSE_PATH).string();
        text::draw_text(
            dest, small, where.c_str(), PADDING, y + line_h + text::font_line_height(font),
            theme.secondary_text, theme.background
        );
        return true;
    }

    // Recents shelf
    if (!state->shelf.empty())
    {
        text::draw_text(
            dest, small, "Recently read", PADDING, y + text::font_ascent(small),
            theme.secondary_text, theme.background
        );
        y += line_h + 6;

        int x = PADDING;
        for (size_t i = 0; i < state->shelf.size(); ++i)
        {
            draw_cover(
                dest, *state, state->shelf[i], x, y, SHELF_COVER_H,
                state->cursor == i, theme, small
            );
            x += cover_width_for(SHELF_COVER_H) + PADDING;
        }
        y += SHELF_COVER_H + PADDING;
    }

    // Library grid
    if (!state->grid.empty())
    {
        text::draw_text(
            dest, small, "Library", PADDING, y + text::font_ascent(small),
            theme.secondary_text, theme.background
        );
        y += line_h + 6;

        const int cell_w = cover_width_for(GRID_COVER_H) + PADDING;
        const int cell_h = GRID_COVER_H + PADDING;
        const int visible_rows = std::max(1, (SCREEN_HEIGHT - y - line_h) / cell_h);

        for (int row = 0; row < visible_rows; ++row)
        {
            const int data_row = state->grid_scroll_row + row;
            for (int col = 0; col < GRID_COLUMNS; ++col)
            {
                const size_t idx = static_cast<size_t>(data_row) * GRID_COLUMNS + col;
                if (idx >= state->grid.size())
                {
                    break;
                }
                draw_cover(
                    dest, *state, state->grid[idx],
                    PADDING + col * cell_w, y + row * cell_h, GRID_COVER_H,
                    state->cursor == state->shelf.size() + idx, theme, small
                );
            }
        }
    }

    // Footer: the selected book's title and author, which the covers alone cannot carry
    // at this size.
    if (const LibraryEntry *selected = state->item_at(state->cursor))
    {
        const int footer_y = SCREEN_HEIGHT - line_h - 4;
        draw_rect(dest, 0, footer_y - 4, SCREEN_WIDTH, line_h + 8, theme.background);

        std::string label = selected->title;
        if (!selected->author.empty())
        {
            label += " — " + selected->author;
        }

        text::draw_text(
            dest, small, elide(small, label, SCREEN_WIDTH - 2 * PADDING - 60).c_str(),
            PADDING, footer_y + text::font_ascent(small),
            theme.main_text, theme.background
        );

        if (state->outstanding > 0)
        {
            text::draw_text(
                dest, small, "indexing…", SCREEN_WIDTH - PADDING - 90,
                footer_y + text::font_ascent(small),
                theme.secondary_text, theme.background
            );
        }
    }

    return true;
}

bool LibraryView::is_done()
{
    return state->is_done;
}

void LibraryView::on_focus()
{
    state->refresh_entries();
}

void LibraryView::on_keypress(SDLKey key)
{
    const size_t total = state->total_items();
    if (total == 0)
    {
        if (key == SW_BTN_Y && state->on_browse_requested)
        {
            state->on_browse_requested();
        }
        return;
    }

    const size_t shelf_n = state->shelf.size();
    size_t cursor = state->cursor;

    switch (key)
    {
        case SW_BTN_LEFT:
            if (cursor > 0) { --cursor; }
            break;
        case SW_BTN_RIGHT:
            if (cursor + 1 < total) { ++cursor; }
            break;
        case SW_BTN_UP:
            if (cursor >= shelf_n + GRID_COLUMNS)
            {
                cursor -= GRID_COLUMNS;
            }
            else if (cursor >= shelf_n && shelf_n > 0)
            {
                // Stepping up out of the grid lands on the shelf column beneath it.
                cursor = std::min(shelf_n - 1, (cursor - shelf_n) % GRID_COLUMNS);
            }
            break;
        case SW_BTN_DOWN:
            if (cursor < shelf_n)
            {
                const size_t target = shelf_n + cursor;
                cursor = std::min(target, total - 1);
            }
            else if (cursor + GRID_COLUMNS < total)
            {
                cursor += GRID_COLUMNS;
            }
            break;
        case SW_BTN_A:
            if (const LibraryEntry *entry = state->item_at(cursor))
            {
                if (state->on_book_selected)
                {
                    state->on_book_selected(entry->path);
                }
            }
            return;
        case SW_BTN_Y:
            if (state->on_browse_requested)
            {
                state->on_browse_requested();
            }
            return;
        default:
            return;
    }

    if (cursor != state->cursor)
    {
        state->cursor = cursor;

        // Keep the selection on screen.
        if (cursor >= shelf_n)
        {
            const int row = static_cast<int>((cursor - shelf_n) / GRID_COLUMNS);
            if (row < state->grid_scroll_row)
            {
                state->grid_scroll_row = row;
            }
            else if (row > state->grid_scroll_row + 1)
            {
                state->grid_scroll_row = row - 1;
            }
        }

        state->needs_render = true;
    }
}

void LibraryView::on_keyheld(SDLKey key, uint32_t held_time_ms)
{
    switch (key)
    {
        case SW_BTN_UP:
        case SW_BTN_DOWN:
        case SW_BTN_LEFT:
        case SW_BTN_RIGHT:
            if (state->scroll_throttle(held_time_ms))
            {
                on_keypress(key);
            }
            break;
        default:
            break;
    }
}
