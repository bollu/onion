#include "util/edit_distance.h"

#include <gtest/gtest.h>

TEST(EditDistance, Identity)
{
    EXPECT_EQ(edit_distance("casa", "casa", 3), 0);
    EXPECT_EQ(edit_distance("", "", 3), 0);
}

TEST(EditDistance, SingleEdits)
{
    EXPECT_EQ(edit_distance("casa", "caso", 3), 1);   // substitution
    EXPECT_EQ(edit_distance("casa", "cassa", 3), 1);  // insertion
    EXPECT_EQ(edit_distance("cassa", "casa", 3), 1);  // deletion
    EXPECT_EQ(edit_distance("casa", "acsa", 3), 2);   // transposition = 2 edits for Levenshtein
}

TEST(EditDistance, EmptyStrings)
{
    EXPECT_EQ(edit_distance("", "abc", 5), 3);
    EXPECT_EQ(edit_distance("abc", "", 5), 3);
    EXPECT_EQ(edit_distance("", "abc", 2), 3);  // capped: 3 > 2 -> max+1 == 3 here
}

TEST(EditDistance, CapEarlyExit)
{
    // Far apart: returns max+1 rather than the true (large) distance.
    EXPECT_EQ(edit_distance("università", "cane", 2), 3);
    EXPECT_EQ(edit_distance("aaaaaaaa", "bbbbbbbb", 2), 3);
    // Length gap alone exceeds the cap.
    EXPECT_EQ(edit_distance("a", "aaaaa", 2), 3);
    // Just within the cap is returned exactly.
    EXPECT_EQ(edit_distance("kitten", "sitting", 3), 3);
}
