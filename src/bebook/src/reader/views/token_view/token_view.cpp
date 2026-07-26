#include "./token_view.h"

#include "./token_line_scroller.h"
#include "./token_view_styling.h"
#include "./word_layout.h"

#include "doc_api/doc_reader.h"
#include "doc_api/link_runs.h"
#include "reader/config.h"
#include "reader/system_styling.h"
#include "reader/shoulder_keymap.h"
#include "sys/battery.h"
#include "sys/keymap.h"
#include "sys/screen.h"
#include "util/sdl_utils.h"
#include "util/str_utils.h"
#include "util/throttled.h"

#include "text/hyphenate.h"
#include "text/line_break.h"
#include "text/styled_text.h"

#include <algorithm>
#include <stdexcept>

namespace {

// Lays out one paragraph with the current font and column.
//
// The measurement callback hands the breaker exact 26.6 widths of arbitrary byte ranges.
// The old SDL_ttf path could not do this: TTF_SizeUTF8 only took C strings, so measuring
// a slice meant casting away const and poking a temporary NUL into the caller's buffer.
std::vector<text::Line> layout_paragraph_with(
    text::Font *font,
    int avail_width,
    bool justify,
    bool hyphenate,
    const char *s,
    uint32_t len,
    const std::vector<text::StyleRun> &style_runs
)
{
    text::StyledText styled;
    styled.text = s;
    styled.length = len;
    styled.runs = style_runs.empty() ? nullptr : &style_runs;
    styled.family = font->family;
    styled.size_px = font->size_px;

    const text::Fixed first_line_indent =
        text::int_to_fixed(static_cast<int>(font->size_px) * PARAGRAPH_INDENT_PERCENT / 100);

    return text::layout_paragraph(
        styled, font, avail_width,
        justify ? text::Align::Justify : text::Align::Left,
        hyphenate, first_line_indent
    );
}

// Build the StyledText for a laid-out text line, exactly as render() does.
text::StyledText styled_for(const TextLine *tl, const text::Font *font)
{
    text::StyledText styled;
    styled.text = tl->text.c_str();
    styled.length = static_cast<uint32_t>(tl->text.size());
    styled.runs = tl->style_runs.empty() ? nullptr : &tl->style_runs;
    styled.family = font->family;
    styled.size_px = font->size_px;
    return styled;
}

struct WordBox { int x0; int x1; };

// A TextLine as a text::Line (the whole line is one laid-out line, offset 0), so the shared
// text engine can position it.
text::Line as_text_line(const TextLine *tl)
{
    text::Line ln;
    ln.offset = 0;
    ln.length = static_cast<uint32_t>(tl->text.size());
    ln.trailing_hyphen = tl->trailing_hyphen;
    ln.natural_width = tl->natural_width;
    ln.target_width = tl->target_width;
    ln.stretch_gaps = tl->stretch_gaps;
    return ln;
}

text::Align line_align(const TextLine *tl)
{
    return tl->centered ? text::Align::Center : text::Align::Justify;
}

// On-screen x-range of byte range [start, end) in a laid-out line, via the shared
// text::pen_x_at so the geometry uses the exact same justification math as the glyph
// drawing (no re-implementation to drift out of sync). Used for both the word-selection
// highlight and link underlines.
WordBox span_box(
    const TextLine *tl, const text::Font *font, int text_width, int margin_x,
    uint32_t start, uint32_t end
)
{
    text::StyledText styled = styled_for(tl, font);
    const text::Line ln = as_text_line(tl);
    const text::Align align = line_align(tl);
    const int x0 = text::pen_x_at(styled, ln, start, margin_x, text_width, align);
    const int x1 = text::pen_x_at(styled, ln, end, margin_x, text_width, align);
    return {x0, x1};
}

WordBox word_box(
    const TextLine *tl, const text::Font *font, int text_width, int margin_x,
    const WordSpan &span
)
{
    return span_box(tl, font, text_width, margin_x, span.start, span.end);
}

// Words in the display line at relative offset `rel`, or empty if it is not a text line.
std::vector<WordSpan> spans_at(TokenLineScroller &scroller, int rel)
{
    const DisplayLine *line = scroller.get_line_relative(rel);
    if (!line || line->type != DisplayLine::Type::Text)
    {
        return {};
    }
    return tokenize_words(static_cast<const TextLine *>(line)->text);
}

}  // namespace

