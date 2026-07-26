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

std::string joined(const WordPreview &p)
{
    std::string out;
    for (const auto &g : p.glosses)
    {
        out += (out.empty() ? "" : ", ") + g;
    }
    return out;
}
}

TEST(WordPreview, KnownNounHasItsArticleAndGloss)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok()) << "seed DB missing; run: python3 tools/build_lexicon.py";

    const WordPreview p = summarize_word(lex, "cane");
    EXPECT_EQ(p.subject, "il cane") << "gender is shown as an article";
    EXPECT_TRUE(contains(joined(p), "dog")) << "got: " << joined(p);
    EXPECT_TRUE(contains(p.grammar, "sost.")) << "got: " << p.grammar;
}

TEST(WordPreview, TheDictionaryFormIsNotRepeatedWhenItIsTheWordItself)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    // "cane · sost." -- the lemma is only worth saying when the page shows something else.
    const WordPreview p = summarize_word(lex, "cane");
    EXPECT_FALSE(contains(p.grammar, "cane")) << "got: " << p.grammar;
}

TEST(WordPreview, ConjugatedFormResolvesToLemma)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    const WordPreview p = summarize_word(lex, "facevo");
    EXPECT_TRUE(contains(p.grammar, "fare")) << "got: " << p.grammar;
    EXPECT_EQ(p.subject, "io facevo");
    EXPECT_TRUE(contains(p.grammar, "imperf.")) << "got: " << p.grammar;
}

TEST(WordPreview, TheEnglishIsConjugatedToMatch)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    // "fecero ... to make" leaves the reader to conjugate in their head, which is the thing
    // they are reading in order to learn.
    const WordPreview p = summarize_word(lex, "fecero");
    EXPECT_EQ(p.subject, "loro fecero");
    ASSERT_FALSE(p.glosses.empty());
    EXPECT_TRUE(contains(p.glosses.front(), "they ")) << "got: " << p.glosses.front();
    EXPECT_FALSE(contains(joined(p), "to make")) << "should be conjugated: " << joined(p);
}

TEST(WordPreview, SeveralSensesNotJustTheFirst)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    // "fare" carries 64 senses in the database. Showing the first alone is a coin flip, and
    // that first one is "to do" when the sentence may well want "to make".
    const WordPreview p = summarize_word(lex, "fare");
    EXPECT_GT(p.glosses.size(), 1u) << "got: " << joined(p);
}

TEST(WordPreview, ThePronounIsSaidOnce)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    // "they made, created", not "they made, they created": past the first gloss the pronoun
    // is the same word every time and only costs width.
    const WordPreview p = summarize_word(lex, "fecero");
    ASSERT_GT(p.glosses.size(), 1u) << "got: " << joined(p);
    for (size_t i = 1; i < p.glosses.size(); ++i)
    {
        EXPECT_NE(p.glosses[i].rfind("they ", 0), 0u)
            << "gloss " << i << " repeats the pronoun: " << p.glosses[i];
    }
}

TEST(WordPreview, PassatoRemotoIsDistinguishableFromThePresent)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    // Both are fare, 1sg. Only the tense tells them apart.
    const WordPreview remote = summarize_word(lex, "feci");
    const WordPreview present = summarize_word(lex, "faccio");
    ASSERT_TRUE(contains(remote.grammar, "fare")) << "got: " << remote.grammar;
    EXPECT_TRUE(contains(remote.grammar, "pass. rem.")) << "got: " << remote.grammar;
    EXPECT_TRUE(contains(present.grammar, "pres.")) << "got: " << present.grammar;
    EXPECT_NE(joined(remote), joined(present));
}

TEST(WordPreview, AnUnknownWordStillSaysSomething)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    // A blank line reads as a broken app rather than an unrecognised word, and proper nouns
    // and rare inflections land here constantly.
    const WordPreview p = summarize_word(lex, "zzxqwvnonsenseword");
    EXPECT_FALSE(p.empty());
    EXPECT_EQ(p.subject, "zzxqwvnonsenseword") << "the word itself is always shown";
}

TEST(WordPreview, MissingDatabaseIsEmptyNotCrash)
{
    lexicon::LexiconService lex("does/not/exist.sqlite");
    EXPECT_FALSE(lex.ok());

    // No lexicon means no suggestion either, so only the word comes back.
    const WordPreview p = summarize_word(lex, "cane");
    EXPECT_EQ(p.subject, "cane");
    EXPECT_TRUE(p.glosses.empty());
    EXPECT_TRUE(p.grammar.empty());
}
