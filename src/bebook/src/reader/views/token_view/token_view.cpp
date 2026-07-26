#include "./token_view.h"

#include "./token_line_scroller.h"
#include "./token_view_styling.h"
#include "./word_layout.h"
#include "./ws_camera.h"

#include "util/debounced.h"

#include "doc_api/doc_reader.h"
#include "doc_api/link_runs.h"
#include "reader/config.h"
#include "reader/system_styling.h"
#include "reader/shoulder_keymap.h"
#include "sys/battery.h"
#include "sys/keymap.h"
#include "sys/screen.h"
#include "util/sdl_utils.h"
#include "util/accelerating_throttle.h"
#include "util/str_utils.h"
#include "util/throttled.h"

#include "text/hyphenate.h"
#include "text/line_break.h"
#include "text/styled_text.h"

#include <algorithm>
#include <stdexcept>

namespace {

// Lines the peek panel occupies at the top of the screen. One: a second cost a line of
// text on a screen that only has ten, which is too much to pay for a zone that mostly
// repeats what the popup will say anyway. Permanent rather than shown-on-demand because the
// cursor is always live -- a panel that came and went would shift the text under it as the
// cursor moved.
const int PEEK_LINES = 1;

// How far L1/R1 nudge when the owner has not claimed them. Small on purpose: enough to
// follow a long paragraph without losing the line you were on.
const int SHOULDER_NUDGE_LINES = 3;

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

// The indent the first line of a paragraph is drawn at. The line breaker already narrowed
// that line by this much; without shifting the pen too, the space would sit at the right
// end of the line rather than the left, which is not an indent at all.
int line_indent(const text::Font *font, const TextLine *tl)
{
    if (tl == nullptr || !tl->first_in_paragraph || tl->centered)
    {
        return 0;
    }
    return static_cast<int>(font->size_px) * PARAGRAPH_INDENT_PERCENT / 100;
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
    // Two positions, because they answer different questions: how far through the whole
    // book, and how far through the section being read. One number could never say both,
    // which is what the old Progress setting was choosing between.
    int progress_global_percent = 0;
    int progress_chapter_percent = 0;

    std::function<void(DocAddr)> on_scroll;

    // The word cursor, which is always live. `ws_line` is the relative line offset (as
    // passed to get_line_relative) of the cursor's line and `ws_word` the word index
    // within it. Both are re-clamped on render, so a font/size change can never point them
    // out of range.
    int ws_line = 0;
    int ws_word = 0;
    std::function<void(const std::string &)> on_open_word;

    // One-line meaning HUD for the selected word. `on_word_preview` maps a surface form to a
    // one-line summary (books wire it to the lexicon; the wiki reader leaves it unset). The
    // result is cached against the surface it was computed for, so the provider runs only
    // when the highlight lands on a different word.
    std::function<WordPreview(const std::string &)> on_word_preview;
    WordPreview ws_preview;
    std::string ws_preview_surface;

    // Fuzzy suggestion for a word the lexicon does not know, fetched off the frame. The
    // debounce is what stops a cursor sweeping a line from asking at every step; `asked` is
    // what stops it asking twice for the same word.
    std::function<void(const std::string &, TokenView::SuggestReply)> on_word_unknown;
    Debounced suggest_debounce{300};
    std::string suggest_asked;

    // Whether X follows links. Books have none; a wiki article does. See ws_handle_key.
    bool has_links = false;
    std::function<void(const std::string &)> on_follow_link;

    // The cursor has come to rest on a link. Its own debounce rather than a share of
    // `suggest_debounce`: Debounced fires exactly once per armed period, so two consumers
    // reading one instance race, and whichever asks first silently eats the other's turn.
    // `dwell_told` is keyed on the target rather than the surface, so two different links
    // both reading «Roma» each announce themselves.
    std::function<void(const std::string &)> on_link_dwell;
    Debounced dwell_debounce{300};
    std::string dwell_told;

    // What L1/R1 mean here, when the owner has something better than a nudge. -1 is back.
    std::function<void(int)> on_shoulder_hop;

    bool follows_links() const { return has_links; }

    // Reading up/down repeats a half-page scroll at a gentle steady rate; the word-select
    // cursor (up/down line, left/right word) repeats with an accelerating ease-in.
    Throttled scroll_throttle;
    AcceleratingThrottle ws_move_throttle;

    int num_display_lines() const
    {
        return SCREEN_HEIGHT / line_height;
    }

    // Two lines go to the peek panel at the top, permanently: the pairing being learned on
    // one, what is true of the word on the other. Permanent rather than shown-on-demand
    // because the cursor is always live -- a panel that appeared and vanished would shift
    // every line of text under it as the cursor moved. It costs a line of text, which is
    // the trade this app exists to make.
    int num_text_display_lines() const
    {
        // The title bar always takes a line, the peek panel always takes one.
        return num_display_lines() - 1 - PEEK_LINES;
    }

    // Reading scroll step: half a screen, keeping a few lines of overlap for context.
    int half_page() const
    {
        return std::max(1, num_text_display_lines() / 2);
    }

    int excess_pxl_y() const
    {
        return SCREEN_HEIGHT - num_display_lines() * line_height;
    }

    int line_pxl_limit_y() const
    {
        return SCREEN_HEIGHT - line_height - excess_pxl_y() / 2;
    }

    // Top of the text area: below the peek panel. Every ws_line -> y mapping goes through
    // this, so the panel's line cannot be double-counted or forgotten in one place.
    int text_top() const
    {
        return excess_pxl_y() / 2 + PEEK_LINES * line_height;
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
          // Gentle half-page repeat; and the accelerating cursor gate: ~180ms first steps
          // easing to a ~40ms floor over ~0.5s of holding, then instant stop on release.
          // Lower thresholds throughout: at TARGET_FPS 20 a frame is 50ms, so that is the
          // floor, and the old first-repeat delays made holding a direction feel stuck
          // before it started moving.
          scroll_throttle(250, 150),
          ws_move_throttle(160, 110, 50, 260.0f)
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
    // The cursor is live from the moment the document opens; there is no mode to enter.
    ws_seed();
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
    // Text starts below the permanent peek panel; see TokenViewState::text_top().
    const int text_top = state->text_top();
    Sint16 line_y = static_cast<Sint16>(text_top);

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

                const int indent = line_indent(font, text_line);
                text::draw_line_aligned(
                    dest_surface, styled, ln,
                    margin_x + indent, state->text_width() - indent,
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
    // nothing. Drawn before the word-select highlight, which then sits on top.
    const TextLine *sel_tl = nullptr;
    WordSpan sel_sp{ 0, 0 };
    const bool have_sel = ws_selected_span(&sel_tl, &sel_sp);

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

        const Sint16 box_y = static_cast<Sint16>(text_top + i * line_height);

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

            // A link is underlined, not boxed. The box is what "the cursor is here" means,
            // and spending it on "this could be followed" made a page of links look like a
            // page of selections -- two different facts wearing one costume. An underline
            // is the older and more literal convention, it leaves the glyphs untouched
            // (no fill, so no redraw to avoid haloing the antialiasing), and it stacks:
            // the selected link keeps its box *and* thickens its rule.
            const bool selected =
                have_sel && sel_tl == tl
                && link.offset < sel_sp.end
                && sel_sp.start < link.offset + link.length;

            const auto &c = selected ? theme.link_highlight_background : theme.link_background;
            const int thickness = selected ? 3 : 1;

            SDL_Rect rule = {
                static_cast<Sint16>(lb.x0),
                static_cast<Sint16>(box_y + leading_above + ascent + 2),
                static_cast<Uint16>(lb.x1 - lb.x0),
                static_cast<Uint16>(thickness)
            };
            SDL_FillRect(dest_surface, &rule,
                         SDL_MapRGB(dest_surface->format, c.r, c.g, c.b));
        }
    }

    // Word-selection highlight. Drawn after the body text so it sits on top: a filled
    // rect in the highlight colour with the word's glyphs redrawn over it. ws_line and
    // ws_word are re-clamped here, so a font/size change between frames (which re-wraps
    // and can shorten a line) can never leave the highlight pointing off the page.
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

            // Refresh the meaning preview when the highlight lands on a new word. Cached by
            // surface so the provider (a DB lookup) runs once per selection, not per frame.
            if (state->on_word_preview)
            {
                std::string surface = ws_selected_surface();
                if (surface != state->ws_preview_surface)
                {
                    state->ws_preview_surface = surface;
                    state->ws_preview = state->on_word_preview(surface);
                    // A new word: restart the quiet period, so a cursor still moving asks
                    // for nothing.
                    const uint32_t now = SDL_GetTicks();
                    state->suggest_debounce.poke(now);
                    state->dwell_debounce.poke(now);
                }

                if (state->on_word_unknown && state->ws_preview.glosses.empty()
                    && state->ws_preview_surface != state->suggest_asked
                    && state->suggest_debounce(SDL_GetTicks()))
                {
                    state->suggest_asked = state->ws_preview_surface;
                    state->on_word_unknown(
                        state->ws_preview_surface,
                        [this](const std::string &for_surface, const std::string &text) {
                            // Late answers are dropped: the cursor may have moved on, and a
                            // suggestion for a word no longer under it would be a lie.
                            if (for_surface != state->ws_preview_surface)
                            {
                                return;
                            }
                            state->ws_preview.grammar = text;
                            state->needs_render = true;
                        });
                }
            }

            // The beat between landing on a link and pressing X is free time. Announcing it
            // lets the owner have the article ready before the button, which is the whole
            // difference between a follow that draws immediately and one that stops to read
            // the card. Same settle rule as the suggestion: sweeping past a link says
            // nothing, resting on one says it once.
            if (state->on_link_dwell && state->dwell_debounce(SDL_GetTicks()))
            {
                const LinkRun *link = link_run_overlapping(tl->link_runs, sp.start, sp.end);
                if (link != nullptr && link->target != state->dwell_told)
                {
                    state->dwell_told = link->target;
                    state->on_link_dwell(link->target);
                }
            }

            const int indent = line_indent(font, tl);
            WordBox wb = word_box(tl, font, state->text_width() - indent, margin_x + indent, sp);

            const int pad = 2;
            const Sint16 box_y = static_cast<Sint16>(text_top + state->ws_line * line_height);
            SDL_Rect box = {
                static_cast<Sint16>(wb.x0 - pad),
                box_y,
                static_cast<Uint16>((wb.x1 - wb.x0) + 2 * pad),
                static_cast<Uint16>(line_height)
            };

            // A selected link takes its own colour, so the highlight itself says whether
            // the action button will navigate -- without it the reader has to press and
            // find out. Overlap rather than containment, matching ws_follow_selected:
            // tokenize_words drops leading punctuation, so the word inside «Roma» starts
            // a byte after the link its glyphs sit under.
            const bool on_link =
                link_run_overlapping(tl->link_runs, sp.start, sp.end) != nullptr;
            const auto &hlbg = on_link ? theme.link_highlight_background
                                       : theme.highlight_background;

            SDL_FillRect(dest_surface, &box, SDL_MapRGB(dest_surface->format, hlbg.r, hlbg.g, hlbg.b));

            // Redraw the whole line's glyphs in the highlight colour but clipped to the word
            // box, so only the selected word repaints. Through the same aligned-draw helper
            // the body text uses, so the glyphs stay pixel-identical under the highlight.
            text::StyledText styled = styled_for(tl, font);
            text::draw_line_aligned(
                dest_surface, styled, as_text_line(tl),
                margin_x + indent, state->text_width() - indent,
                box_y + leading_above + ascent, line_align(tl),
                theme.highlight_text, hlbg,
                &box
            );
        }
        // Nothing selectable on screen at all (a full-page image): leave the cursor where
        // it is rather than drawing a highlight over nothing.
    }

    // The title bar keeps its own line at the bottom; the peek panel has its own at the
    // top, so the two no longer compete for one. Always drawn: it carries the breadcrumb,
    // and a reader who cannot see where they are is lost in a way no saved pixel repays.
    {
        // Recompute for short book case
        line_y = text_top + num_text_display_lines * line_height;
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

        // No percentage here any more: the two margin bars say where we are, and better --
        // continuously, and without asking the eye to read a number. The width it used to
        // reserve goes back to the title, which on a wiki is a breadcrumb and wants every
        // pixel it can get.
        title_crop_rect.w = SCREEN_WIDTH - margin_x * 2 - battery_w;

        // Title, or what the buttons do, when the owner has set a hint.
        const std::string &bar_text = !state->hint.empty() ? state->hint : state->title;

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

    // The peek panel: the top line, always present. The pairing being learned on the left in
    // the bright colour, what is true of the word on the right in the dim one -- position
    // and colour separate them, so neither needs punctuation between them.
    //
    // A hairline rule along the bottom edge separates it from the page. Colour alone was not
    // enough: the peek is prose in the same face as the text under it, so without a line the
    // eye reads straight from the meaning into the article as though it were one paragraph.
    {
        SDL_Rect bar = {0, 0, (Uint16)SCREEN_WIDTH, (Uint16)line_height};
        SDL_FillRect(dest_surface, &bar,
            SDL_MapRGB(dest_surface->format, theme.background.r, theme.background.g, theme.background.b));

        SDL_Rect peek_rule = {
            0, static_cast<Sint16>(line_height - 1), (Uint16)SCREEN_WIDTH, 1
        };
        SDL_FillRect(dest_surface, &peek_rule,
            SDL_MapRGB(dest_surface->format,
                       theme.secondary_text.r, theme.secondary_text.g, theme.secondary_text.b));

        const int baseline = leading_above + ascent;
        const int avail = state->text_width();
        const int gap = line_height / 2;
        const WordPreview &peek = state->ws_preview;

        auto width_of = [font](const std::string &t) {
            int w = 0;
            text::text_size(font, t.c_str(), &w, nullptr);
            return w;
        };

        // Build the pairing, taking only as many senses as the width allows. One sense of
        // the sixty-four "fare" has is a coin flip, so it takes what it can rather than
        // always the first.
        std::string meaning = peek.subject;
        if (!peek.glosses.empty())
        {
            const std::string arrow = " \xE2\x86\x92 ";  // " → "
            const int budget = avail * 2 / 3;
            std::string joined;
            for (const auto &gloss : peek.glosses)
            {
                const std::string candidate = joined.empty() ? gloss : joined + ", " + gloss;
                if (width_of(meaning + arrow + candidate) > budget)
                {
                    break;
                }
                joined = candidate;
            }
            if (joined.empty())
            {
                joined = text::elide_to_width(
                    font, peek.glosses.front(),
                    std::max(0, budget - width_of(meaning) - width_of(arrow)));
            }
            if (!joined.empty())
            {
                meaning += arrow + joined;
            }
        }

        int meaning_w = meaning.empty() ? 0 : width_of(meaning);
        std::string grammar = peek.grammar;
        int grammar_w = grammar.empty() ? 0 : width_of(grammar);

        // Only elide when the two actually collide; the meaning is served first, since it
        // carries the pairing while the grammar repeats what is derivable.
        if (meaning_w + (meaning_w && grammar_w ? gap : 0) + grammar_w > avail)
        {
            meaning = text::elide_to_width(font, meaning, avail * 2 / 3);
            meaning_w = meaning.empty() ? 0 : width_of(meaning);
            grammar = text::elide_to_width(
                font, grammar, std::max(0, avail - meaning_w - gap));
            grammar_w = grammar.empty() ? 0 : width_of(grammar);
        }

        if (!meaning.empty())
        {
            text::draw_text(dest_surface, font, meaning.c_str(), margin_x, baseline,
                            theme.main_text, theme.background);
        }
        if (!grammar.empty())
        {
            text::draw_text(dest_surface, font, grammar.c_str(),
                            margin_x + avail - grammar_w, baseline,
                            theme.secondary_text, theme.background);
        }
    }

    // Two vertical progress bars, one down each margin: left is the whole book or article,
    // right is the section being read. Unobtrusive like scrollbars, and always present, so
    // position is something you glance at rather than something you ask for. They live in
    // the margins (TEXT_MARGIN_X is 28) and never touch the text.
    {
        const int bar_w = 5;

        // A fill of zero would read as a broken bar exactly when you most need to know the
        // bar is there, so the thumb never shrinks below this.
        const int min_fill = 14;

        const Uint32 track_c = SDL_MapRGB(dest_surface->format, theme.secondary_text.r,
                                          theme.secondary_text.g, theme.secondary_text.b);
        const Uint32 fill_c = SDL_MapRGB(dest_surface->format, theme.main_text.r,
                                         theme.main_text.g, theme.main_text.b);

        auto draw_bar = [&](Sint16 x, int percent) {
            const int pct = std::max(0, std::min(100, percent));
            const int filled = std::max(min_fill, SCREEN_HEIGHT * pct / 100);

            SDL_Rect track = { x, 0, (Uint16)bar_w, (Uint16)SCREEN_HEIGHT };
            SDL_FillRect(dest_surface, &track, track_c);

            SDL_Rect fill = { x, 0, (Uint16)bar_w,
                              (Uint16)std::min(filled, static_cast<int>(SCREEN_HEIGHT)) };
            SDL_FillRect(dest_surface, &fill, fill_c);
        };

        draw_bar(0, state->progress_global_percent);
        draw_bar(static_cast<Sint16>(SCREEN_WIDTH - bar_w), state->progress_chapter_percent);
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
    // The two shoulder pairs do different jobs, which they can because they are distinct
    // keys (sys/keymap.h) and both pairs used to do the same thing.
    //
    // L2/R2 jump half a page. L1/R1 belong to the owner if it wants them -- the wiki walks
    // its breadcrumb -- and otherwise nudge by a few lines. A few lines rather than a page
    // because the point is to keep your place: the text moves under you a little and the
    // sentence you were reading is still on screen, where a page jump makes you find your
    // way again. Everything here still moves the cursor and lets the camera follow, so
    // there is one rule for how the page moves.
    switch (key)
    {
        case SW_BTN_L2:
            ws_page(-1);
            return;
        case SW_BTN_R2:
            ws_page(1);
            return;
        case SW_BTN_L1:
        case SW_BTN_R1:
        {
            const int dir = (key == SW_BTN_L1) ? -1 : 1;
            if (state->on_shoulder_hop)
            {
                state->on_shoulder_hop(dir);
                return;
            }
            for (int i = 0; i < SHOULDER_NUDGE_LINES; ++i)
            {
                ws_move_line(dir);
            }
            return;
        }
        default:
            break;
    }

    ws_handle_key(key);
}

void TokenView::on_keyheld(SDLKey key, uint32_t held_time_ms)
{
    // Every direction and shoulder repeats through the accelerating gate, so the cursor
    // eases up to speed and stops the instant the key is released.
    switch (key)
    {
        case SW_BTN_UP:
        case SW_BTN_DOWN:
        case SW_BTN_LEFT:
        case SW_BTN_RIGHT:
        case SW_BTN_L1:
        case SW_BTN_R1:
        case SW_BTN_L2:
        case SW_BTN_R2:
            if (state->ws_move_throttle(held_time_ms))
            {
                on_keypress(key);
            }
            break;
        default:
            break;
    }
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
    // ws_line is relative to the viewport, which has just moved out from under the cursor,
    // so place it afresh rather than leaving it pointing at whatever now occupies that row.
    ws_seed();
    ws_recentre();
    state->needs_render = true;
}

bool TokenView::is_done()
{
    return false;
}

void TokenView::set_word_select_hint(const std::string &hint)
{
    state->hint = hint;
}

int TokenView::title_text_width() const
{
    // Mirrors the crop in render(): both margins, the progress percent, and the battery when
    // one can be read. The percent is measured at its widest ("100%") rather than its
    // current value, so the breadcrumb does not reflow as progress ticks over.
    text::Font *font = state->sys_styling.get_loaded_font();
    int percent_w = 0;
    text::text_size(font, "100%", &percent_w, nullptr);

    int battery_w = 0;
    if (read_battery_percent() >= 0)
    {
        const int body_h = std::max(6, state->line_height / 3);
        battery_w = body_h * 2 + std::max(1, body_h / 4) + state->margin_x / 2;
    }

    return std::max(0, SCREEN_WIDTH - state->margin_x * 2 - percent_w - battery_w);
}

void TokenView::set_title(const std::string &title)
{
    if (title != state->title)
    {
        state->title = title;
        state->needs_render = true;
    }
}

void TokenView::set_progress(int global_percent, int chapter_percent)
{
    if (global_percent != state->progress_global_percent
        || chapter_percent != state->progress_chapter_percent)
    {
        state->progress_global_percent = global_percent;
        state->progress_chapter_percent = chapter_percent;
        state->needs_render = true;
    }
}

void TokenView::set_on_scroll(std::function<void(DocAddr)> callback)
{
    state->on_scroll = callback;
}

// ---- The word cursor ---------------------------------------------------------------

void TokenView::set_on_open_word(std::function<void(const std::string &)> callback)
{
    state->on_open_word = std::move(callback);
}

void TokenView::set_on_word_preview(std::function<WordPreview(const std::string &)> callback)
{
    state->on_word_preview = std::move(callback);
}

void TokenView::set_on_word_unknown(
    std::function<void(const std::string &, SuggestReply)> callback)
{
    state->on_word_unknown = std::move(callback);
}

void TokenView::set_follows_links(bool follows)
{
    state->has_links = follows;
}

void TokenView::set_on_follow_link(std::function<void(const std::string &)> callback)
{
    state->on_follow_link = std::move(callback);
}

void TokenView::set_on_link_dwell(std::function<void(const std::string &)> callback)
{
    state->on_link_dwell = std::move(callback);
}

void TokenView::set_on_shoulder_hop(std::function<void(int)> callback)
{
    state->on_shoulder_hop = std::move(callback);
}

int TokenView::scroll_reporting(int n)
{
    const int before = state->line_scroller.get_line_number();
    scroll(n);
    return state->line_scroller.get_line_number() - before;
}

void TokenView::ws_seed()
{
    // Opening position for the cursor: the first line on screen that has words. Nothing
    // selectable (a full-page image) simply leaves it at 0; the render clamp will move it
    // onto a word as soon as one scrolls into view.
    const int n = state->num_text_display_lines();
    for (int i = 0; i < n; ++i)
    {
        if (!spans_at(state->line_scroller, i).empty())
        {
            state->ws_line = i;
            state->ws_word = 0;
            state->needs_render = true;
            return;
        }
    }
}

void TokenView::ws_recentre()
{
    // Pull the cursor back to the middle row by scrolling the page under it. scroll_reporting
    // returns what actually moved, which is less than asked at the start and end of a
    // document -- and that is the entire edge behaviour: the cursor keeps the rows the page
    // could not give back, so it walks the first and last half-page itself.
    const int want = ws_camera::scroll_for(state->ws_line, state->num_text_display_lines());
    if (want == 0)
    {
        return;
    }
    const int moved = scroll_reporting(want);
    state->ws_line = ws_camera::cursor_after(state->ws_line, moved);
    state->needs_render = true;
}

void TokenView::ws_page(int dir)
{
    // A page jump is a cursor move like any other: shift the cursor half a page and let the
    // camera scroll to follow, so there is only one rule for how the page moves.
    const int step = state->half_page();
    for (int i = 0; i < step; ++i)
    {
        if (!ws_walk(dir))
        {
            break;
        }
    }
    auto spans = spans_at(state->line_scroller, state->ws_line);
    state->ws_word = std::min(state->ws_word, std::max(0, (int)spans.size() - 1));
    ws_recentre();
}

void TokenView::ws_handle_key(SDLKey key)
{
    switch (key)
    {
        case SW_BTN_LEFT:  ws_move_word(-1); break;
        case SW_BTN_RIGHT: ws_move_word(1); break;
        case SW_BTN_UP:    ws_move_line(-1); break;
        case SW_BTN_DOWN:  ws_move_line(1); break;

        // A always means "what does this word mean", in a book and in an article alike.
        // X follows a link, and exists only where there are links to follow. What a button
        // does depends on the document, not on whether a callback happens to be set --
        // wiring one up must never silently rebind a button.
        case SW_BTN_A:
            ws_open_selected();
            break;
        case SW_BTN_X:
            if (state->follows_links()) { ws_follow_selected(); }
            break;

        default: break;
    }
}

void TokenView::ws_move_word(int dir)
{
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
        ws_recentre();
    }
}