// What the view is currently doing with the keyboard. Reading is the default page-turn /
// scroll mode; WordSelect is the dictionary picker. Kept as an enum rather than a set of
// bools so adding a mode later (e.g. annotation) is one case, not another flag to clear.
enum class TokenViewMode
{
    Reading,
    WordSelect,
};

struct TokenViewState
{
    SystemStyling &sys_styling;
    TokenViewStyling &token_view_styling;
    const uint32_t sys_styling_sub_id;
    const uint32_t token_view_styling_sub_id;

    text::Font *current_font = nullptr;

    // Side margin. Was 4px, which ran the text into the bezel.
    const int margin_x = TEXT_MARGIN_X;
    int line_height;

    int text_width() const { return SCREEN_WIDTH - 2 * margin_x; }

    TokenLineScroller line_scroller;

    bool needs_render = true;

    std::string title;
    // Shown in place of the title while a word is selected. Empty leaves the title.
    std::string hint;
    int title_progress_percent = 0;

    std::function<void(DocAddr)> on_scroll;

    // Word-selection mode. When mode == WordSelect, `ws_line` is the relative line offset
    // (as passed to get_line_relative) of the highlighted line and `ws_word` the word
    // index within it. Both are re-clamped on render, so a font/size change can never
    // point them out of range.
    TokenViewMode mode = TokenViewMode::Reading;
    int ws_line = 0;
    int ws_word = 0;
    std::function<void(const std::string &)> on_open_word;

    // Which buttons do what once a word is selected. See ws_handle_key.
    TokenView::WordSelectKeys word_select_keys = TokenView::WordSelectKeys::LookupOnA;
    std::function<void(const std::string &)> on_follow_link;

    bool follows_links() const
    {
        return word_select_keys == TokenView::WordSelectKeys::FollowOnA;
    }

    bool word_select() const { return mode == TokenViewMode::WordSelect; }

    Throttled line_scroll_throttle;
    Throttled page_scroll_throttle;

    int num_display_lines() const
    {
        return SCREEN_HEIGHT / line_height;
    }

    int num_text_display_lines() const
    {
        bool show_title_bar = token_view_styling.get_show_title_bar();
        return num_display_lines() - (show_title_bar ? 1 : 0);
    }

    int excess_pxl_y() const
    {
        return SCREEN_HEIGHT - num_display_lines() * line_height;
    }

    int line_pxl_limit_y() const
    {
        if (!token_view_styling.get_show_title_bar())
        {
            return SCREEN_HEIGHT;
        }

        return SCREEN_HEIGHT - line_height - excess_pxl_y() / 2;
    }

    TokenViewState(std::shared_ptr<DocReader> reader, DocAddr address, SystemStyling &sys_styling, TokenViewStyling &token_view_styling)
        : sys_styling(sys_styling),
          token_view_styling(token_view_styling),
          sys_styling_sub_id(sys_styling.subscribe_to_changes([this](SystemStyling::ChangeId change_id) {
              if (change_id == SystemStyling::ChangeId::FONT_SIZE || change_id == SystemStyling::ChangeId::FONT_NAME)
              {
                  current_font = this->sys_styling.get_loaded_font();
                  line_height = detect_line_height(current_font) * LINE_HEIGHT_PERCENT / 100;
                  line_scroller.set_line_height_pixels(line_height);
                  line_scroller.reset_buffer();  // need to re-wrap lines if font-size changed
              }
              needs_render = true;
          })),
          token_view_styling_sub_id(token_view_styling.subscribe_to_changes([this]() {
              needs_render = true;
          })),
          current_font(sys_styling.get_loaded_font()),
          line_height(detect_line_height(sys_styling.get_font_name(), sys_styling.get_font_size()) * LINE_HEIGHT_PERCENT / 100),
          line_scroller(
              reader,
              address,
              [this](const char *s, uint32_t len, const std::vector<text::StyleRun> &runs) {
                  return layout_paragraph_with(
                      current_font,
                      text_width(),
                      this->token_view_styling.get_justify(),
                      this->token_view_styling.get_hyphenate(),
                      s,
                      len,
                      runs
                  );
              },
              line_height
          ),
          line_scroll_throttle(250, 50),
          page_scroll_throttle(750, 150)
    {
    }

