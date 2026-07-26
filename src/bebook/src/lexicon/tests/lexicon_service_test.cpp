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
    EXPECT_EQ(e->morphology_human, "verbo \xC2\xB7 imperfetto \xC2\xB7 io");
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

TEST(LexiconService, AccentInsensitiveFallback)
{
    LexiconService lex = open();
    // A book with a missing/wrong accent ("universita" for "università") still resolves via
    // the accent-folded fallback. (The unaccented spelling must not itself be a real word --
    // e.g. "perche" is the plural of the fish "perca", so there an exact match rightly wins.)
    EXPECT_TRUE(has_lemma(lex.lemmatize("universita"), "universit\xC3\xA0"));  // università
    // The exact accented form is of course unaffected.
    EXPECT_TRUE(has_lemma(lex.lemmatize("universit\xC3\xA0"), "universit\xC3\xA0"));
}

TEST(LexiconService, EnglishGlosses)
{
    LexiconService lex = open();
    auto senses = lex.lookup_it_en("fare");
    ASSERT_GE(senses.size(), 2u);
    // Assert presence, not an exact index/string: the glosses come from Wiktionary and
    // their precise wording/order is not a stable contract.
    auto has = [&](const std::string &needle) {
        return std::any_of(senses.begin(), senses.end(), [&](const Sense &s) {
            return s.gloss.find(needle) != std::string::npos;
        });
    };
    EXPECT_TRUE(has("do"));
    EXPECT_TRUE(has("make"));
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
    ASSERT_EQ(tables.size(), 6u);
    EXPECT_EQ(tables[0].tense, "presente");
    EXPECT_EQ(tables[1].tense, "imperfetto");
    EXPECT_EQ(tables[2].tense, "passato_prossimo");
    // The two past tenses sit together, ahead of the future.
    EXPECT_EQ(tables[3].tense, "passato_remoto");
    EXPECT_EQ(tables[4].tense, "futuro_semplice");
    EXPECT_EQ(tables[5].tense, "condizionale");
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

    // "casa" is a noun. (Real data may also surface a rare verb homograph, so find the
    // noun analysis rather than assuming it is first.)
    auto res = lex.lemmatize("casa");
    ASSERT_FALSE(res.empty());
    const LemmaEntry *noun = nullptr;
    for (const auto &e : res)
    {
        if (e.pos == "NOUN") noun = &e;
    }
    ASSERT_NE(noun, nullptr);
    EXPECT_FALSE(noun->is_verb());
    EXPECT_EQ(noun->morphology_human, "sostantivo");  // "base" morphology carries only the POS
}

TEST(LexiconService, NounInflectionResolves)
{
    LexiconService lex = open();
    // Noun plural resolves to its lemma (regression: non-verb inflections used to be dropped).
    EXPECT_TRUE(has_lemma(lex.lemmatize("case"), "casa"));
    EXPECT_TRUE(has_lemma(lex.lemmatize("amici"), "amico"));
}

TEST(LexiconService, EssereePassatoProssimo)
{
    LexiconService lex = open();
    auto tables = lex.conjugations("andare");
    const ConjTable *pp = nullptr;
    for (const auto &t : tables)
    {
        if (t.tense == "passato_prossimo") pp = &t;
    }
    ASSERT_NE(pp, nullptr) << "andare should have a passato prossimo";
    // Essere auxiliary + participle agreement, not the wrong "ho andato".
    EXPECT_EQ(pp->forms[0].rfind("sono ", 0), 0u);   // starts with "sono "
    EXPECT_NE(pp->forms[0].find("andat"), std::string::npos);
}

TEST(LexiconService, EmptyAndUnknownInputAreSafe)
{
    LexiconService lex = open();
    EXPECT_TRUE(lex.lemmatize("").empty());
    EXPECT_TRUE(lex.lemmatize("zzzznotanitalianword").empty());
    EXPECT_TRUE(lex.conjugations("").empty());
    EXPECT_TRUE(lex.lookup_it_en("").empty());
    EXPECT_TRUE(lex.lookup_it_it("qualunque").empty());  // It->It is unpopulated; must not crash
}

bool has_suggestion(const std::vector<Suggestion> &v, const std::string &lemma)
{
    return std::any_of(v.begin(), v.end(), [&](const Suggestion &s) { return s.lemma == lemma; });
}

TEST(LexiconService, SuggestHandlesWrongFirstLetter)
{
    LexiconService lex = open();
    // The case this exists for: a wrong FIRST letter must not silently fail.
    EXPECT_TRUE(has_suggestion(lex.suggest("bniversita"), "universit\xC3\xA0"));  // -> università
    // Wrong last letter, and a dropped letter, also resolve.
    EXPECT_TRUE(has_suggestion(lex.suggest("universitq"), "universit\xC3\xA0"));
    EXPECT_TRUE(has_suggestion(lex.suggest("camminvo"), "camminare"));
}

