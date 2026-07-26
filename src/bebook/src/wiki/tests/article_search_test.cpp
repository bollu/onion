#include "../article_search.h"

#include <gtest/gtest.h>

using namespace wiki;

TEST(NormaliseQuery, SpacesBecomeUnderscores)
{
    EXPECT_EQ(normalise_query("impero romano"), "Impero_romano");
}

TEST(NormaliseQuery, TheFirstLetterIsCapitalised)
{
    // The path list is sorted case-sensitively, so a lowercase query lands past every
    // capitalised entry and matches nothing. Wikipedia titles are capitalised, and the
    // on-screen keyboard types lowercase.
    EXPECT_EQ(normalise_query("roma"), "Roma");
}

TEST(NormaliseQuery, AlreadyCapitalisedIsLeftAlone)
{
    EXPECT_EQ(normalise_query("Roma"), "Roma");
    EXPECT_EQ(normalise_query("1946"), "1946") << "a digit is not lowercased into anything";
}

TEST(NormaliseQuery, EmptyStaysEmpty)
{
    EXPECT_EQ(normalise_query(""), "");
}