    ~TokenViewState()
    {
        sys_styling.unsubscribe_from_changes(sys_styling_sub_id);
        token_view_styling.unsubscribe_from_changes(token_view_styling_sub_id);
    }
};

TokenView::TokenView(std::shared_ptr<DocReader> reader, DocAddr address, SystemStyling &sys_styling, TokenViewStyling &token_view_styling)
    : state(std::make_unique<TokenViewState>(reader, address, sys_styling, token_view_styling))
{
}

TokenView::~TokenView()
{
}

bool TokenView::render(SDL_Surface *dest_surface, bool force_render)
{
    if (!state->needs_render && !force_render)
    {
        return false;
    }
    state->needs_render = false;

    scroll(0);  // Will adjust scroll position if necessary for end of book

    text::Font *font = state->current_font;
    const auto &theme = state->sys_styling.get_loaded_color_theme();
    const int line_height = state->line_height;
    const int margin_x = state->margin_x;
    const int ascent = text::font_ascent(font);

    // Leading is added around the glyphs, so centre the text within the line box
    // instead of hanging it from the top; otherwise extra leading pushes every line
    // down and the page looks top-heavy.
    const int leading_above = (line_height - detect_line_height(font)) / 2;

    // Clear screen
    {
        SDL_Rect rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
        const auto &bgcolor = theme.background;

        SDL_FillRect(
            dest_surface,
            &rect,
            SDL_MapRGB(dest_surface->format, bgcolor.r, bgcolor.g, bgcolor.b)
        );
    }

    int num_text_display_lines = state->num_text_display_lines();
    const Uint16 padding_y = state->excess_pxl_y() / 2;
    Sint16 line_y = padding_y;

    for (int i = 0; i < num_text_display_lines; ++i)
    {
        const DisplayLine *line = state->line_scroller.get_line_relative(i);
        if (line)
        {
            if (line->type == DisplayLine::Type::Text)
            {
                const auto *text_line = static_cast<const TextLine *>(line);
                const uint32_t len = static_cast<uint32_t>(text_line->text.size());

                text::StyledText styled;
                styled.text = text_line->text.c_str();
                styled.length = len;
                styled.runs = text_line->style_runs.empty() ? nullptr : &text_line->style_runs;
                styled.family = font->family;
                styled.size_px = font->size_px;

                // The laid-out line's geometry, as a text::Line, so the same aligned-draw
                // helper the dictionary popup uses handles the centre/justify math. A
                // centred heading was laid out ragged (target == natural), so Justify and
                // Center agree on it except for the pen x, which is what Align selects.
                //
                // Drawn straight onto the destination rather than into a per-line surface
                // that is then blitted: no allocation on the scroll path, and glyphs keep
                // their fractional horizontal positions instead of snapping to a surface.
                text::Line ln;
                ln.offset = 0;
                ln.length = len;
                ln.trailing_hyphen = text_line->trailing_hyphen;
                ln.natural_width = text_line->natural_width;
                ln.target_width = text_line->target_width;
                ln.stretch_gaps = text_line->stretch_gaps;

                text::draw_line_aligned(
                    dest_surface, styled, ln,
                    margin_x, state->text_width(),
                    line_y + leading_above + ascent,
                    text_line->centered ? text::Align::Center : text::Align::Justify,
                    theme.main_text, theme.background
                );
            }
            else if (line->type == DisplayLine::Type::Image || (line->type == DisplayLine::Type::ImageRef && i == 0))
            {
                const ImageLine *image_line = nullptr;
                uint32_t line_offset = 0;

                if (line->type == DisplayLine::Type::ImageRef)
                {
                    line_offset = static_cast<const ImageRefLine *>(line)->offset;
                    const DisplayLine *ref_line = state->line_scroller.get_line_relative(i - line_offset);
                    if (ref_line)
                    {
                        if (ref_line->type != DisplayLine::Type::Image)
                        {
                            throw std::runtime_error("ImageRefLine points to non image");
                        }
                        image_line = static_cast<const ImageLine *>(ref_line);
                    }
                }
                else
                {
                    image_line = static_cast<const ImageLine *>(line);
                }

                if (image_line)
                {
                    auto *surface = state->line_scroller.load_scaled_image(image_line->image_path);

                    // Amount of line height not used by image
                    uint32_t img_excess_y = image_line->num_lines * line_height - image_line->height;
                    // Y coordinate of image in screen space
                    int screen_start_y = line_y + img_excess_y / 2 - line_height * line_offset;

                    // Crop off-screen part of image. Allow to extend to edge of screen.
                    Sint16 src_y = std::max(-screen_start_y, 0);
                    Sint16 dst_y = std::max(screen_start_y, 0);

                    if (surface && src_y < (Sint16)image_line->height)
                    {
                        Uint16 width = image_line->width;
                        Uint16 height = image_line->height - src_y;

                        // Crop bottom
                        auto dst_y_bottom = dst_y + height;
                        Uint16 y_limit = state->line_pxl_limit_y();
                        if (dst_y_bottom > y_limit)
                        {
                            height -= dst_y_bottom - y_limit;
                        }

                        SDL_Rect src_rect = {0, src_y, width, height};
                        SDL_Rect dest_rect = {
                            static_cast<Sint16>((SCREEN_WIDTH - width) / 2),
                            dst_y,
                            0,
                            0
                        };
                        SDL_BlitSurface(surface, &src_rect, dest_surface, &dest_rect);
                    }
                }
            }
        }
        else
        {
            break;
        }

        line_y += line_height;
    }

    // Link underlines. Only the wiki reader produces link runs, so this loop costs epubs
    // nothing. Drawn before the word-select highlight so the highlight sits on top.
    // ColorTheme has no link colour, and adding one would touch every theme, so links are
    // marked with a rule in the secondary text colour rather than by recolouring glyphs.
    for (int i = 0; i < num_text_display_lines; ++i)
    {
        const DisplayLine *line = state->line_scroller.get_line_relative(i);
        if (!line || line->type != DisplayLine::Type::Text)
        {
            continue;
        }

        const auto *tl = static_cast<const TextLine *>(line);
        if (tl->link_runs.empty())
        {
            continue;
        }

        const int rule_y = padding_y + i * line_height + leading_above + ascent + 2;
        const auto &fg = theme.secondary_text;
        const Uint32 rule_colour = SDL_MapRGB(dest_surface->format, fg.r, fg.g, fg.b);

        for (const auto &link : tl->link_runs)
        {
            const WordBox lb = span_box(
                tl, font, state->text_width(), margin_x,
                link.offset, link.offset + link.length
            );
            if (lb.x1 <= lb.x0)
            {
                continue;
            }

            SDL_Rect rule = {
                static_cast<Sint16>(lb.x0),
                static_cast<Sint16>(rule_y),
                static_cast<Uint16>(lb.x1 - lb.x0),
                1
            };
            SDL_FillRect(dest_surface, &rule, rule_colour);
        }
    }

    // Word-selection highlight. Drawn after the body text so it sits on top: a filled
    // rect in the highlight colour with the word's glyphs redrawn over it. ws_line and
    // ws_word are re-clamped here, so a font/size change between frames (which re-wraps
    // and can shorten a line) can never leave the highlight pointing off the page.
    if (state->word_select())
    {
        const int n = num_text_display_lines;

        std::vector<WordSpan> spans;
        if (state->ws_line >= 0 && state->ws_line < n)
        {
            spans = spans_at(state->line_scroller, state->ws_line);
        }

        // If the remembered line no longer holds words, snap to the first visible line
        // that does. If none do, the mode has nothing to point at, so leave it.
        if (spans.empty())
        {
            state->ws_word = 0;
            for (int i = 0; i < n; ++i)
            {
                auto s = spans_at(state->line_scroller, i);
                if (!s.empty())
                {
                    state->ws_line = i;
                    spans = std::move(s);
                    break;
                }
            }
        }

        if (!spans.empty())
        {
            state->ws_word = std::max(0, std::min(state->ws_word, (int)spans.size() - 1));

            const DisplayLine *line = state->line_scroller.get_line_relative(state->ws_line);
            const auto *tl = static_cast<const TextLine *>(line);
            const WordSpan &sp = spans[state->ws_word];

            WordBox wb = word_box(tl, font, state->text_width(), margin_x, sp);

            const int pad = 2;
            const Sint16 box_y = static_cast<Sint16>(padding_y + state->ws_line * line_height);
            SDL_Rect box = {
                static_cast<Sint16>(wb.x0 - pad),
                box_y,
                static_cast<Uint16>((wb.x1 - wb.x0) + 2 * pad),
                static_cast<Uint16>(line_height)
            };

            const auto &hlbg = theme.highlight_background;
            SDL_FillRect(dest_surface, &box, SDL_MapRGB(dest_surface->format, hlbg.r, hlbg.g, hlbg.b));

            // Redraw the whole line's glyphs in the highlight colour but clipped to the word
            // box, so only the selected word repaints. Through the same aligned-draw helper
            // the body text uses, so the glyphs stay pixel-identical under the highlight.
            text::StyledText styled = styled_for(tl, font);
            text::draw_line_aligned(
                dest_surface, styled, as_text_line(tl),
                margin_x, state->text_width(),
                box_y + leading_above + ascent, line_align(tl),
                theme.highlight_text, theme.highlight_background,
                &box
            );
        }
        else
        {
            state->mode = TokenViewMode::Reading;
        }
    }

    if (state->token_view_styling.get_show_title_bar())
    {
        // Recompute for short book case
        line_y = padding_y + num_text_display_lines * line_height;
        SDL_Rect title_crop_rect = {0, 0, 0, (Uint16)line_height};

        const int title_baseline = line_y + leading_above + ascent;

        // Battery, at the far right. A glyph rather than a second percentage: a number
        // here would sit beside the reading progress with nothing to say which was
        // which. Absent when it cannot be read, so nothing changes off-device.
        int battery_w = 0;
        {
            const int battery_percent = read_battery_percent();
            if (battery_percent >= 0)
            {
                const int body_h = std::max(6, line_height / 3);
                const int body_w = body_h * 2;
                const int cap_w = std::max(1, body_h / 4);
                const int cap_h = std::max(2, body_h / 2);
                battery_w = body_w + cap_w + margin_x / 2;

                const int x = SCREEN_WIDTH - margin_x - body_w - cap_w;
                const int y = line_y + (line_height - body_h) / 2;

                const Uint32 fg = SDL_MapRGB(
                    dest_surface->format,
                    theme.secondary_text.r, theme.secondary_text.g, theme.secondary_text.b
                );
                const Uint32 bg = SDL_MapRGB(
                    dest_surface->format,
                    theme.background.r, theme.background.g, theme.background.b
                );

                // Outline, hollowed out, then filled to the charge level.
                SDL_Rect body = {(Sint16)x, (Sint16)y, (Uint16)body_w, (Uint16)body_h};
                SDL_FillRect(dest_surface, &body, fg);
                SDL_Rect inner = {
                    (Sint16)(x + 1), (Sint16)(y + 1),
                    (Uint16)(body_w - 2), (Uint16)(body_h - 2)
                };
                SDL_FillRect(dest_surface, &inner, bg);

                const int fill_w = ((body_w - 4) * battery_percent) / 100;
                if (fill_w > 0)
                {
                    SDL_Rect fill = {
                        (Sint16)(x + 2), (Sint16)(y + 2),
                        (Uint16)fill_w, (Uint16)(body_h - 4)
                    };
                    SDL_FillRect(dest_surface, &fill, fg);
                }

                SDL_Rect cap = {
                    (Sint16)(x + body_w), (Sint16)(y + (body_h - cap_h) / 2),
                    (Uint16)cap_w, (Uint16)cap_h
                };
                SDL_FillRect(dest_surface, &cap, fg);
            }
        }

        // Progress
        {
            char percent_str[32];
            snprintf(percent_str, sizeof(percent_str), " %d%%", state->title_progress_percent);

            int percent_w = 0;
            text::text_size(font, percent_str, &percent_w, nullptr);

            text::draw_text(
                dest_surface, font, percent_str,
                SCREEN_WIDTH - percent_w - margin_x - battery_w, title_baseline,
                theme.secondary_text, theme.background
            );

            title_crop_rect.w = SCREEN_WIDTH - margin_x * 2 - percent_w - battery_w;
        }

        // Title, or what the buttons do. While a word is selected the bindings are worth
        // more than the title -- it is the one moment the user needs them, and it costs no
        // screen space to say so here rather than on a line of its own.
        const std::string &bar_text = state->word_select() && !state->hint.empty()
            ? state->hint
            : state->title;

        if (bar_text.size() > 0)
        {
            SDL_Rect clip = {
                static_cast<Sint16>(margin_x),
                static_cast<Sint16>(line_y),
                static_cast<Uint16>(title_crop_rect.w > 0 ? title_crop_rect.w : 0),
                static_cast<Uint16>(line_height)
            };

            text::draw_text(
                dest_surface, font, bar_text.c_str(),
                margin_x, title_baseline,
                theme.secondary_text, theme.background,
                &clip
            );
        }
    }

    return true;
}

