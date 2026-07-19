#ifndef STYLED_TEXT_H_
#define STYLED_TEXT_H_

#include "./font.h"
#include "./text_types.h"

#include <SDL/SDL.h>

#include <vector>

namespace text
{

// A paragraph plus the styling of its parts.
//
// Style is carried as runs over the paragraph's own bytes rather than by splitting the
// text into styled pieces, because everything downstream -- line breaking, hyphenation,
// and the reader's document addressing -- works on contiguous byte offsets into one
// string. Splitting would force all of them to reassemble it.
struct StyledText
{
    const char *text = nullptr;
    uint32_t length = 0;

    // Sorted, non-overlapping, covering the whole paragraph. Empty means all Regular,
    // which is the common case and costs nothing.
    const std::vector<StyleRun> *runs = nullptr;

    int family = 0;
    uint32_t size_px = 0;

    Style style_at(uint32_t offset) const;
};

// Exact width of a byte range, in 26.6, summed across whatever styles it spans.
Fixed measure_styled(const StyledText &text, uint32_t offset, uint32_t length);

// Shapes a byte range, restarting shaping at each style boundary. Clusters are absolute
// offsets into the paragraph.
void shape_styled(
    const StyledText &text,
    uint32_t offset,
    uint32_t length,
    std::vector<PositionedGlyph> &out
);

// Draws one laid-out line, applying justification and the hyphen a discretionary break
// introduced. Subsumes the single-style path: a StyledText with no runs behaves exactly
// as before.
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
    const SDL_Rect *clip = nullptr
);

// Restricts `runs` to [offset, offset+length), rebasing offsets to the slice. Used to
// hand each display line only the styling it needs.
std::vector<StyleRun> slice_runs(
    const std::vector<StyleRun> &runs,
    uint32_t offset,
    uint32_t length
);

// Collapses adjacent runs of equal style and drops empty ones.
void normalize_runs(std::vector<StyleRun> &runs);

} // namespace text

#endif
