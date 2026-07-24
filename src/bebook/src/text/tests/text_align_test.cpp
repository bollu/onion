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