// Adjust scroll amount to avoid going beyond start or end of book.
static int get_bounded_scroll_amount(TokenLineScroller &line_scroller, int num_display_lines, int num_lines)
{
    {
        // if start/end of book is within reach, make sure it is discovered
        line_scroller.get_line_relative(num_display_lines);
        line_scroller.get_line_relative(-num_display_lines);
    }

    int cur_line = line_scroller.get_line_number();
    int new_line = cur_line + num_lines;

    auto end_line = line_scroller.end_line_number();
    if (end_line)
    {
        new_line = std::min(
            *end_line - num_display_lines,
            new_line
        );
    }

    auto first_line = line_scroller.first_line_number();
    if (first_line)
    {
        new_line = std::max(
            *first_line,
            new_line
        );
    }

    return new_line - cur_line;
}

void TokenView::scroll(int num_lines)
{
    num_lines = get_bounded_scroll_amount(
        state->line_scroller,
        state->num_text_display_lines(),
        num_lines
    );
    if (num_lines != 0)
    {
        state->needs_render = true;
        state->line_scroller.seek_lines_relative(num_lines);
        if (state->on_scroll)
        {
            state->on_scroll(get_address());
        }
    }
}

void TokenView::on_keypress(SDLKey key)
{
    // A shoulder button is the word-select control: it maps to "previous"/"next"
    // (SW_BTN_LEFT / SW_BTN_RIGHT). `shoulder` is that canonical key, or SDLK_UNKNOWN if
    // this press was not a shoulder button.
    SDLKey shoulder = SDLK_UNKNOWN;
    if (key == SW_BTN_L1 || key == SW_BTN_R1 || key == SW_BTN_L2 || key == SW_BTN_R2)
    {
        auto [l_key, r_key] = get_shoulder_keymap_lr(
            state->sys_styling.get_shoulder_keymap()
        );
        if (key == l_key)
        {
            shoulder = SW_BTN_LEFT;
        }
        else if (key == r_key)
        {
            shoulder = SW_BTN_RIGHT;
        }
    }

    // While word-select is active it owns the keyboard: shoulders move the highlight, and
    // the owning view routes A/B/D-pad here too.
    if (state->word_select())
    {
        ws_handle_key(shoulder != SDLK_UNKNOWN ? shoulder : key);
        return;
    }

    // A shoulder press from reading mode enters word-select on the first visible word.
    if (shoulder != SDLK_UNKNOWN)
    {
        ws_enter();
        return;
    }

    switch (key) {
        case SW_BTN_UP:
            scroll(-1);
            break;
        case SW_BTN_DOWN:
            scroll(1);
            break;
        case SW_BTN_LEFT:
            scroll(-state->num_text_display_lines());
            break;
        case SW_BTN_RIGHT:
            scroll(state->num_text_display_lines());
            break;
        default:
            break;
    }
}