TEST(LexiconService, SuggestIsRankedAndBounded)
{
    LexiconService lex = open();
    auto s = lex.suggest("universia", 4);
    ASSERT_FALSE(s.empty());
    EXPECT_LE(s.size(), 4u);
    for (size_t i = 1; i < s.size(); ++i)
    {
        EXPECT_LE(s[i - 1].distance, s[i].distance);  // nearest first
    }
    // Too-short queries and total nonsense return nothing.
    EXPECT_TRUE(lex.suggest("ab").empty());
    EXPECT_TRUE(lex.suggest("zxqwkjvbn").empty());
}

bool has_word(const std::vector<SearchHit> &v, const std::string &word)
{
    return std::any_of(v.begin(), v.end(), [&](const SearchHit &h) { return h.word == word; });
}

TEST(LexiconService, SearchFindsExactFoldAndConjugatedForms)
{
    LexiconService lex = open();
    // "faro" (no accent) finds the noun "faro" AND "farò" (fare, future) via the fold.
    auto r = lex.search("faro");
    ASSERT_FALSE(r.empty());
    EXPECT_TRUE(has_word(r, "faro"));
    EXPECT_TRUE(has_word(r, "far\xC3\xB2"));  // farò
    // The conjugated hit carries its lemma.
    for (const auto &h : r)
    {
        if (h.word == "far\xC3\xB2") EXPECT_EQ(h.lemma, "fare");
    }
}

TEST(LexiconService, SearchPrefixBrowsesLemmas)
{
    LexiconService lex = open();
    auto r = lex.search("camm");
    ASSERT_FALSE(r.empty());
    // Headword lemmas beginning "camm" (e.g. camminare).
    EXPECT_TRUE(std::any_of(r.begin(), r.end(),
                            [](const SearchHit &h) { return h.lemma == "camminare"; }));
    EXPECT_LE(r.size(), 40u);
}

TEST(LexiconService, SearchIsDedupedAndSafe)
{
    LexiconService lex = open();
    EXPECT_TRUE(lex.search("").empty());
    // No exact/prefix hit -> fuzzy fallback still returns something sensible.
    EXPECT_FALSE(lex.search("universit").empty());
    // Deduped: no (word,lemma) pair appears twice.
    auto r = lex.search("cane");
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto &h : r) EXPECT_TRUE(seen.insert({h.word, h.lemma}).second);
}

TEST(DescribeMorphology, RendersGenderAndNumber)
{
    EXPECT_EQ(describe_morphology("NOUN", "pl"), "sostantivo \xC2\xB7 plurale");
    EXPECT_EQ(describe_morphology("ADJ", "f+pl"),
              "aggettivo \xC2\xB7 femminile \xC2\xB7 plurale");
    EXPECT_EQ(describe_morphology("ADJ", "m+sg"),
              "aggettivo \xC2\xB7 maschile \xC2\xB7 singolare");
}

TEST(DescribeMorphology, HandlesTenseAndPerson)
{
    EXPECT_EQ(describe_morphology("VER", "pres+3+p"), "verbo \xC2\xB7 presente \xC2\xB7 loro");
    EXPECT_EQ(describe_morphology("VER", "fut+2+s"), "verbo \xC2\xB7 futuro \xC2\xB7 tu");
    EXPECT_EQ(describe_morphology("VER", "inf"), "verbo \xC2\xB7 infinito");
    EXPECT_EQ(describe_morphology("NOUN", "base"), "sostantivo");
    // Codes the bulk build adds beyond the four surfaced tenses.
    EXPECT_EQ(describe_morphology("VER", "sub+impf+1+s"),
              "verbo \xC2\xB7 congiuntivo \xC2\xB7 imperfetto \xC2\xB7 io");
    EXPECT_EQ(describe_morphology("VER", "rem+3+s"), "verbo \xC2\xB7 passato remoto \xC2\xB7 lui/lei");
    EXPECT_EQ(describe_morphology("VER", "part"), "verbo \xC2\xB7 participio");
}

TEST(AbbreviateMorphology, IndicativeTenses)
{
    EXPECT_EQ(abbreviate_morphology("VER", "pres+1+s"), "pres.");
    EXPECT_EQ(abbreviate_morphology("VER", "impf+3+p"), "imperf.");
    EXPECT_EQ(abbreviate_morphology("VER", "rem+1+s"), "pass. rem.");
    EXPECT_EQ(abbreviate_morphology("VER", "fut+2+s"), "fut.");
    EXPECT_EQ(abbreviate_morphology("VER", "cond+3+s"), "cond.");
}

