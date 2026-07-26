#include "../match_detail.h"

#include <gtest/gtest.h>

using namespace dict;

TEST(TenseKeyForFeatures, IndicativeTensesMapToTheirTables)
{
    ASSERT_EQ(tense_key_for_features("pres+1+s"), "presente");
    ASSERT_EQ(tense_key_for_features("impf+1+s"), "imperfetto");
    ASSERT_EQ(tense_key_for_features("fut+3+p"), "futuro_semplice");
    ASSERT_EQ(tense_key_for_features("cond+2+s"), "condizionale");
}

TEST(TenseKeyForFeatures, AParticipleOpensOnTheCompoundTense)
{
    // Someone looking up "andato" is holding half of "sono andato".
    ASSERT_EQ(tense_key_for_features("part"), "passato_prossimo");
}

TEST(TenseKeyForFeatures, MoodsWithoutATableFallBackToThePresent)
{
    // Only the indicative has tables, so a subjunctive form has no tense of its own to
    // show. It must not resolve to "imperfetto" just because the token is in there.
    ASSERT_EQ(tense_key_for_features("sub+impf+1+s"), "presente");
    ASSERT_EQ(tense_key_for_features("sub+pres+3+p"), "presente");
    ASSERT_EQ(tense_key_for_features("impr+2+s"), "presente");
}

TEST(TenseKeyForFeatures, NonFiniteAndUnknownFallBackToThePresent)
{
    ASSERT_EQ(tense_key_for_features("inf"), "presente");
    ASSERT_EQ(tense_key_for_features("ger"), "presente");
    ASSERT_EQ(tense_key_for_features("base"), "presente");
    ASSERT_EQ(tense_key_for_features(""), "presente");
    ASSERT_EQ(tense_key_for_features("nonsense+9"), "presente");
}

TEST(PersonIndexForFeatures, SingularThenPlural)
{
    ASSERT_EQ(person_index_for_features("pres+1+s"), 0);  // io
    ASSERT_EQ(person_index_for_features("pres+2+s"), 1);  // tu
    ASSERT_EQ(person_index_for_features("pres+3+s"), 2);  // lui/lei
    ASSERT_EQ(person_index_for_features("pres+1+p"), 3);  // noi
    ASSERT_EQ(person_index_for_features("pres+2+p"), 4);  // voi
    ASSERT_EQ(person_index_for_features("pres+3+p"), 5);  // loro
}

TEST(PersonIndexForFeatures, FormsWithoutAPersonReportNone)
{
    ASSERT_EQ(person_index_for_features("inf"), -1);
    ASSERT_EQ(person_index_for_features("base"), -1);
    ASSERT_EQ(person_index_for_features("part"), -1);
    ASSERT_EQ(person_index_for_features(""), -1);
    ASSERT_EQ(person_index_for_features("m+pl"), -1) << "a noun plural is not a verb person";
}

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