void TokenView::on_keyheld(SDLKey key, uint32_t held_time_ms)
{
    switch (key) {
        case SW_BTN_UP:
        case SW_BTN_DOWN:
            if (state->line_scroll_throttle(held_time_ms))
            {
                on_keypress(key);
            }
            break;
        case SW_BTN_LEFT:
        case SW_BTN_RIGHT:
        case SW_BTN_L1:
        case SW_BTN_R1:
        case SW_BTN_L2:
        case SW_BTN_R2:
            if (state->page_scroll_throttle(held_time_ms))
            {
                on_keypress(key);
            }
            break;
        default:
            break;
    }
}

bool TokenView::is_done()
{
    return false;
}

DocAddr TokenView::get_address() const
{
    const DisplayLine *line = state->line_scroller.get_line_relative(0);
    if (line)
    {
        return line->address;
    }
    return 0;
}

void TokenView::seek_to_address(DocAddr address)
{
    state->line_scroller.seek_to_address(address);
    state->needs_render = true;
}

void TokenView::set_word_select_hint(const std::string &hint)
{
    state->hint = hint;
}

void TokenView::set_title(const std::string &title)
{
    if (title != state->title)
    {
        state->title = title;
        state->needs_render = true;
    }
}

void TokenView::set_title_progress(int percent)
{
    if (percent != state->title_progress_percent)
    {
        state->title_progress_percent = percent;
        state->needs_render = true;
    }
}