void TokenView::ws_move_line(int dir)
{
    // The next line, landing on its first word. Deliberately the dullest rule available:
    // the previous two attempts each tried to be clever and each wandered. Walking to the
    // "nearest line with words" crossed paragraph blanks and travelled two lines or more;
    // scrolling one line and then re-placing the cursor near the middle let it land on
    // whichever line happened to sit closest, which is not the same line twice running.
    //
    // Here the cursor moves one word-bearing line and the camera follows it, so the only
    // thing that varies is how far the page scrolls -- and that is the camera's job.
    if (!ws_walk(dir))
    {
        // Nothing in that direction within a page: a full-page image, or a plate section.
        // Scroll anyway, or such a book cannot be moved through at all.
        if (scroll_reporting(dir * state->half_page()) != 0)
        {
            ws_seed();
            state->needs_render = true;
        }
        return;
    }

    state->ws_word = 0;
    ws_recentre();
    state->needs_render = true;
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

// The line and word span currently under the cursor, or false if there is none.
// The word under the cursor as the dictionary should see it. A line broken at a
// discretionary hyphen splits one word across two lines, and looking up the halves gives
// the wrong answer twice over: "con-danna" would resolve "con", a real preposition, and
// hide that the word is "condanna". TextLine keeps the break as a flag rather than splicing
// a hyphen into the text, so the two halves join cleanly.
std::string TokenView::ws_selected_surface() const
{
    const TextLine *tl = nullptr;
    WordSpan sp{};
    if (!ws_selected_span(&tl, &sp))
    {
        return "";
    }

    std::string surface = tl->text.substr(sp.start, sp.end - sp.start);

    // The continuation half: the cursor is on the first word of a line whose predecessor
    // broke at a hyphen, so the word began there. Joining only forwards left this case
    // resolving the tail on its own -- "teso" of "es-teso" looked up as a word in its own
    // right -- and moving forwards through a page lands on the tail far more often than on
    // the head.
    if (sp.start == 0 && state->ws_line > 0)
    {
        const DisplayLine *prev = state->line_scroller.get_line_relative(state->ws_line - 1);
        if (prev != nullptr && prev->type == DisplayLine::Type::Text)
        {
            const auto *prev_tl = static_cast<const TextLine *>(prev);
            if (prev_tl->trailing_hyphen)
            {
                const auto prev_spans = tokenize_words(prev_tl->text);
                if (!prev_spans.empty() && prev_spans.back().end == prev_tl->text.size())
                {
                    const auto &head = prev_spans.back();
                    return prev_tl->text.substr(head.start, head.end - head.start) + surface;
                }
            }
        }
    }

    // Only the last word of a hyphenated line continues onto the next one.
    if (!tl->trailing_hyphen || sp.end != tl->text.size())
    {
        return surface;
    }

    const DisplayLine *next = state->line_scroller.get_line_relative(state->ws_line + 1);
    if (next == nullptr || next->type != DisplayLine::Type::Text)
    {
        return surface;
    }
    const auto *next_tl = static_cast<const TextLine *>(next);
    const auto next_spans = tokenize_words(next_tl->text);
    if (next_spans.empty() || next_spans.front().start != 0)
    {
        // The continuation must start the line; anything else means this was a real hyphen
        // in the source, not a break the layout introduced.
        return surface;
    }

    return surface + next_tl->text.substr(
        next_spans.front().start, next_spans.front().end - next_spans.front().start);
}

bool TokenView::ws_selected_span(const TextLine **out_line, WordSpan *out_span) const
{
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

    state->on_open_word(ws_selected_surface());
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
    // Follows or does nothing. Nothing is honest here rather than surprising, because a
    // link is tinted and a selected link takes its own highlight colour -- the reader can
    // see which words this button acts on before pressing it.
    const LinkRun *link = link_run_overlapping(tl->link_runs, sp.start, sp.end);
    if (link != nullptr && state->on_follow_link)
    {
        state->on_follow_link(link->target);
    }
}
