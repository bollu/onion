#include "../conj_layout.h"

#include <gtest/gtest.h>

using namespace conj;

TEST(ConjColumns, PacksTheColumnsWhenBothFit)
{
    // A simple tense: "faccio" beside "facciamo" in a wide panel.
    const Columns c = columns(86, 75, 100, 32, 538);
    ASSERT_TRUE(c.two_columns);
    ASSERT_EQ(c.plural_col, 86 + 75 + 32) << "packed against the widest singular, not halved";
}

TEST(ConjColumns, RefusesTwoColumnsWhenThePluralWouldNotFit)
{
    // Passato prossimo: "sono andato/andata" beside "siamo andati/andate" does not fit, and
    // the old code clamped instead of saying so, clipping the plural form mid-word.
    const Columns c = columns(86, 300, 320, 32, 538);
    ASSERT_FALSE(c.two_columns);
}

TEST(ConjColumns, TheFallbackSplitIsStillUsable)
{
    // A caller with no vertical room draws two columns anyway and elides, so plural_col and
    // cell_w must stay sane when the fit fails rather than becoming junk.
    const Columns c = columns(86, 300, 320, 32, 538);
    ASSERT_EQ(c.plural_col, 538 / 2);
    ASSERT_GT(c.singular_cell_w, 0);
    ASSERT_GT(c.plural_cell_w, 0);
    ASSERT_LE(c.plural_col + 86 + c.plural_cell_w, 538);
}

TEST(ConjColumns, EachColumnIsMeasuredAgainstWhatItHolds)
{
    // A shared minimum width would elide "facciamo" to fit the cell sized for "faccio".
    // When the columns fit, neither form should need eliding at all.
    const int pron = 86, sing = 75, plur = 110, gutter = 32, avail = 538;
    const Columns c = columns(pron, sing, plur, gutter, avail);
    ASSERT_TRUE(c.two_columns);
    ASSERT_GE(c.singular_cell_w, sing) << "the singular fits its own cell without eliding";
    ASSERT_GE(c.plural_cell_w, plur) << "the wider plural is not squeezed into the singular's";
}

TEST(ConjColumns, TheBoundaryIsInclusive)
{
    // Exactly filling the width still counts as fitting.
    const int pron = 80, gutter = 20, avail = 400;
    const int sing = 100;
    const int plur = avail - (pron + sing + gutter) - pron;   // exact fit
    ASSERT_TRUE(columns(pron, sing, plur, gutter, avail).two_columns);
    ASSERT_FALSE(columns(pron, sing, plur + 1, gutter, avail).two_columns);
}

TEST(ConjColumns, CellWidthNeverGoesNegative)
{
    // A tiny panel must not hand a negative width to the eliding caller.
    const Columns c = columns(200, 400, 400, 32, 100);
    ASSERT_FALSE(c.two_columns);
    ASSERT_GE(c.singular_cell_w, 0);
    ASSERT_GE(c.plural_cell_w, 0);
}

TEST(ConjRows, TwoColumnsPairEachPersonWithItsPlural)
{
    const std::vector<Row> r = rows(true);
    ASSERT_EQ(r.size(), 3u) << "six persons in three rows is the point of two columns";
    ASSERT_EQ(r[0].left, 0);  ASSERT_EQ(r[0].right, 3);   // io   | noi
    ASSERT_EQ(r[1].left, 1);  ASSERT_EQ(r[1].right, 4);   // tu   | voi
    ASSERT_EQ(r[2].left, 2);  ASSERT_EQ(r[2].right, 5);   // lui  | loro
}

TEST(ConjRows, OneColumnReadsInNaturalPersonOrder)
{
    // The whole reason this is a function: splitting each pair where it sits would give
    // io/noi/tu/voi/lui/loro, which is not how a conjugation is read.
    const std::vector<Row> r = rows(false);
    ASSERT_EQ(r.size(), 6u);
    for (int i = 0; i < 6; ++i)
    {
        ASSERT_EQ(r[i].left, i) << "person " << i << " out of order";
        ASSERT_EQ(r[i].right, -1) << "a single-column row carries one person";
    }
}