void TokenView::set_on_scroll(std::function<void(DocAddr)> callback)
{
    state->on_scroll = callback;
}

// ---- Word-selection mode ----------------------------------------------------------

bool TokenView::is_word_select_active() const
{
    return state->word_select();
}

void TokenView::set_on_open_word(std::function<void(const std::string &)> callback)
{
    state->on_open_word = std::move(callback);
}

void TokenView::enter_word_select()
{
    ws_enter();
}

void TokenView::set_word_select_keys(WordSelectKeys keys)
{
    state->word_select_keys = keys;
}

void TokenView::set_on_follow_link(std::function<void(const std::string &)> callback)
{
    state->on_follow_link = std::move(callback);
}

int TokenView::scroll_reporting(int n)
{
    const int before = state->line_scroller.get_line_number();
    scroll(n);
    return state->line_scroller.get_line_number() - before;
}

void TokenView::ws_enter()
{
    const int n = state->num_text_display_lines();
    for (int i = 0; i < n; ++i)
    {
        if (!spans_at(state->line_scroller, i).empty())
        {
            state->mode = TokenViewMode::WordSelect;
            state->ws_line = i;
            state->ws_word = 0;
            state->needs_render = true;
            return;
        }
    }
    // Nothing selectable on screen (e.g. a full-page image); stay in reading mode.
}

