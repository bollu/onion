#include "text/styled_text.h"

#include "text/font.h"
#include "text/line_break.h"
#include "text/text_types.h"

#include <gtest/gtest.h>

#include <string>

using namespace text;

// --- Pure alignment math (no font needed) ------------------------------------------

TEST(TextAlign, LeftAndJustifyPenAtLeftEdge)
{
    const Fixed natural = int_to_fixed(100);
    EXPECT_EQ(aligned_left_x(Align::Left, 10, 300, natural), 10);
    EXPECT_EQ(aligned_left_x(Align::Justify, 10, 300, natural), 10);
}

TEST(TextAlign, CenterCentersOnNaturalWidth)
{
    const Fixed natural = int_to_fixed(100);
    // left 10, box 300, line 100 wide -> (300-100)/2 = 100 of slack on the left.
    EXPECT_EQ(aligned_left_x(Align::Center, 10, 300, natural), 10 + 100);
}

// --- Font-backed layout (Charis is bundled; resolves from the repo root at test time) --

TEST(TextLayout, WrapsLongLineAndRespectsAlignment)
{
    Font *font = Engine::instance().load("resources/fonts/Charis-Regular.ttf", 26);
    ASSERT_NE(font, nullptr) << "bundled font missing?";

    const std::string s =
        "the quick brown fox jumps over the lazy dog and then keeps on running for quite a while";
    StyledText st;
    st.text = s.c_str();
    st.length = static_cast<uint32_t>(s.size());
    st.runs = nullptr;
    st.family = font->family;
    st.size_px = font->size_px;

    const int width = 160;

    // Justified: the paragraph wraps, and every line but the last is stretched to the
    // column width; the last line stays ragged (target == natural).
    auto just = layout_paragraph(st, font, width, Align::Justify, /*hyphenate*/ true);
    ASSERT_GT(just.size(), 1u);
    for (size_t i = 0; i + 1 < just.size(); ++i)
    {
        EXPECT_EQ(just[i].target_width, int_to_fixed(width)) << "interior line " << i;
    }
    EXPECT_EQ(just.back().target_width, just.back().natural_width);

    // Centered/left: no line is stretched -- every target equals its natural width.
    auto centered = layout_paragraph(st, font, width, Align::Center, /*hyphenate*/ true);
    ASSERT_GT(centered.size(), 1u);
    for (const auto &ln : centered)
    {
        EXPECT_EQ(ln.target_width, ln.natural_width);
    }
}

TEST(TextAlign, PenXAtMatchesMeasureAndStretch)
{
    Font *font = Engine::instance().load("resources/fonts/Charis-Regular.ttf", 26);
    ASSERT_NE(font, nullptr);

    const std::string s = "alpha beta gamma";  // three words, two spaces
    StyledText st;
    st.text = s.c_str();
    st.length = static_cast<uint32_t>(s.size());
    st.runs = nullptr;
    st.family = font->family;
    st.size_px = font->size_px;

    // A single ragged line (whole string), so target == natural.
    Line ln;
    ln.offset = 0;
    ln.length = st.length;
    ln.trailing_hyphen = false;
    ln.natural_width = measure_styled(st, 0, st.length);
    ln.target_width = ln.natural_width;
    ln.stretch_gaps = 2;

    const int left = 40, width = 600;
    const uint32_t beta_off = 6;  // "alpha " -> index of 'b'

    // Left: pen x at an offset is left + the measured prefix; the start is exactly left.
    EXPECT_EQ(pen_x_at(st, ln, 0, left, width, Align::Left), left);
    EXPECT_EQ(pen_x_at(st, ln, beta_off, left, width, Align::Left),
              left + fixed_round(measure_styled(st, 0, beta_off)));

    // Center: the start pen equals aligned_left_x on the natural width.
    EXPECT_EQ(pen_x_at(st, ln, 0, left, width, Align::Center),
              aligned_left_x(Align::Center, left, width, ln.natural_width));

    // Justify: widen the target so the two gaps stretch; a later offset shifts right past its
    // ragged position by the share of stretch in the gaps before it.
    ln.target_width = ln.natural_width + int_to_fixed(20);  // +20px over 2 gaps
    const int left_x = pen_x_at(st, ln, beta_off, left, width, Align::Left);
    const int just_x = pen_x_at(st, ln, beta_off, left, width, Align::Justify);
    EXPECT_GT(just_x, left_x);                 // "beta" pushed right by one stretched gap
    EXPECT_NEAR(just_x - left_x, 10, 1);       // one of two gaps' worth of the +20px
}
