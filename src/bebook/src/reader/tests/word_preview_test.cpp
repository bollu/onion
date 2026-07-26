#include "reader/word_preview.h"

#include "lexicon/lexicon_service.h"

#include <gtest/gtest.h>

namespace
{
// The prebuilt DB, relative to the repo root (where `make test` runs the binary).
const char *DB_PATH = "resources/italian.sqlite";
}

TEST(WordPreview, KnownNounHasLemmaAndGloss)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok()) << "seed DB missing; run: python3 tools/build_lexicon.py";

    std::string s = summarize_word(lex, "cane");
    EXPECT_NE(s.find("cane"), std::string::npos);
    EXPECT_NE(s.find("dog"), std::string::npos) << "got: " << s;
}

TEST(WordPreview, ConjugatedFormResolvesToLemma)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    // "facevo" is the imperfect 1sg of fare; the preview should surface the lemma.
    std::string s = summarize_word(lex, "facevo");
    EXPECT_NE(s.find("fare"), std::string::npos) << "got: " << s;
}

TEST(WordPreview, UnknownWordIsEmpty)
{
    lexicon::LexiconService lex(DB_PATH);
    ASSERT_TRUE(lex.ok());

    EXPECT_EQ(summarize_word(lex, "zzxqwvnonsenseword"), "");
}

TEST(WordPreview, MissingDatabaseIsEmptyNotCrash)
{
    lexicon::LexiconService lex("does/not/exist.sqlite");
    EXPECT_FALSE(lex.ok());
    EXPECT_EQ(summarize_word(lex, "cane"), "");
}
