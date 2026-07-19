#include "text/line_break.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numeric>
#include <string>

namespace
{

// A deterministic stand-in for a real font: every character is one unit wide. Layout
// quality can then be reasoned about exactly, without a font file in the test.
text::Fixed monospace_measure(const std::string &text, uint32_t offset, uint32_t length)
{
    (void)text;
    return text::int_to_fixed(static_cast<int>(length));
}

struct Layout
{
    std::vector<text::Line> lines;
    std::vector<std::string> text;
};

Layout run(
    const std::string &paragraph,
    int width_chars,
    const text::HyphenateFn &hyphenate = nullptr,
    bool enable_hyphenation = false
)
{
    text::BreakOptions options;
    options.line_width = text::int_to_fixed(width_chars);
    options.hyphenate = enable_hyphenation;

    Layout out;
    out.lines = text::break_paragraph(
        paragraph.data(),
        static_cast<uint32_t>(paragraph.size()),
        [&](uint32_t o, uint32_t l) { return monospace_measure(paragraph, o, l); },
        options,
        hyphenate
    );

    for (const auto &line : out.lines)
    {
        out.text.push_back(paragraph.substr(line.offset, line.length));
    }
    return out;
}

// Sum of squared slack, the quantity Knuth-Plass exists to minimise. Excludes the last
// line, which is set ragged by design.
double raggedness(const Layout &layout, int width_chars)
{
    double total = 0;
    for (size_t i = 0; i + 1 < layout.lines.size(); ++i)
    {
        const double slack = width_chars - static_cast<double>(layout.text[i].size());
        total += slack * slack;
    }
    return total;
}

// Reference greedy first-fit, i.e. what the reader did before this module existed.
std::vector<std::string> greedy(const std::string &paragraph, int width_chars)
{
    std::vector<std::string> lines;
    std::string current;
    size_t i = 0;
    while (i < paragraph.size())
    {
        size_t end = paragraph.find(' ', i);
        if (end == std::string::npos) end = paragraph.size();
        const std::string word = paragraph.substr(i, end - i);

        if (current.empty())
        {
            current = word;
        }
        else if (current.size() + 1 + word.size() <= static_cast<size_t>(width_chars))
        {
            current += " " + word;
        }
        else
        {
            lines.push_back(current);
            current = word;
        }
        i = end + 1;
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

const char *AUSTEN =
    "It is a truth universally acknowledged that a single man in possession "
    "of a good fortune must be in want of a wife";

} // namespace

TEST(LineBreakTest, EmptyParagraphYieldsOneEmptyLine)
{
    auto layout = run("", 40);
    ASSERT_EQ(layout.lines.size(), 1u);
    EXPECT_EQ(layout.lines[0].length, 0u);
}

TEST(LineBreakTest, ShortParagraphFitsOnOneLine)
{
    auto layout = run("hello world", 40);
    ASSERT_EQ(layout.lines.size(), 1u);
    EXPECT_EQ(layout.text[0], "hello world");
}

// The rendered width is target_width, not natural_width: a justified line may hold
// slightly more text than the column and have its spaces shrunk to fit. So the invariant
// is on the target, and the natural overshoot must stay within the shrink available
// (a third of each space, per build_items).
TEST(LineBreakTest, NoRenderedLineExceedsTheColumnWidth)
{
    const int width = 40;
    auto layout = run(AUSTEN, width);

    for (const auto &line : layout.lines)
    {
        EXPECT_LE(line.target_width, text::int_to_fixed(width));

        const text::Fixed shrink_budget =
            static_cast<text::Fixed>(line.stretch_gaps) * text::int_to_fixed(1) / 3;
        EXPECT_LE(line.natural_width, line.target_width + shrink_budget);
    }
}

TEST(LineBreakTest, LinesCoverTheSourceInOrderWithoutOverlap)
{
    auto layout = run(AUSTEN, 40);

    uint32_t expected_next = 0;
    for (const auto &line : layout.lines)
    {
        EXPECT_GE(line.offset, expected_next);
        expected_next = line.offset + line.length;
    }
    EXPECT_LE(expected_next, std::string(AUSTEN).size());
}

// The reason document addressing survives a change of line-breaking algorithm: the
// reader advances addresses by counting non-whitespace characters per line, so layout
// may move breaks around but must never lose or duplicate a character.
TEST(LineBreakTest, PreservesEveryNonWhitespaceCharacter)
{
    const std::string paragraph = AUSTEN;

    auto count_non_space = [](const std::string &s) {
        return std::count_if(s.begin(), s.end(), [](char c) { return c != ' '; });
    };

    for (int width : {20, 28, 33, 40, 55, 72})
    {
        auto layout = run(paragraph, width);

        long total = 0;
        for (const auto &line : layout.text)
        {
            total += count_non_space(line);
        }
        EXPECT_EQ(total, count_non_space(paragraph)) << "width " << width;
    }
}

TEST(LineBreakTest, LastLineIsNotJustified)
{
    auto layout = run(AUSTEN, 40);
    const auto &last = layout.lines.back();
    EXPECT_EQ(last.target_width, last.natural_width);
}

TEST(LineBreakTest, JustifiedLinesTargetTheFullColumn)
{
    const int width = 40;
    auto layout = run(AUSTEN, width);

    ASSERT_GT(layout.lines.size(), 1u);
    for (size_t i = 0; i + 1 < layout.lines.size(); ++i)
    {
        if (layout.lines[i].stretch_gaps > 0)
        {
            EXPECT_EQ(layout.lines[i].target_width, text::int_to_fixed(width));
        }
    }
}

// The point of the whole module: total-fit should never be raggeder than first-fit,
// and on real prose should be clearly better.
TEST(LineBreakTest, IsNoRaggederThanGreedy)
{
    for (int width : {25, 30, 35, 40, 45, 50, 60})
    {
        auto layout = run(AUSTEN, width);

        Layout greedy_layout;
        greedy_layout.text = greedy(AUSTEN, width);
        greedy_layout.lines.resize(greedy_layout.text.size());

        const double kp = raggedness(layout, width);
        const double gr = raggedness(greedy_layout, width);

        EXPECT_LE(kp, gr) << "width " << width << ": knuth-plass " << kp << " vs greedy " << gr;
    }
}

TEST(LineBreakTest, FirstLineIndentNarrowsOnlyTheFirstLine)
{
    text::BreakOptions options;
    options.line_width = text::int_to_fixed(40);
    options.first_line_indent = text::int_to_fixed(8);

    const std::string paragraph = AUSTEN;
    auto lines = text::break_paragraph(
        paragraph.data(), static_cast<uint32_t>(paragraph.size()),
        [&](uint32_t o, uint32_t l) { return monospace_measure(paragraph, o, l); },
        options
    );

    ASSERT_GT(lines.size(), 1u);
    EXPECT_LE(lines[0].natural_width, text::int_to_fixed(32));
}

// A word wider than the column cannot be broken without hyphenation; it must still
// render rather than throwing or looping.
TEST(LineBreakTest, OverlongWordDoesNotHangOrThrow)
{
    const std::string paragraph = "short " + std::string(120, 'x') + " tail";
    auto layout = run(paragraph, 20);

    ASSERT_FALSE(layout.lines.empty());
    long total = 0;
    for (const auto &l : layout.text)
    {
        total += std::count_if(l.begin(), l.end(), [](char c) { return c != ' '; });
    }
    EXPECT_EQ(total, 120 + 5 + 4);
}

TEST(LineBreakTest, HyphenationIntroducesFlaggedBreaks)
{
    // Allows a break after every third character, standing in for real patterns.
    auto every_third = [](const char *, uint32_t length) {
        std::vector<uint16_t> points;
        for (uint16_t i = 3; i + 3 <= length; i += 3)
        {
            points.push_back(i);
        }
        return points;
    };

    const std::string paragraph =
        "extraordinarily complicated typographical considerations notwithstanding";

    auto without = run(paragraph, 24);
    auto with = run(paragraph, 24, every_third, true);

    bool any_hyphen = false;
    for (const auto &line : with.lines)
    {
        any_hyphen = any_hyphen || line.trailing_hyphen;
    }
    EXPECT_TRUE(any_hyphen);

    // Hyphenation should tighten the setting, not loosen it.
    EXPECT_LE(raggedness(with, 24), raggedness(without, 24));
}
