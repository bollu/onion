#include "./line_break.h"

#include "util/str_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace text
{

namespace
{

constexpr int INFINITE_PENALTY = 10000;
constexpr int FORCED_BREAK = -10000;

// Knuth's model of a paragraph: an alternating sequence of boxes (unbreakable material),
// glue (stretchable space) and penalties (discretionary break points).
enum class ItemType
{
    Box,
    Glue,
    Penalty,
};

struct Item
{
    ItemType type = ItemType::Box;

    Fixed width = 0;
    Fixed stretch = 0;   // glue only
    Fixed shrink = 0;    // glue only

    int penalty = 0;     // penalty only
    bool flagged = false; // penalty only: a break here inserts a hyphen

    // Byte range of the source this item covers. Glue covers the whitespace run;
    // penalties are zero-length markers sitting between two boxes.
    uint32_t offset = 0;
    uint32_t length = 0;
};

// Fitness classes exist so that a very loose line is not placed next to a very tight
// one, which reads worse than either on its own.
int fitness_class(double ratio)
{
    if (ratio < -0.5) return 0;  // tight
    if (ratio < 0.5)  return 1;  // normal
    if (ratio < 1.0)  return 2;  // loose
    return 3;                    // very loose
}

double badness_of(double ratio)
{
    const double r = std::fabs(ratio);
    return 100.0 * r * r * r;
}

struct Node
{
    size_t item_index;   // breakpoint position
    uint32_t line;       // number of lines before this break
    int fitness;
    bool flagged;        // the break here is a hyphen

    Fixed total_width;
    Fixed total_stretch;
    Fixed total_shrink;

    double demerits;     // best known total demerits to reach this break
    int previous;        // index into the node pool, -1 for the paragraph start
};

bool is_space_byte(char c)
{
    return is_whitespace(c);
}

} // namespace

// Turns the paragraph into the box/glue/penalty sequence. Word widths come from
// `measure`; space stretchability follows the usual typographic proportions of the
// space's own width, since SDL-era bitmap fonts do not expose TeX's fontdimen values.
static std::vector<Item> build_items(
    const char *text,
    uint32_t length,
    const MeasureFn &measure,
    const BreakOptions &options,
    const HyphenateFn &hyphenate
)
{
    std::vector<Item> items;
    items.reserve(length / 4 + 8);

    uint32_t i = 0;
    while (i < length)
    {
        if (is_space_byte(text[i]))
        {
            const uint32_t start = i;
            while (i < length && is_space_byte(text[i]))
            {
                ++i;
            }

            const Fixed w = measure(start, i - start);

            Item glue;
            glue.type = ItemType::Glue;
            glue.width = w;
            // A space may grow by half its width and shrink by a third: the classic
            // proportions, tight enough that justification never opens visible rivers.
            glue.stretch = w / 2;
            glue.shrink = w / 3;
            glue.offset = start;
            glue.length = i - start;
            items.push_back(glue);
            continue;
        }

        const uint32_t word_start = i;
        while (i < length && !is_space_byte(text[i]))
        {
            ++i;
        }
        const uint32_t word_length = i - word_start;

        std::vector<uint16_t> breaks;
        if (options.hyphenate && hyphenate && word_length >= 5)
        {
            breaks = hyphenate(text + word_start, word_length);
        }

        // Emit the word as boxes separated by flagged penalties at each hyphen point.
        uint32_t piece_start = word_start;
        for (uint16_t b : breaks)
        {
            const uint32_t split = word_start + b;
            if (split <= piece_start || split >= word_start + word_length)
            {
                continue;
            }

            Item box;
            box.type = ItemType::Box;
            box.offset = piece_start;
            box.length = split - piece_start;
            box.width = measure(box.offset, box.length);
            items.push_back(box);

            Item pen;
            pen.type = ItemType::Penalty;
            pen.penalty = options.hyphen_penalty;
            pen.flagged = true;
            pen.offset = split;
            pen.length = 0;
            // The hyphen only occupies space when the break is actually taken, which is
            // precisely what a penalty item's width means: it counts towards the line
            // only if the line ends here.
            pen.width = options.hyphen_width;
            items.push_back(pen);

            piece_start = split;
        }

        Item box;
        box.type = ItemType::Box;
        box.offset = piece_start;
        box.length = word_start + word_length - piece_start;
        box.width = measure(box.offset, box.length);
        items.push_back(box);
    }

    // Forced break at the paragraph end, preceded by infinitely stretchable glue so the
    // last line is set ragged rather than justified.
    Item finish_glue;
    finish_glue.type = ItemType::Glue;
    finish_glue.width = 0;
    finish_glue.stretch = int_to_fixed(10000);
    finish_glue.shrink = 0;
    finish_glue.offset = length;
    finish_glue.length = 0;
    items.push_back(finish_glue);

    Item finish;
    finish.type = ItemType::Penalty;
    finish.penalty = FORCED_BREAK;
    finish.flagged = false;
    finish.offset = length;
    finish.length = 0;
    items.push_back(finish);

    return items;
}

namespace
{

bool is_legal_breakpoint(const std::vector<Item> &items, size_t index)
{
    const Item &item = items[index];
    if (item.type == ItemType::Penalty)
    {
        return item.penalty < INFINITE_PENALTY;
    }
    if (item.type == ItemType::Glue)
    {
        // Glue is only a breakpoint when it follows actual material.
        return index > 0 && items[index - 1].type == ItemType::Box;
    }
    return false;
}

// Running totals measured just after a break at `index`, i.e. skipping the glue and
// penalties that would be discarded at the start of the next line.
void totals_after_break(
    const std::vector<Item> &items,
    size_t index,
    Fixed &width,
    Fixed &stretch,
    Fixed &shrink
)
{
    for (size_t i = index; i < items.size(); ++i)
    {
        const Item &item = items[i];
        if (item.type == ItemType::Box)
        {
            break;
        }
        if (item.type == ItemType::Penalty && i != index)
        {
            break;
        }
        if (item.type == ItemType::Glue)
        {
            width += item.width;
            stretch += item.stretch;
            shrink += item.shrink;
        }
    }
}

} // namespace

// Greedy first-fit, used when no set of breakpoints meets the tolerance. Guarantees
// something renders for pathological input such as an unbreakable URL wider than the
// column.
static std::vector<Line> break_greedy(
    const std::vector<Item> &items,
    const BreakOptions &options
)
{
    std::vector<Line> lines;

    size_t line_start_item = 0;
    Fixed width = 0;
    uint32_t gaps = 0;

    // Width and gap count as they stood *before* the last legal breakpoint, so that the
    // emitted line excludes the space it broke at.
    size_t last_glue = std::string::npos;
    Fixed width_at_last_glue = 0;
    uint32_t gaps_at_last_glue = 0;

    auto target_for = [&](size_t line_index) {
        return options.line_width - (line_index == 0 ? options.first_line_indent : 0);
    };

    auto emit = [&](size_t break_item, Fixed natural, uint32_t stretch_gaps) {
        const uint32_t start = items[line_start_item].offset;
        const uint32_t end = items[break_item].offset;
        Line line;
        line.offset = start;
        line.length = end > start ? end - start : 0;
        // The fallback never hyphenates: it exists for input the optimal pass could not
        // set at all, where predictability matters more than tightness.
        line.trailing_hyphen = false;
        line.natural_width = natural;
        line.target_width = natural;  // always ragged
        line.stretch_gaps = stretch_gaps;
        lines.push_back(line);
    };

    for (size_t i = 0; i < items.size(); ++i)
    {
        const Item &item = items[i];

        if (item.type == ItemType::Penalty && item.penalty == FORCED_BREAK)
        {
            emit(i, width, gaps);
            break;
        }

        if (item.type == ItemType::Glue && is_legal_breakpoint(items, i))
        {
            last_glue = i;
            width_at_last_glue = width;
            gaps_at_last_glue = gaps;
        }

        const Fixed next_width = width + item.width;
        if (next_width > target_for(lines.size()) && last_glue != std::string::npos && last_glue > line_start_item)
        {
            emit(last_glue, width_at_last_glue, gaps_at_last_glue);

            line_start_item = last_glue + 1;
            i = last_glue;
            width = 0;
            gaps = 0;
            last_glue = std::string::npos;
            continue;
        }

        width = next_width;
        if (item.type == ItemType::Glue)
        {
            ++gaps;
        }
    }

    return lines;
}

// One total-fit pass at a given tolerance. `emergency_stretch` is added to every line's
// stretchability, uniformly -- applying it only to lines that lack glue would make a
// full line score worse than a nearly empty one, since the two would be measured against
// different denominators.
//
// Returns false if no set of breakpoints satisfies `tolerance`, leaving `out` untouched.
static bool try_total_fit(
    const std::vector<Item> &items,
    const BreakOptions &options,
    double tolerance,
    Fixed emergency_stretch,
    std::vector<Line> &out
)
{
    std::vector<Node> nodes;
    nodes.reserve(64);
    nodes.push_back(Node{0, 0, 1, false, 0, 0, 0, 0.0, -1});

    std::vector<int> active;
    active.push_back(0);

    Fixed sum_width = 0, sum_stretch = 0, sum_shrink = 0;

    auto target_for_line = [&](uint32_t line_index) {
        return options.line_width - (line_index == 0 ? options.first_line_indent : 0);
    };

    int best_final = -1;

    for (size_t b = 0; b < items.size(); ++b)
    {
        const Item &item = items[b];

        if (is_legal_breakpoint(items, b))
        {
            // Candidate best break per fitness class, as in the original paper: keeping
            // one node per class is what stops the active list growing without bound.
            struct Candidate { int node = -1; double demerits = 0; double ratio = 0; };
            Candidate best[4];
            bool have[4] = {false, false, false, false};

            for (size_t ai = 0; ai < active.size();)
            {
                const int node_index = active[ai];
                const Node &a = nodes[node_index];

                Fixed line_w = sum_width - a.total_width;
                if (item.type == ItemType::Penalty)
                {
                    line_w += item.width;
                }

                const Fixed target = target_for_line(a.line);
                const Fixed stretch = sum_stretch - a.total_stretch;
                const Fixed shrink = sum_shrink - a.total_shrink;

                double ratio;
                if (line_w < target)
                {
                    // A line holding a single long word has no inter-word space at
                    // all, so without emergency stretch its badness is infinite and the
                    // breakpoint is rejected -- which can leave a narrow column with no
                    // feasible solution at all. The allowance is added to every line so
                    // that fuller lines still score better than emptier ones.
                    const Fixed effective = stretch + emergency_stretch;
                    ratio = effective > 0
                        ? static_cast<double>(target - line_w) / effective
                        : std::numeric_limits<double>::infinity();
                }
                else if (line_w > target)
                {
                    ratio = shrink > 0
                        ? static_cast<double>(target - line_w) / shrink
                        : -std::numeric_limits<double>::infinity();
                }
                else
                {
                    ratio = 0.0;
                }

                const bool forced = item.type == ItemType::Penalty && item.penalty == FORCED_BREAK;

                // A line that cannot shrink enough is hopeless, and so is every longer
                // line from the same node: retire it.
                if (ratio < -1.0 || forced)
                {
                    active.erase(active.begin() + static_cast<long>(ai));
                }
                else
                {
                    ++ai;
                }

                if (ratio >= -1.0 && ratio <= tolerance)
                {
                    double d = options.line_penalty + badness_of(ratio);
                    d = d * d;

                    if (item.type == ItemType::Penalty)
                    {
                        if (item.penalty > 0)
                        {
                            d += static_cast<double>(item.penalty) * item.penalty;
                        }
                        else if (item.penalty > FORCED_BREAK)
                        {
                            d -= static_cast<double>(item.penalty) * item.penalty;
                        }
                    }

                    if (item.flagged && a.flagged)
                    {
                        d += options.double_hyphen_demerits;
                    }

                    const int fit = fitness_class(ratio);
                    if (std::abs(fit - a.fitness) > 1)
                    {
                        d += options.adjacent_fitness_demerits;
                    }

                    d += a.demerits;

                    if (!have[fit] || d < best[fit].demerits)
                    {
                        have[fit] = true;
                        best[fit].node = node_index;
                        best[fit].demerits = d;
                        best[fit].ratio = ratio;
                    }
                }
            }

            for (int fit = 0; fit < 4; ++fit)
            {
                if (!have[fit])
                {
                    continue;
                }

                Fixed w = sum_width, s = sum_stretch, sh = sum_shrink;
                totals_after_break(items, b, w, s, sh);

                Node node;
                node.item_index = b;
                node.line = nodes[best[fit].node].line + 1;
                node.fitness = fit;
                node.flagged = item.flagged;
                node.total_width = w;
                node.total_stretch = s;
                node.total_shrink = sh;
                node.demerits = best[fit].demerits;
                node.previous = best[fit].node;

                nodes.push_back(node);
                const int new_index = static_cast<int>(nodes.size()) - 1;

                const bool forced = item.type == ItemType::Penalty && item.penalty == FORCED_BREAK;
                if (forced)
                {
                    if (best_final < 0 || node.demerits < nodes[best_final].demerits)
                    {
                        best_final = new_index;
                    }
                }
                else
                {
                    active.push_back(new_index);
                }
            }
        }

        // A penalty's width is the material that appears *only if* the line ends there --
        // the hyphen. It must not enter the running total, or every line following a
        // hyphenation point is measured wider than it really is, by one phantom hyphen
        // per point passed. It is added instead when scoring a break at that penalty.
        if (item.type != ItemType::Penalty)
        {
            sum_width += item.width;
        }
        if (item.type == ItemType::Glue)
        {
            sum_stretch += item.stretch;
            sum_shrink += item.shrink;
        }
    }

    if (best_final < 0)
    {
        return false;
    }

    // Walk the chain of chosen breaks back to the paragraph start.
    std::vector<int> chain;
    for (int n = best_final; n > 0; n = nodes[n].previous)
    {
        chain.push_back(n);
    }
    std::reverse(chain.begin(), chain.end());

    uint32_t line_index = 0;
    size_t start_item = 0;

    for (int n : chain)
    {
        const Node &node = nodes[n];
        const size_t break_item = node.item_index;

        const uint32_t start_offset = items[start_item].offset;
        const uint32_t end_offset = items[break_item].offset;

        Fixed natural = 0;
        uint32_t gaps = 0;
        for (size_t i = start_item; i < break_item; ++i)
        {
            // Penalties passed over contribute nothing; only one taken at the end of the
            // line does, and that is added below.
            if (items[i].type != ItemType::Penalty)
            {
                natural += items[i].width;
            }
            if (items[i].type == ItemType::Glue)
            {
                ++gaps;
            }
        }
        if (items[break_item].type == ItemType::Penalty)
        {
            natural += items[break_item].width;
        }

        Line line;
        line.offset = start_offset;
        line.length = end_offset > start_offset ? end_offset - start_offset : 0;
        line.trailing_hyphen = items[break_item].type == ItemType::Penalty && items[break_item].flagged;
        line.natural_width = natural;
        line.stretch_gaps = gaps;

        // The final line of a paragraph is never stretched; nor is a line with no gaps
        // to stretch, which would otherwise be letter-spaced.
        const bool is_last = (n == chain.back());
        line.target_width = (is_last || gaps == 0)
            ? natural
            : target_for_line(line_index);

        out.push_back(line);

        ++line_index;
        start_item = break_item;
        // Glue at a break is discarded rather than starting the next line.
        while (start_item < items.size()
               && (items[start_item].type == ItemType::Glue
                   || items[start_item].type == ItemType::Penalty))
        {
            ++start_item;
        }
    }

    return !out.empty();
}

std::vector<Line> break_paragraph(
    const char *text,
    uint32_t length,
    const MeasureFn &measure,
    const BreakOptions &options,
    const HyphenateFn &hyphenate
)
{
    std::vector<Line> lines;
    if (!text || length == 0 || options.line_width <= 0)
    {
        lines.push_back(Line{0, 0, false, 0, 0, 0});
        return lines;
    }

    // TeX's escalation, and for the same reason: insist on a tight setting first, and
    // only relax once it is clear the paragraph cannot be set that way. Each pass is
    // internally consistent, so lines within one layout are always comparable.
    const Fixed emergency = options.emergency_stretch > 0
        ? options.emergency_stretch
        : options.line_width / 4;

    const bool can_hyphenate = options.hyphenate && hyphenate != nullptr;

    // Discretionary breaks are offered from the first pass rather than being held back
    // for a retry, which is where TeX puts them. TeX can afford to: its default measure
    // is wide enough that most paragraphs set acceptably without breaking any word. At
    // this screen's ~60 characters they frequently do not, and deferring hyphenation
    // means the tight first pass succeeds with visibly loose spacing and the hyphenated
    // pass is never reached. Offering them throughout costs nothing, because
    // hyphen_penalty already makes the breaker take one only where it genuinely pays.
    const std::vector<Item> items = build_items(
        text, length, measure, options, can_hyphenate ? hyphenate : nullptr
    );

    // Pass 1: tight spacing. Most prose is set here.
    if (try_total_fit(items, options, options.pretolerance, 0, lines))
    {
        return lines;
    }

    // Pass 2: allow looser spacing.
    lines.clear();
    if (try_total_fit(items, options, options.tolerance, 0, lines))
    {
        return lines;
    }

    // Pass 3: permit lines that cannot stretch at all, e.g. a single word wider than
    // half the column, which a narrow measure produces constantly.
    lines.clear();
    if (try_total_fit(items, options, options.tolerance, emergency, lines))
    {
        return lines;
    }

    // Nothing fits: set it greedily so the reader still shows something.
    lines = break_greedy(items, options);
    if (lines.empty())
    {
        lines.push_back(Line{0, length, false, 0, 0, 0});
    }
    return lines;
}

} // namespace text