void TokenView::ws_exit()
{
    if (state->word_select())
    {
        state->mode = TokenViewMode::Reading;
        state->needs_render = true;
    }
}

void TokenView::ws_handle_key(SDLKey key)
{
    switch (key)
    {
        case SW_BTN_LEFT:  ws_move_word(-1); break;
        case SW_BTN_RIGHT: ws_move_word(1); break;
        case SW_BTN_UP:    ws_move_line(-1); break;
        case SW_BTN_DOWN:  ws_move_line(1); break;
        case SW_BTN_B:     ws_exit(); break;

        // A is the primary action, and what it does depends on the document, not on
        // whether a callback happens to be set -- wiring one up must never silently
        // rebind a button. See WordSelectKeys.
        case SW_BTN_A:
            if (state->follows_links()) { ws_follow_selected(); } else { ws_open_selected(); }
            break;
        case SW_BTN_X:
            if (state->follows_links()) { ws_open_selected(); }
            break;

        default: break;
    }
}

void TokenView::ws_move_word(int dir)
{
    if (!state->word_select())
    {
        return;
    }

    // Stay on the current line if the neighbouring word index is valid.
    {
        auto cur = spans_at(state->line_scroller, state->ws_line);
        const int next = state->ws_word + dir;
        if (next >= 0 && next < (int)cur.size())
        {
            state->ws_word = next;
            state->needs_render = true;
            return;
        }
    }

    // Otherwise cross to the nearest line in `dir` that has words.
    if (ws_walk(dir))
    {
        auto spans = spans_at(state->line_scroller, state->ws_line);
        state->ws_word = dir > 0 ? 0 : (int)spans.size() - 1;
    }
}