TEST(AbbreviateMorphology, MoodQualifiesTheTense)
{
    // A subjunctive imperfect is not an imperfect: dropping the mood would make "facessi"
    // read as "facevo".
    EXPECT_EQ(abbreviate_morphology("VER", "sub+impf+1+s"), "cong. imperf.");
    EXPECT_EQ(abbreviate_morphology("VER", "sub+pres+3+p"), "cong. pres.");
    EXPECT_EQ(abbreviate_morphology("VER", "impr+2+s"), "imper.");
}

TEST(AbbreviateMorphology, NonFiniteForms)
{
    EXPECT_EQ(abbreviate_morphology("VER", "inf"), "inf.");
    EXPECT_EQ(abbreviate_morphology("VER", "part"), "part. pass.");
    EXPECT_EQ(abbreviate_morphology("VER", "ger"), "ger.");
}

TEST(AbbreviateMorphology, FallsBackToThePartOfSpeech)
{
    EXPECT_EQ(abbreviate_morphology("NOUN", "base"), "sost.");
    EXPECT_EQ(abbreviate_morphology("ADJ", "base"), "agg.");
    EXPECT_EQ(abbreviate_morphology("ADV", ""), "avv.");
    EXPECT_EQ(abbreviate_morphology("", ""), "") << "nothing to say is better than a stray dot";
}

TEST(AbbreviateMorphology, CongiunzioneDoesNotCollideWithCongiuntivo)
{
    // Both want "cong.". The mood keeps it, because a learner meets it far more often; the
    // part of speech spells more of itself out rather than being ambiguous.
    EXPECT_EQ(abbreviate_morphology("CON", "base"), "congiunz.");
    EXPECT_EQ(abbreviate_morphology("VER", "sub+pres+1+s"), "cong. pres.");
}

TEST(PersonIndexForFeatures, SingularThenPlural)
{
    EXPECT_EQ(person_index_for_features("pres+1+s"), 0);
    EXPECT_EQ(person_index_for_features("pres+3+s"), 2);
    EXPECT_EQ(person_index_for_features("pres+1+p"), 3);
    EXPECT_EQ(person_index_for_features("pres+3+p"), 5);
}

TEST(PersonIndexForFeatures, FormsWithoutAPersonReportNone)
{
    EXPECT_EQ(person_index_for_features("inf"), -1);
    EXPECT_EQ(person_index_for_features("part"), -1);
    EXPECT_EQ(person_index_for_features("base"), -1);
    EXPECT_EQ(person_index_for_features("m+pl"), -1) << "a noun plural is not a verb person";
}

TEST(TenseKeyForFeatures, IndicativeTensesMapToTheirTables)
{
    ASSERT_EQ(tense_key_for_features("pres+1+s"), "presente");
    ASSERT_EQ(tense_key_for_features("impf+1+s"), "imperfetto");
    ASSERT_EQ(tense_key_for_features("fut+3+p"), "futuro_semplice");
    ASSERT_EQ(tense_key_for_features("cond+2+s"), "condizionale");
}
TEST(TenseKeyForFeatures, ThePassatoRemotoHasItsOwnTable)
{
    // Until the tense existed this fell through to the fallback, so "feci" opened on the
    // present -- indistinguishable from an unrecognised code, which is why it went unseen.
    ASSERT_EQ(tense_key_for_features("rem+1+s"), "passato_remoto");
    ASSERT_EQ(tense_key_for_features("rem+3+p"), "passato_remoto");
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

TEST(TenseNameForFeatures, FullWordsForThePopupHeader)
{
    // The popup has a whole line, so it wants "passato remoto" rather than the peek's
    // "pass. rem." -- the shorthand exists only because one line had to hold two things.
    EXPECT_EQ(tense_name_for_features("rem+3+s"), "passato remoto");
    EXPECT_EQ(tense_name_for_features("pres+1+s"), "presente");
    EXPECT_EQ(tense_name_for_features("impf+2+p"), "imperfetto");
    EXPECT_EQ(tense_name_for_features("part"), "participio");
}

TEST(TenseNameForFeatures, MoodQualifiesTheTense)
{
    // A subjunctive imperfect is not an imperfect; dropping the mood would make "volesse"
    // read as "voleva".
    EXPECT_EQ(tense_name_for_features("sub+impf+1+s"), "congiuntivo imperfetto");
    EXPECT_EQ(tense_name_for_features("impr+2+s"), "imperativo");
}

TEST(TenseNameForFeatures, NothingToSayForAWordWithNoTense)
{
    // A noun gets no parenthetical at all rather than an empty pair of brackets.
    EXPECT_EQ(tense_name_for_features("base"), "");
    EXPECT_EQ(tense_name_for_features(""), "");
    EXPECT_EQ(tense_name_for_features("m+pl"), "");
}
