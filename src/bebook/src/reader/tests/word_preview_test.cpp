#include "reader/word_preview.h"

#include "lexicon/lexicon_service.h"

#include <gtest/gtest.h>

namespace
{
// The prebuilt DB, relative to the repo root (where `make test` runs the binary).
const char *DB_PATH = "resources/italian.sqlite";

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}
}

TEST(WordPreview, KnownNounHasLemmaAndGloss)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok()) << "seed DB missing; run: python3 tools/build_lexicon.py";

    const WordPreview p = summarize_word(lex, "cane");
    EXPECT_TRUE(contains(p.meaning, "cane")) << "got: " << p.meaning;
    EXPECT_TRUE(contains(p.meaning, "dog")) << "got: " << p.meaning;
    EXPECT_TRUE(contains(p.grammar, "sost.")) << "got: " << p.grammar;
}

TEST(WordPreview, TheDictionaryFormIsNotRepeatedWhenItIsTheWordItself)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    // "cane · cane · sost." would be noise. The lemma is only worth saying when the page
    // shows something else.
    // "cane · cane" would be noise: the right zone carries the dictionary form only when
    // the page shows something else.
    const WordPreview p = summarize_word(lex, "cane");
    EXPECT_FALSE(contains(p.grammar, "cane")) << "got: " << p.grammar;
}

TEST(WordPreview, ConjugatedFormResolvesToLemma)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    // "facevo" is the imperfect 1sg of fare.
    const WordPreview p = summarize_word(lex, "facevo");
    EXPECT_TRUE(contains(p.grammar, "fare")) << "got: " << p.grammar;
    EXPECT_TRUE(contains(p.meaning, "facevo")) << "got: " << p.meaning;
}

TEST(WordPreview, ConjugatedFormNamesItsPersonAndTense)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    // The pronoun and the tense are the whole point: without them "facevo" and "faccio"
    // read the same at a glance.
    const WordPreview p = summarize_word(lex, "facevo");
    EXPECT_TRUE(contains(p.meaning, "io facevo")) << "got: " << p.meaning;
    EXPECT_TRUE(contains(p.grammar, "imperf.")) << "got: " << p.grammar;
}

TEST(WordPreview, TheEnglishIsConjugatedToMatch)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    // The whole point of the line: "fecero ... to make" leaves the reader to conjugate in
    // their head, which is the thing they are reading in order to learn.
    const WordPreview p = summarize_word(lex, "fecero");
    EXPECT_TRUE(contains(p.meaning, "loro fecero")) << "got: " << p.meaning;
    EXPECT_TRUE(contains(p.meaning, "they ")) << "got: " << p.meaning;
    EXPECT_FALSE(contains(p.meaning, "to make")) << "should be conjugated: " << p.meaning;
}

TEST(WordPreview, PassatoRemotoIsDistinguishableFromThePresent)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    // Both are fare, 1sg. Only the tense tells them apart, which is why it is on the line.
    const WordPreview remote = summarize_word(lex, "feci");
    const WordPreview present = summarize_word(lex, "faccio");
    ASSERT_TRUE(contains(remote.grammar, "fare")) << "got: " << remote.grammar;
    EXPECT_TRUE(contains(remote.grammar, "pass. rem.")) << "got: " << remote.grammar;
    EXPECT_TRUE(contains(present.grammar, "pres.")) << "got: " << present.grammar;
    EXPECT_NE(remote.meaning, present.meaning);
}

TEST(WordPreview, UnknownWordIsEmpty)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    EXPECT_TRUE(summarize_word(lex, "zzxqwvnonsenseword").empty());
}

TEST(WordPreview, MissingDatabaseIsEmptyNotCrash)
{
    lexicon::LexiconService lex("does/not/exist.sqlite");
    EXPECT_FALSE(lex.ok());
    EXPECT_TRUE(summarize_word(lex, "cane").empty());
}
