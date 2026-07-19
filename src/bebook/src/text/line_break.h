#ifndef LINE_BREAK_H_
#define LINE_BREAK_H_

#include "./text_types.h"

#include <functional>
#include <vector>

namespace text
{

// One laid-out line of a paragraph.
//
// `offset`/`length` slice the paragraph's UTF-8, so the caller can still recover the
// exact source text of a line. That matters beyond rendering: the reader derives
// document addresses by counting the non-whitespace characters a line consumed, so the
// source slice has to survive layout intact. A hyphen inserted at a break is therefore
// recorded as a flag rather than spliced into the text.
struct Line
{
    uint32_t offset;
    uint32_t length;
    bool trailing_hyphen;

    // Natural width of the slice, and the width the line should be stretched to when
    // justifying. Equal for the last line of a paragraph and for ragged setting.
    Fixed natural_width;
    Fixed target_width;

    // Number of inter-word gaps available to absorb stretch.
    uint32_t stretch_gaps;
};

struct BreakOptions
{
    Fixed line_width = 0;

    // Extra indent applied to the first line only.
    Fixed first_line_indent = 0;

    bool hyphenate = true;

    // Knuth-Plass tuning. Defaults follow TeX's own.
    int line_penalty = 10;         // per-line cost, discourages ragged paragraphs
    int hyphen_penalty = 50;       // cost of breaking at a discretionary hyphen
    int double_hyphen_demerits = 3000;   // two hyphenated lines in a row
    int final_hyphen_demerits = 5000;    // hyphen on the second-to-last line
    int adjacent_fitness_demerits = 100; // very loose line beside a very tight one

    // Maximum tolerated stretch ratio, i.e. the multiple of its designed stretchability
    // by which a line's spaces may be opened up. The first pass insists on
    // `pretolerance` and only falls back to `tolerance` if the paragraph cannot be set
    // that tightly.
    double pretolerance = 1.0;
    double tolerance = 2.5;

    // Width of the hyphen the renderer will append when a discretionary break is taken.
    // Without it, hyphenated lines are scored one hyphen too narrow and overrun.
    Fixed hyphen_width = 0;

    // Stretchability attributed to a line that contains no inter-word space at all --
    // a single long word, which happens constantly at this column width. Such a line has
    // literally nothing to stretch, so its badness would otherwise be infinite and the
    // breakpoint would be rejected, leaving the paragraph with no feasible solution.
    // Scoring it against a nominal stretchability keeps it legal but expensive, so it is
    // chosen only when there is no alternative. Defaults to a quarter of the column.
    Fixed emergency_stretch = 0;
};

// Measures the width of a byte range of the paragraph, in 26.6.
using MeasureFn = std::function<Fixed(uint32_t offset, uint32_t length)>;

// Returns byte offsets within a word after which a hyphen may be inserted. Passed in
// rather than called directly so line breaking stays testable without the pattern
// tables, and so the language could be varied later.
using HyphenateFn = std::function<std::vector<uint16_t>(const char *word, uint32_t length)>;

// Breaks a paragraph into lines using Knuth and Plass's total-fit algorithm: rather
// than greedily filling each line, it considers every legal set of breakpoints and
// minimises the sum of squared badness over the whole paragraph. This is what stops one
// tight line being followed by a conspicuously loose one, which greedy first-fit --
// what this reader used before -- cannot avoid because it never reconsiders.
//
// Falls back to greedy breaking if no set of breakpoints satisfies `tolerance`, so a
// pathological paragraph (a long URL, say) still renders rather than throwing.
std::vector<Line> break_paragraph(
    const char *text,
    uint32_t length,
    const MeasureFn &measure,
    const BreakOptions &options,
    const HyphenateFn &hyphenate = nullptr
);

} // namespace text

#endif
