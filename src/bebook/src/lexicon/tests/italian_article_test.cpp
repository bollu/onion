#include "../italian_article.h"

#include <gtest/gtest.h>

using namespace lexicon;

TEST(DefiniteArticle, TheOrdinaryCases)
{
    EXPECT_EQ(definite_article("cane", "m"), "il");
    EXPECT_EQ(definite_article("cagna", "f"), "la");
}

TEST(DefiniteArticle, MasculineBeforeAwkwardClusters)
{
    // The rule a learner is taught: lo before s+consonant, z, gn, pn, ps, x, y, i+vowel.
    EXPECT_EQ(definite_article("studente", "m"), "lo");
    EXPECT_EQ(definite_article("zio", "m"), "lo");
    EXPECT_EQ(definite_article("psicologo", "m"), "lo");
    EXPECT_EQ(definite_article("gnomo", "m"), "lo");
    EXPECT_EQ(definite_article("xilofono", "m"), "lo");
    EXPECT_EQ(definite_article("iato", "m"), "lo") << "i + vowel";
}

TEST(DefiniteArticle, SPlusVowelIsOrdinary)
{
    // "sale" is s + vowel, not a cluster, so it is "il" and not "lo".
    EXPECT_EQ(definite_article("sale", "m"), "il");
}

TEST(DefiniteArticle, VowelsElide)
{
    EXPECT_EQ(definite_article("amico", "m"), "l'");
    EXPECT_EQ(definite_article("isola", "f"), "l'");
    EXPECT_EQ(definite_article("ora", "f"), "l'");
}

TEST(DefiniteArticle, AccentedVowelsElideToo)
{
    EXPECT_EQ(definite_article("\xC3\xA8poca", "f"), "l'");
}

TEST(DefiniteArticle, UnknownGenderGetsNoArticle)
{
    // Better a bare noun than an invented gender: the article is the whole claim here.
    EXPECT_EQ(definite_article("cane", ""), "");
    EXPECT_EQ(definite_article("cane", "x"), "");
    EXPECT_EQ(definite_article("", "m"), "");
}

TEST(WithArticle, ElidedArticlesJoinTheNoun)
{
    EXPECT_EQ(with_article("cane", "m"), "il cane");
    EXPECT_EQ(with_article("studente", "m"), "lo studente");
    EXPECT_EQ(with_article("amico", "m"), "l'amico") << "no space after an elision";
    EXPECT_EQ(with_article("isola", "f"), "l'isola");
}

TEST(WithArticle, UnknownGenderLeavesTheNounAlone)
{
    EXPECT_EQ(with_article("cane", ""), "cane");
}
