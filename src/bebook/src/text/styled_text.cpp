#include "./styled_text.h"

#include "./face_cache.h"
#include "./hyphenate.h"
#include "./line_break.h"
#include "./shaper.h"
#include "./text_render.h"
#include "util/str_utils.h"

#include <algorithm>

namespace text
{

Style StyledText::style_at(uint32_t offset) const
{
    if (!runs || runs->empty())
    {
        return Style::Regular;
    }

    // Runs are sorted and non-overlapping, so a binary search on start offset finds the
    // containing run. Line breaking calls this often enough to be worth it.
    auto it = std::upper_bound(
        runs->begin(), runs->end(), offset,
        [](uint32_t value, const StyleRun &run) { return value < run.offset; }
    );
    if (it == runs->begin())
    {
        return Style::Regular;
    }
    --it;
    return (offset < it->offset + it->length) ? it->style : Style::Regular;
}

namespace
{

// Calls `visit(style, chunk_offset, chunk_length)` for each maximal single-style piece
// of [offset, offset+length).
template <typename Visit>
void for_each_style_chunk(const StyledText &text, uint32_t offset, uint32_t length, Visit visit)
{
    const uint32_t end = std::min<uint32_t>(offset + length, text.length);
    uint32_t pos = std::min(offset, text.length);

    while (pos < end)
    {
        const Style style = text.style_at(pos);

        uint32_t chunk_end = pos + 1;
        while (chunk_end < end && text.style_at(chunk_end) == style)
        {
            ++chunk_end;
        }

        visit(style, pos, chunk_end - pos);
        pos = chunk_end;
    }
}

} // namespace

Fixed measure_styled(const StyledText &text, uint32_t offset, uint32_t length)
{
    if (!text.text || length == 0)
    {
        return 0;
    }

    if (!text.runs || text.runs->empty())
    {
        return Engine::instance().shaper().measure(
            text.family, Style::Regular, text.size_px, text.text + offset, length
        );
    }

    Fixed total = 0;
    for_each_style_chunk(text, offset, length, [&](Style style, uint32_t o, uint32_t l) {
        total += Engine::instance().shaper().measure(
            text.family, style, text.size_px, text.text + o, l
        );
    });
    return total;
}

void shape_styled(
    const StyledText &text,
    uint32_t offset,
    uint32_t length,
    std::vector<PositionedGlyph> &out
)
{
    if (!text.text || length == 0)
    {
        return;
    }

    for_each_style_chunk(text, offset, length, [&](Style style, uint32_t o, uint32_t l) {
        Engine::instance().shaper().shape(
            text.family, style, text.size_px, text.text + o, l, o, out
        );
    });
}

int draw_styled_line(
    SDL_Surface *dst,
    const StyledText &text,
    uint32_t offset,
    uint32_t length,
    Fixed extra_total,
    uint32_t gaps,
    bool trailing_hyphen,
    int x,
    int baseline_y,
    SDL_Color fg,
    SDL_Color bg,
    const SDL_Rect *clip
)
{
    if (!dst || !text.text || length == 0)
    {
        return x;
    }

    std::vector<PositionedGlyph> glyphs;
    shape_styled(text, offset, length, glyphs);

    if (gaps > 0 && extra_total != 0)
    {
        // Split the adjustment exactly: every gap gets the same whole number of 1/64px
        // units and the leftover units are handed out one per gap from the left, so the
        // widths differ by at most 1/64px and their sum is exactly `extra_total`. The
        // line therefore ends precisely on the column edge, which is not achievable when
        // glyphs can only be positioned on whole pixels.
        const Fixed per_gap = extra_total / static_cast<Fixed>(gaps);
        Fixed remainder = extra_total - per_gap * static_cast<Fixed>(gaps);
        const Fixed remainder_step = remainder >= 0 ? 1 : -1;

        for (auto &g : glyphs)
        {
            if (g.cluster >= text.length || !is_whitespace(text.text[g.cluster]))
            {
                continue;
            }

            g.x_advance += per_gap;
            if (remainder != 0)
            {
                g.x_advance += remainder_step;
                remainder -= remainder_step;
            }
        }
    }

    if (trailing_hyphen)
    {
        // Shaped in the style the line ends in, so an italic passage hyphenates with an
        // italic hyphen. The hyphen exists only in the rendered line and never in the
        // source, so document addressing is unaffected by where the break fell.
        const uint32_t last = offset + length > 0 ? offset + length - 1 : offset;
        Engine::instance().shaper().shape(
            text.family, text.style_at(last), text.size_px, "-", 1, 0, glyphs
        );
    }

    TextPalette palette;
    palette.build(dst->format, fg, bg);

    const Fixed end = draw_glyphs_shaded(
        dst, glyphs, int_to_fixed(x), baseline_y,
        Engine::instance().atlas(), text.size_px, palette, clip
    );
    return fixed_round(end);
}

std::vector<StyleRun> slice_runs(
    const std::vector<StyleRun> &runs,
    uint32_t offset,
    uint32_t length
)
{
    std::vector<StyleRun> out;
    const uint32_t end = offset + length;

    for (const auto &run : runs)
    {
        const uint32_t run_end = run.offset + run.length;
        if (run_end <= offset || run.offset >= end)
        {
            continue;
        }

        const uint32_t clipped_start = std::max(run.offset, offset);
        const uint32_t clipped_end = std::min(run_end, end);
        out.push_back(StyleRun{clipped_start - offset, clipped_end - clipped_start, run.style});
    }

    normalize_runs(out);
    return out;
}

void normalize_runs(std::vector<StyleRun> &runs)
{
    std::vector<StyleRun> out;
    out.reserve(runs.size());

    for (const auto &run : runs)
    {
        if (run.length == 0)
        {
            continue;
        }
        if (!out.empty()
            && out.back().style == run.style
            && out.back().offset + out.back().length == run.offset)
        {
            out.back().length += run.length;
            continue;
        }
        out.push_back(run);
    }

    runs.swap(out);
}

std::vector<Line> layout_paragraph(
    const StyledText &st,
    const Font *font,
    int width,
    Align align,
    bool hyphenate,
    Fixed first_line_indent
)
{
    BreakOptions options;
    options.line_width = int_to_fixed(width);
    options.hyphenate = hyphenate;
    options.hyphen_width = text_width(font, "-", 1);
    options.first_line_indent = first_line_indent;

    // Measured through the styled path, so a range spanning a switch to italic is summed
    // across both faces rather than measured entirely in the regular one.
    std::vector<Line> lines = break_paragraph(
        st.text, st.length,
        [&st](uint32_t offset, uint32_t length) { return measure_styled(st, offset, length); },
        options,
        hyphenate ? HyphenateFn(hyphenate_word) : nullptr
    );

    // Only justified setting stretches lines; Left/Center are ragged. break_paragraph
    // already leaves the last line ragged, so this only affects the interior lines.
    if (align != Align::Justify)
    {
        for (auto &line : lines)
        {
            line.target_width = line.natural_width;
        }
    }
    return lines;
}

int aligned_left_x(Align align, int left_x, int width, Fixed natural)
{
    if (align == Align::Center)
    {
        return left_x + (width - fixed_round(natural)) / 2;
    }
    return left_x;
}

int pen_x_at(
    const StyledText &st,
    const Line &ln,
    uint32_t byte_offset,
    int left_x,
    int width,
    Align align
)
{
    const int base = aligned_left_x(align, left_x, width, ln.natural_width);
    const Fixed prefix = measure_styled(st, ln.offset, byte_offset - ln.offset);

    // Justification opens the inter-word gaps: every whitespace before `byte_offset` gets an
    // equal share of the stretch plus, from the left, one 1/64px unit of the remainder --
    // the same distribution draw_styled_line applies to the glyph advances.
    Fixed extra_before = 0;
    if (align == Align::Justify && ln.stretch_gaps > 0)
    {
        const Fixed extra_total = ln.target_width - ln.natural_width;
        if (extra_total != 0)
        {
            const Fixed per_gap = extra_total / static_cast<Fixed>(ln.stretch_gaps);
            const Fixed remainder = extra_total - per_gap * static_cast<Fixed>(ln.stretch_gaps);
            const Fixed rmag = remainder >= 0 ? remainder : -remainder;
            const Fixed rstep = remainder >= 0 ? 1 : -1;

            uint32_t ws_before = 0;
            for (uint32_t i = ln.offset; i < byte_offset && i < st.length; ++i)
            {
                if (is_whitespace(st.text[i])) ++ws_before;
            }
            const Fixed getting = std::min<Fixed>(static_cast<Fixed>(ws_before), rmag);
            extra_before = per_gap * static_cast<Fixed>(ws_before) + rstep * getting;
        }
    }

    return base + fixed_round(prefix + extra_before);
}

int draw_line_aligned(
    SDL_Surface *dst,
    const StyledText &st,
    const Line &ln,
    int left_x,
    int width,
    int baseline_y,
    Align align,
    SDL_Color fg,
    SDL_Color bg,
    const SDL_Rect *clip
)
{
    const int x = aligned_left_x(align, left_x, width, ln.natural_width);
    Fixed extra_total = 0;
    uint32_t gaps = 0;
    if (align == Align::Justify)
    {
        extra_total = ln.target_width - ln.natural_width;
        gaps = ln.stretch_gaps;
    }

    return draw_styled_line(
        dst, st, ln.offset, ln.length,
        extra_total, gaps, ln.trailing_hyphen,
        x, baseline_y, fg, bg, clip
    );
}

} // namespace text
