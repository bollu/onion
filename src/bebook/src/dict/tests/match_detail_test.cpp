#include "../match_detail.h"

#include <gtest/gtest.h>

using namespace dict;

namespace
{

lexicon::ConjTable table(const std::string &tense)
{
    lexicon::ConjTable t;
    t.tense = tense;
    t.display_name = tense;
    t.forms = {"a", "b", "c", "d", "e", "f"};
    return t;
}

}

TEST(PickTable, PrefersTheImpliedTense)
{
    const std::vector<lexicon::ConjTable> tables = {
        table("presente"), table("imperfetto"), table("futuro_semplice")
    };

    ASSERT_EQ(pick_table(tables, "imperfetto")->tense, "imperfetto");
    ASSERT_EQ(pick_table(tables, "presente")->tense, "presente");
}

TEST(PickTable, FallsBackToTheFirstWhenTheTenseIsMissing)
{
    // Defective verbs, and builds carrying fewer tenses, are why this is not an assert.
    const std::vector<lexicon::ConjTable> tables = { table("presente") };
    ASSERT_EQ(pick_table(tables, "condizionale")->tense, "presente");
}

TEST(PickTable, NoTablesMeansNoPointer)
{
    const std::vector<lexicon::ConjTable> none;
    ASSERT_EQ(pick_table(none, "presente"), nullptr);
}

TEST(HasMoreToShow, ASimpleNounHasNothingMore)
{
    // One reading, no conjugation, every sense already on screen: opening the modal would
    // add only chrome, so the prompt for it must not appear.
    ASSERT_FALSE(has_more_to_show(1, 0, 3, 3));
}

TEST(HasMoreToShow, AVerbAlwaysHasMoreTenses)
{
    ASSERT_TRUE(has_more_to_show(1, 5, 2, 2));
}

TEST(HasMoreToShow, AnAmbiguousFormHasAnotherReading)
{
    // "sono" is essere 1sg-present and 3pl-present.
    ASSERT_TRUE(has_more_to_show(2, 0, 1, 1));
}

TEST(HasMoreToShow, SensesThatDidNotFitCount)
{
    ASSERT_TRUE(has_more_to_show(1, 0, 9, 4));
    ASSERT_FALSE(has_more_to_show(1, 0, 4, 4));
}

TEST(HasMoreToShow, ASingleTenseIsNotMore)
{
    // One table is already the one on screen, so it is not a reason to open the modal.
    ASSERT_FALSE(has_more_to_show(1, 1, 2, 2));
}