void TokenView::ws_move_line(int dir)
{
    if (!state->word_select())
    {
        return;
    }

    if (ws_walk(dir))
    {
        auto spans = spans_at(state->line_scroller, state->ws_line);
        // Keep the column roughly where it was rather than snapping to word 0.
        state->ws_word = std::min(state->ws_word, (int)spans.size() - 1);
    }
}

bool TokenView::ws_walk(int dir)
{
    const int n = state->num_text_display_lines();
    // Bound the scrolling so a press over a large image/blank region can't jump the page
    // far: search at most ~one page of scrolling, then restore the original position so a
    // fruitless press is a no-op. The 4096 guard only bounds the walk over already-visible
    // blank lines.
    const int scroll_budget = n + 1;
    const int start_line_num = state->line_scroller.get_line_number();
    int scrolled = 0;
    int line = state->ws_line;

    for (int guard = 0; guard < 4096; ++guard)
    {
        int cand = line + dir;
        if (cand < 0)
        {
            if (scrolled >= scroll_budget || scroll_reporting(-1) == 0) break;
            ++scrolled;
            cand = 0;
        }
        else if (cand >= n)
        {
            if (scrolled >= scroll_budget || scroll_reporting(1) == 0) break;
            ++scrolled;
            cand = n - 1;
        }
        line = cand;

        if (!spans_at(state->line_scroller, line).empty())
        {
            state->ws_line = line;
            state->needs_render = true;
            return true;
        }
    }

    // Not found within budget: undo any scrolling so the highlight and page stay put.
    const int now = state->line_scroller.get_line_number();
    if (now != start_line_num)
    {
        scroll(start_line_num - now);
    }
    return false;
}

// The line and word span currently under the highlight, or false if there is none.
bool TokenView::ws_selected_span(const TextLine **out_line, WordSpan *out_span) const
{
    if (!state->word_select())
    {
        return false;
    }

    auto spans = spans_at(state->line_scroller, state->ws_line);
    if (state->ws_word < 0 || state->ws_word >= (int)spans.size())
    {
        return false;
    }

    const DisplayLine *line = state->line_scroller.get_line_relative(state->ws_line);
    if (!line || line->type != DisplayLine::Type::Text)
    {
        return false;
    }

    *out_line = static_cast<const TextLine *>(line);
    *out_span = spans[state->ws_word];
    return true;
}

void TokenView::ws_open_selected()
{
    const TextLine *tl = nullptr;
    WordSpan sp{0, 0};
    if (!state->on_open_word || !ws_selected_span(&tl, &sp))
    {
        return;
    }

    state->on_open_word(tl->text.substr(sp.start, sp.end - sp.start));
}

void TokenView::ws_follow_selected()
{
    const TextLine *tl = nullptr;
    WordSpan sp{0, 0};
    if (!ws_selected_span(&tl, &sp))
    {
        return;
    }

    // Overlap rather than containment: tokenize_words drops leading punctuation, so a
    // word inside «Roma» starts a byte after the link its glyphs sit under.
    const LinkRun *link = link_run_overlapping(tl->link_runs, sp.start, sp.end);
    if (link != nullptr && state->on_follow_link)
    {
        state->on_follow_link(link->target);
        return;
    }

    // Not a link. Look the word up rather than doing nothing: A is the primary button and
    // a press that produces no response at all reads as the app being broken. Only the
    // underline distinguishes the two cases, and it is one dim pixel.
    ws_open_selected();
}
