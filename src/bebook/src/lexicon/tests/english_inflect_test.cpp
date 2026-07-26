#include "../english_inflect.h"

#include <gtest/gtest.h>

using namespace lexicon;

TEST(EnglishPast, RegularVerbs)
{
    EXPECT_EQ(english_past("walk"), "walked");
    EXPECT_EQ(english_past("live"), "lived") << "a final e takes only -d";
    EXPECT_EQ(english_past("carry"), "carried") << "consonant + y becomes -ied";
    EXPECT_EQ(english_past("play"), "played") << "vowel + y keeps the y";
    EXPECT_EQ(english_past("stop"), "stopped") << "consonant-vowel-consonant doubles";
}

TEST(EnglishPast, TheCommonIrregulars)
{
    // These are exactly the verbs a beginner meets first, which is why the table exists:
    // the regular rule would give "goed", "haved", "maked".
    EXPECT_EQ(english_past("go"), "went");
    EXPECT_EQ(english_past("have"), "had");
    EXPECT_EQ(english_past("make"), "made");
    EXPECT_EQ(english_past("say"), "said");
}

TEST(EnglishThirdPerson, SibilantsTakeEs)
{
    EXPECT_EQ(english_third_person("make"), "makes");
    EXPECT_EQ(english_third_person("watch"), "watches");
    EXPECT_EQ(english_third_person("push"), "pushes");
    EXPECT_EQ(english_third_person("carry"), "carries");
    EXPECT_EQ(english_third_person("be"), "is");
    EXPECT_EQ(english_third_person("have"), "has");
}

TEST(EnglishGerund, DropsTheSilentE)
{
    EXPECT_EQ(english_gerund("make"), "making");
    EXPECT_EQ(english_gerund("go"), "going");
    EXPECT_EQ(english_gerund("stop"), "stopping");
    EXPECT_EQ(english_gerund("be"), "being") << "be keeps its e";
}

TEST(ConjugateGloss, TheCaseThatMotivatedThis)
{
    // "fecero" is fare, loro, passato remoto. A bare "to make" leaves the reader to do the
    // conjugating in their head -- the very thing they are reading in order to learn.
    EXPECT_EQ(conjugate_gloss("to make", 5, "passato_remoto"), "they made");
}

TEST(ConjugateGloss, EachTenseGetsItsOwnEnglishFrame)
{
    EXPECT_EQ(conjugate_gloss("to make", 0, "presente"), "I make");
    EXPECT_EQ(conjugate_gloss("to make", 2, "presente"), "he/she makes");
    EXPECT_EQ(conjugate_gloss("to make", 5, "futuro_semplice"), "they will make");
    EXPECT_EQ(conjugate_gloss("to make", 5, "condizionale"), "they would make");
    EXPECT_EQ(conjugate_gloss("to make", 5, "passato_prossimo"), "they have made");
    EXPECT_EQ(conjugate_gloss("to make", 2, "passato_prossimo"), "he/she has made");
}

TEST(ConjugateGloss, TheImperfectIsProgressive)
{
    // Italian's imperfect is ongoing or habitual. A bare past would render it identically
    // to the passato remoto, losing the distinction the reader is trying to learn.
    EXPECT_EQ(conjugate_gloss("to make", 0, "imperfetto"), "I was making");
    EXPECT_EQ(conjugate_gloss("to make", 5, "imperfetto"), "they were making");
    EXPECT_NE(conjugate_gloss("to make", 5, "imperfetto"),
              conjugate_gloss("to make", 5, "passato_remoto"));
}

TEST(ConjugateGloss, OnlyTheHeadWordInflects)
{
    EXPECT_EQ(conjugate_gloss("to bring about", 5, "passato_remoto"), "they brought about");
    EXPECT_EQ(conjugate_gloss("to look at", 2, "presente"), "he/she looks at");
}

TEST(ConjugateGloss, OnlyInfinitiveGlossesAreConjugated)
{
    // Many glosses are prose about the word rather than a verb to inflect. Treating the
    // first word of one as a verb gave "he/she Useds" for essere.
    EXPECT_EQ(conjugate_gloss("Used as a copula. to be", 2, "presente"), "");
    EXPECT_EQ(conjugate_gloss("dog", 2, "presente"), "");
    EXPECT_EQ(conjugate_gloss("to be", 2, "presente"), "he/she is");
}

TEST(ConjugateGloss, BePicksTheRightPastArm)
{
    // The table carries "was/were"; showing both would be noise.
    EXPECT_EQ(conjugate_gloss("to be", 0, "passato_remoto"), "I was");
    EXPECT_EQ(conjugate_gloss("to be", 5, "passato_remoto"), "they were");
}

TEST(ConjugateGloss, EmptyWhenItCannotBeRendered)
{
    EXPECT_EQ(conjugate_gloss("to make", -1, "presente"), "") << "no person";
    EXPECT_EQ(conjugate_gloss("", 0, "presente"), "") << "no gloss";
    EXPECT_EQ(conjugate_gloss("to make", 0, "congiuntivo"), "") << "no frame for this mood";
}
