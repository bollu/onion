#include "../hyphenate.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{

std::vector<uint16_t> hyphenate(const std::string &word)
{
    return text::hyphenate_word(word.data(), static_cast<uint32_t>(word.size()));
}

// "computer" -> "com-puter": the break points spliced back into the word, which is
// far easier to eyeball in a failure message than a vector of offsets.
std::string hyphenated(const std::string &word)
{
    auto breaks = hyphenate(word);
    std::string out;
    size_t next = 0;
    for (size_t i = 0; i < word.size(); ++i)
    {
        if (next < breaks.size() && breaks[next] == i)
        {
            out += '-';
            ++next;
        }
        out += word[i];
    }
    return out;
}

}  // namespace

TEST(HYPHENATE, known_tex_results)
{
    EXPECT_EQ(hyphenated("hyphenation"), "hy-phen-ation");
    EXPECT_EQ(hyphenated("algorithm"), "al-go-rithm");
    EXPECT_EQ(hyphenated("typography"), "ty-pog-ra-phy");
    EXPECT_EQ(hyphenated("beautiful"), "beau-ti-ful");
    EXPECT_EQ(hyphenated("university"), "uni-ver-sity");
    EXPECT_EQ(hyphenated("mississippi"), "mis-sis-sippi");
    EXPECT_EQ(hyphenated("supercalifragilisticexpialidocious"),
              "su-per-cal-ifrag-ilis-tic-ex-pi-ali-do-cious");
}

// Knuth's textbook example is "com-put-er", but that is the raw pattern result;
// \righthyphenmin=3 forbids leaving only "er" on the next line, so TeX itself
// breaks this word once.
TEST(HYPHENATE, computer_is_clipped_by_righthyphenmin)
{
    EXPECT_EQ(hyphenated("computer"), "com-puter");
}

// The upstream pattern file lists this under "known_bugs": ushyphmax yields
// de-mo-c-rat where dem-o-crat is wanted. Pinned so a pattern refresh that fixes
// it upstream shows up as a test failure rather than a silent change.
TEST(HYPHENATE, democratic_matches_the_documented_upstream_bug)
{
    EXPECT_EQ(hyphenated("democratic"), "de-mo-c-ra-tic");
}

TEST(HYPHENATE, exception_list_overrides_patterns)
{
    EXPECT_EQ(hyphenated("table"), "ta-ble");
    EXPECT_EQ(hyphenated("associate"), "as-so-ciate");
    EXPECT_EQ(hyphenated("associates"), "as-so-ciates");
    EXPECT_EQ(hyphenated("declination"), "dec-li-na-tion");
    EXPECT_EQ(hyphenated("reformation"), "ref-or-ma-tion");
    EXPECT_EQ(hyphenated("philanthropic"), "phil-an-thropic");

    // DEK's list spells these with no hyphens at all, i.e. "do not break".
    EXPECT_TRUE(hyphenate("present").empty());
    EXPECT_TRUE(hyphenate("presents").empty());
    EXPECT_TRUE(hyphenate("project").empty());
    EXPECT_TRUE(hyphenate("projects").empty());
}

TEST(HYPHENATE, exceptions_are_case_insensitive)
{
    EXPECT_EQ(hyphenated("Table"), "Ta-ble");
    EXPECT_EQ(hyphenated("TABLE"), "TA-BLE");
    EXPECT_EQ(hyphenated("Hyphenation"), "Hy-phen-ation");
    EXPECT_EQ(hyphenate("HYPHENATION"), hyphenate("hyphenation"));
}

TEST(HYPHENATE, respects_left_and_right_hyphen_min)
{
    for (const char *word : {"hyphenation", "typography", "university",
                             "supercalifragilisticexpialidocious", "table"})
    {
        uint32_t length = static_cast<uint32_t>(strlen(word));
        for (uint16_t offset : text::hyphenate_word(word, length))
        {
            EXPECT_GE(offset, 2u) << word;
            EXPECT_LE(offset + 3u, length) << word;
        }
    }
}

TEST(HYPHENATE, offsets_are_strictly_increasing_and_interior)
{
    for (const char *word : {"hyphenation", "beautiful", "democratic",
                             "declination", "supercalifragilisticexpialidocious"})
    {
        uint32_t length = static_cast<uint32_t>(strlen(word));
        auto breaks = text::hyphenate_word(word, length);
        for (size_t i = 0; i < breaks.size(); ++i)
        {
            EXPECT_GT(breaks[i], 0u) << word;
            EXPECT_LT(breaks[i], length) << word;
            if (i > 0)
            {
                EXPECT_GT(breaks[i], breaks[i - 1]) << word;
            }
        }
    }
}

TEST(HYPHENATE, short_words_have_no_breaks)
{
    EXPECT_TRUE(hyphenate("").empty());
    EXPECT_TRUE(hyphenate("a").empty());
    EXPECT_TRUE(hyphenate("to").empty());
    EXPECT_TRUE(hyphenate("the").empty());
    EXPECT_TRUE(hyphenate("word").empty());
    // Five bytes is the shortest word a break can fit in (2 + 3).
    EXPECT_EQ(hyphenated("table"), "ta-ble");
}

TEST(HYPHENATE, non_alphabetic_input_is_declined)
{
    EXPECT_TRUE(hyphenate("hyphen4tion").empty());
    EXPECT_TRUE(hyphenate("hyphen-ation").empty());
    EXPECT_TRUE(hyphenate("hyphenation.").empty());
    EXPECT_TRUE(hyphenate("don't").empty());
    EXPECT_TRUE(hyphenate("naïvety").empty());        // U+00EF
    EXPECT_TRUE(hyphenate("hyphenation’s").empty());
    EXPECT_TRUE(hyphenate(std::string("hyphen\0ation", 12)).empty());
}

TEST(HYPHENATE, absurdly_long_words_are_declined)
{
    EXPECT_TRUE(hyphenate(std::string(101, 'a')).empty());
    EXPECT_FALSE(hyphenate(std::string(50, 'a') + "hyphenation").empty());
}

TEST(HYPHENATE, null_input_is_safe)
{
    EXPECT_TRUE(text::hyphenate_word(nullptr, 0).empty());
    EXPECT_TRUE(text::hyphenate_word(nullptr, 10).empty());
}

TEST(HYPHENATE, length_bounds_the_word_rather_than_nul)
{
    // Only the first eight bytes are the word; the rest must be ignored.
    const char *buffer = "computerized";
    EXPECT_EQ(text::hyphenate_word(buffer, 8), hyphenate("computer"));
}
