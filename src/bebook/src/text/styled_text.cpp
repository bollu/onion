#include "./styled_text.h"

#include "./face_cache.h"
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

} // namespace text
