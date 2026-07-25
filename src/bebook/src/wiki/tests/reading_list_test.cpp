#include "../reading_list.h"

#include <gtest/gtest.h>

TEST(ReadingList, ParsesSectionsAndEntries)
{
    const auto sections = parse_reading_list(
        "Roma\tImpero romano\tImpero_romano\n"
        "Roma\tAugusto\tAugusto\n"
        "Cina\tDinastia Han\tDinastia_Han\n");

    ASSERT_EQ(sections.size(), 2u);

    ASSERT_EQ(sections[0].name, "Roma");
    ASSERT_EQ(sections[0].entries.size(), 2u);
    ASSERT_EQ(sections[0].entries[0].title, "Impero romano");
    ASSERT_EQ(sections[0].entries[0].path, "Impero_romano");
    ASSERT_EQ(sections[0].entries[1].title, "Augusto");

    ASSERT_EQ(sections[1].name, "Cina");
    ASSERT_EQ(sections[1].entries.size(), 1u);
}

TEST(ReadingList, KeepsFirstSeenSectionOrder)
{
    const auto sections = parse_reading_list(
        "Zeta\tA\tA\n"
        "Alfa\tB\tB\n"
        "Zeta\tC\tC\n");

    ASSERT_EQ(sections.size(), 2u);
    ASSERT_EQ(sections[0].name, "Zeta") << "not sorted; the file's order is the curation";
    ASSERT_EQ(sections[0].entries.size(), 2u) << "a re-opened section is appended to";
    ASSERT_EQ(sections[1].name, "Alfa");
}

TEST(ReadingList, IgnoresCommentsAndBlankLines)
{
    const auto sections = parse_reading_list(
        "# un commento\n"
        "\n"
        "   \n"
        "Roma\tImpero romano\tImpero_romano\n"
        "# Roma\tNon incluso\tNon_incluso\n");

    ASSERT_EQ(sections.size(), 1u);
    ASSERT_EQ(sections[0].entries.size(), 1u);
}

TEST(ReadingList, SkipsMalformedRowsWithoutLosingTheRest)
{
    const auto sections = parse_reading_list(
        "Roma\tImpero romano\tImpero_romano\n"
        "solo una colonna\n"
        "Roma\tsolo due\n"
        "Roma\t\tPercorso_senza_titolo\n"
        "Roma\tTitolo senza percorso\t\n"
        "Roma\tAugusto\tAugusto\n");

    ASSERT_EQ(sections.size(), 1u);
    ASSERT_EQ(sections[0].entries.size(), 2u) << "one bad row costs one article, not the file";
    ASSERT_EQ(sections[0].entries[1].path, "Augusto");
}

TEST(ReadingList, ToleratesCrlf)
{
    const auto sections = parse_reading_list(
        "Roma\tImpero romano\tImpero_romano\r\n"
        "Roma\tAugusto\tAugusto\r\n");

    ASSERT_EQ(sections.size(), 1u);
    ASSERT_EQ(sections[0].entries[0].path, "Impero_romano")
        << "a trailing \\r would break every path lookup";
    ASSERT_EQ(sections[0].entries[1].path, "Augusto");
}

TEST(ReadingList, TrimsSurroundingWhitespace)
{
    const auto sections = parse_reading_list("  Roma  \t  Impero romano  \t  Impero_romano  \n");

    ASSERT_EQ(sections.size(), 1u);
    ASSERT_EQ(sections[0].name, "Roma");
    ASSERT_EQ(sections[0].entries[0].title, "Impero romano");
    ASSERT_EQ(sections[0].entries[0].path, "Impero_romano");
}

TEST(ReadingList, IgnoresExtraColumns)
{
    const auto sections = parse_reading_list("Roma\tImpero romano\tImpero_romano\textra\tancora\n");

    ASSERT_EQ(sections.size(), 1u);
    ASSERT_EQ(sections[0].entries[0].path, "Impero_romano");
}

TEST(ReadingList, KeepsDuplicatePaths)
{
    // The same article can legitimately belong to two sections.
    const auto sections = parse_reading_list(
        "Roma\tImpero romano\tImpero_romano\n"
        "Europa\tImpero romano\tImpero_romano\n");

    ASSERT_EQ(sections.size(), 2u);
    ASSERT_EQ(sections[0].entries[0].path, sections[1].entries[0].path);
}

TEST(ReadingList, HandlesAccentsAndApostrophes)
{
    const auto sections = parse_reading_list(
        "Storia\tCittà del Vaticano\tCittà_del_Vaticano\n"
        "Storia\tGuerre dell'oppio\tGuerre_dell'oppio\n");

    ASSERT_EQ(sections[0].entries[0].path, "Città_del_Vaticano");
    ASSERT_EQ(sections[0].entries[1].path, "Guerre_dell'oppio");
}

TEST(ReadingList, EmptyInputYieldsNoSections)
{
    ASSERT_TRUE(parse_reading_list("").empty());
    ASSERT_TRUE(parse_reading_list("\n\n# solo commenti\n").empty());
}

TEST(ReadingList, MissingFileIsReported)
{
    std::vector<ReadingListSection> out;
    ASSERT_FALSE(load_reading_list("/nonexistent/reading_list.tsv", out));
}
