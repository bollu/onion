#ifndef STYLED_TEXT_H_
#define STYLED_TEXT_H_

#include "./font.h"
#include "./line_break.h"
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

// Horizontal alignment for a laid-out paragraph.
enum class Align { Left, Center, Justify };

// Break a paragraph (the bytes/style in `st`) into lines at `width` px, hyphenating if
// asked. Justify leaves each non-final line stretched to `width` (target_width); Left and
// Center leave lines ragged (target_width == natural_width). This is the shared layout
// glue the reader page and the dictionary popup both use, so wrapping/hyphenation behave
// identically everywhere.
std::vector<Line> layout_paragraph(
    const StyledText &st,
    const Font *font,
    int width,
    Align align,
    bool hyphenate,
    Fixed first_line_indent = 0
);

// Left pen-x for a line of natural width `natural` (26.6) set within [left_x, left_x+width]
// under `align`. Center places it in the middle; Left and Justify pen at left_x. Exposed so
// the alignment math is unit-testable without a font.
int aligned_left_x(Align align, int left_x, int width, Fixed natural);

// On-screen x of the cluster boundary at absolute byte `byte_offset` within a laid-out
// line, under `align` inside [left_x, left_x+width]. For Justify this accounts for the
// inter-word stretch distributed before the offset, exactly as draw_line_aligned/
// draw_styled_line position the glyphs. Used to hit-test a word span for highlighting so
// that geometry and drawing share one implementation of the justification math.
int pen_x_at(
    const StyledText &st,
    const Line &ln,
    uint32_t byte_offset,
    int left_x,
    int width,
    Align align
);

// Draw one laid-out line inside [left_x, left_x + width] at glyph baseline `baseline_y`,
// applying `align`: Left pens at left_x; Center pens at left_x + (width - natural)/2;
// Justify opens the inter-word gaps to target_width. Thin wrapper over draw_styled_line;
// returns the x it ended at.
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
