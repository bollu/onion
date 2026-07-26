#include "reader/views/token_view/word_layout.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

// Convenience: return the surface strings, so tests read in words not byte offsets.
std::vector<std::string> words_of(const std::string &text)
{
    std::vector<std::string> out;
    for (const auto &w : tokenize_words(text))
    {
        out.push_back(text.substr(w.start, w.end - w.start));
    }
    return out;
}

} // namespace

TEST(WordLayout, SplitsPlainSentence)
{
    EXPECT_EQ(words_of("Io facevo il caffe"),
              (std::vector<std::string>{"Io", "facevo", "il", "caffe"}));
}

TEST(WordLayout, DropsPunctuation)
{
    EXPECT_EQ(words_of("Ciao, mondo! Come stai?"),
              (std::vector<std::string>{"Ciao", "mondo", "Come", "stai"}));
}

TEST(WordLayout, KeepsInternalApostropheElision)
{
    EXPECT_EQ(words_of("l'acqua e dell'olio"),
              (std::vector<std::string>{"l'acqua", "e", "dell'olio"}));
    EXPECT_EQ(words_of("un'ora fa"),
              (std::vector<std::string>{"un'ora", "fa"}));
}

TEST(WordLayout, RecognisesTypographicApostrophe)
{
    // "un’ora" — right single quotation mark instead of straight apostrophe.
    EXPECT_EQ(words_of("un\xE2\x80\x99ora"),
              (std::vector<std::string>{"un\xE2\x80\x99ora"}));
}

TEST(WordLayout, KeepsAccentedLetters)
{
    // caffè, perché, città
    EXPECT_EQ(words_of("caff\xC3\xA8 perch\xC3\xA9 citt\xC3\xA0"),
              (std::vector<std::string>{"caff\xC3\xA8", "perch\xC3\xA9", "citt\xC3\xA0"}));
}

TEST(WordLayout, DropsLeadingQuoteKeepsTrailing)
{
    // 'ciao' with straight quotes: leading dropped, trailing kept (could be elision).
    EXPECT_EQ(words_of("'ciao'"),
              (std::vector<std::string>{"ciao'"}));
}

TEST(WordLayout, ExcludesTypographicQuotesAndDashes)
{
    // Curly double quotes “ ” and em dash — are separators. String literals are split so
    // a hex escape is never followed by a hex-digit letter (\x9C + 'c' would over-read).
    EXPECT_EQ(words_of("\xE2\x80\x9C" "ciao" "\xE2\x80\x9D" "\xE2\x80\x94" "ora"),
              (std::vector<std::string>{"ciao", "ora"}));
}

TEST(WordLayout, EmptyAndPunctuationOnly)
{
    EXPECT_TRUE(tokenize_words("").empty());
    EXPECT_TRUE(tokenize_words("   ").empty());
    EXPECT_TRUE(tokenize_words("... !? -- ").empty());
}

TEST(WordLayout, ByteOffsetsAreCorrect)
{
    auto spans = tokenize_words("ab cd");
    ASSERT_EQ(spans.size(), 2u);
    EXPECT_EQ(spans[0], (WordSpan{0, 2}));
    EXPECT_EQ(spans[1], (WordSpan{3, 5}));
}

TEST(TokenizeWords, DigitsAreSelectable)
{
    // A year is often the thing worth landing on in an article, and a token the cursor
    // cannot reach is also one it silently jumps past.
    auto w = tokenize_words("nel 1946 la");
    ASSERT_EQ(w.size(), 3u);
    EXPECT_EQ(std::string("nel 1946 la").substr(w[1].start, w[1].end - w[1].start), "1946");
}

TEST(TokenizeWords, AlphanumericRunsStayOneToken)
{
    const std::string text = "km2 e 1287,36";
    auto w = tokenize_words(text);
    ASSERT_EQ(w.size(), 4u) << "the comma splits the number, as any punctuation does";
    EXPECT_EQ(text.substr(w[0].start, w[0].end - w[0].start), "km2");
    EXPECT_EQ(text.substr(w[2].start, w[2].end - w[2].start), "1287");
}
