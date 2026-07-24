#include "lexicon/lexicon_service.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace lexicon;

namespace
{

// The prebuilt seed DB, relative to the repo root (where `make test` runs the binary).
const char *DB_PATH = "resources/italian.sqlite";

LexiconService open()
{
    return LexiconService(DB_PATH);
}

bool has_lemma(const std::vector<LemmaEntry> &v, const std::string &lemma)
{
    return std::any_of(v.begin(), v.end(), [&](const LemmaEntry &e) {
        return e.lemma == lemma;
    });
}

const LemmaEntry *find_features(const std::vector<LemmaEntry> &v, const std::string &feat)
{
    for (const auto &e : v)
    {
        if (e.features == feat) return &e;
    }
    return nullptr;
}

} // namespace

TEST(LexiconService, DatabaseOpens)
{
    LexiconService lex = open();
    ASSERT_TRUE(lex.ok()) << "seed DB missing; run: python3 tools/build_lexicon.py";
}

TEST(LexiconService, MissingDatabaseIsNotOkButSafe)
{
    LexiconService lex("does/not/exist.sqlite");
    EXPECT_FALSE(lex.ok());
    EXPECT_TRUE(lex.lemmatize("facevo").empty());
    EXPECT_TRUE(lex.conjugations("fare").empty());
}

TEST(LexiconService, LemmatizesConjugatedForm)
{
    LexiconService lex = open();
    auto res = lex.lemmatize("facevo");
    ASSERT_FALSE(res.empty());
    const LemmaEntry *e = find_features(res, "impf+1+s");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->lemma, "fare");
    EXPECT_EQ(e->pos, "VER");
    EXPECT_TRUE(e->is_verb());
    EXPECT_EQ(e->morphology_human, "verb \xC2\xB7 imperfect \xC2\xB7 io");
}

TEST(LexiconService, LemmatizationIsCaseInsensitive)
{
    LexiconService lex = open();
    EXPECT_TRUE(has_lemma(lex.lemmatize("Facevo"), "fare"));
    EXPECT_TRUE(has_lemma(lex.lemmatize("FACEVO"), "fare"));
}

TEST(LexiconService, AmbiguousFormReturnsMultipleAnalyses)
{
    LexiconService lex = open();
    // "sono" is essere present 1sg AND 3pl.
    auto res = lex.lemmatize("sono");
    ASSERT_GE(res.size(), 2u);
    EXPECT_TRUE(has_lemma(res, "essere"));
    EXPECT_NE(find_features(res, "pres+1+s"), nullptr);
    EXPECT_NE(find_features(res, "pres+3+p"), nullptr);
}

TEST(LexiconService, EnglishGlosses)
{
    LexiconService lex = open();
    auto senses = lex.lookup_it_en("fare");
    ASSERT_GE(senses.size(), 2u);
    EXPECT_EQ(senses[0].gloss, "to do");
    EXPECT_EQ(senses[1].gloss, "to make");
}

TEST(LexiconService, ImperfettoTableIsComplete)
{
    LexiconService lex = open();
    auto tables = lex.conjugations("fare");
    ASSERT_FALSE(tables.empty());

    const ConjTable *impf = nullptr;
    for (const auto &t : tables)
    {
        if (t.tense == "imperfetto") impf = &t;
    }
    ASSERT_NE(impf, nullptr);
    EXPECT_EQ(impf->forms[0], "facevo");
    EXPECT_EQ(impf->forms[1], "facevi");
    EXPECT_EQ(impf->forms[2], "faceva");
    EXPECT_EQ(impf->forms[3], "facevamo");
    EXPECT_EQ(impf->forms[4], "facevate");
    EXPECT_EQ(impf->forms[5], "facevano");
}

TEST(LexiconService, TablesAreInDisplayOrder)
{
    LexiconService lex = open();
    auto tables = lex.conjugations("parlare");
    ASSERT_EQ(tables.size(), 5u);
    EXPECT_EQ(tables[0].tense, "presente");
    EXPECT_EQ(tables[1].tense, "imperfetto");
    EXPECT_EQ(tables[2].tense, "passato_prossimo");
    EXPECT_EQ(tables[3].tense, "futuro_semplice");
    EXPECT_EQ(tables[4].tense, "condizionale");
}

TEST(LexiconService, RegularVerbConjugation)
{
    LexiconService lex = open();
    // parlare, presente: parlo/parli/parla/parliamo/parlate/parlano
    auto tables = lex.conjugations("parlare");
    const ConjTable *pres = &tables[0];
    ASSERT_EQ(pres->tense, "presente");
    EXPECT_EQ(pres->forms[0], "parlo");
    EXPECT_EQ(pres->forms[3], "parliamo");
    EXPECT_EQ(pres->forms[5], "parlano");
}

TEST(LexiconService, NonVerbHasNoConjugations)
{
    LexiconService lex = open();
    EXPECT_TRUE(lex.conjugations("casa").empty());

    auto res = lex.lemmatize("casa");
    ASSERT_FALSE(res.empty());
    EXPECT_FALSE(res[0].is_verb());
    EXPECT_EQ(res[0].pos, "NOUN");
    // "base" morphology carries only the part of speech.
    EXPECT_EQ(res[0].morphology_human, "noun");
}

TEST(DescribeMorphology, HandlesTenseAndPerson)
{
    EXPECT_EQ(describe_morphology("VER", "pres+3+p"), "verb \xC2\xB7 present \xC2\xB7 loro");
    EXPECT_EQ(describe_morphology("VER", "fut+2+s"), "verb \xC2\xB7 future \xC2\xB7 tu");
    EXPECT_EQ(describe_morphology("VER", "inf"), "verb \xC2\xB7 infinitive");
    EXPECT_EQ(describe_morphology("NOUN", "base"), "noun");
}
