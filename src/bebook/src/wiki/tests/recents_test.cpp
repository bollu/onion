#include "../recents.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{

std::filesystem::path temp_stub(const std::string &name, const std::string &contents)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::trunc);
    out << contents;
    return path;
}

}

TEST(WikiRecents, StubPathIsNamedAfterTheArticle)
{
    const std::string path = wiki_recents::stub_path_for("Impero_bizantino");

    // The Game Switcher captions a tile from the rompath's basename and ignores the label
    // field, and file_cleanName maps underscores to spaces -- so this reads as
    // "Impero bizantino" with no work.
    ASSERT_NE(path.find("Impero_bizantino"), std::string::npos);
    ASSERT_TRUE(wiki_recents::is_stub_path(path));
}

TEST(WikiRecents, StubPathIsSafeForAwkwardArticleNames)
{
    // Article paths do contain slashes and colons; neither survives as a filename.
    const std::string path = wiki_recents::stub_path_for("Foo/Bar:Baz");

    ASSERT_EQ(path.find("Foo/Bar"), std::string::npos);
    ASSERT_NE(path.find("Foo_Bar_Baz"), std::string::npos);
}

TEST(WikiRecents, KeepsAccentsAndApostrophes)
{
    const std::string path = wiki_recents::stub_path_for("Guerre_dell'oppio");
    ASSERT_NE(path.find("Guerre_dell'oppio"), std::string::npos);

    const std::string accented = wiki_recents::stub_path_for("Città_del_Vaticano");
    ASSERT_NE(accented.find("Città_del_Vaticano"), std::string::npos);
}

TEST(WikiRecents, RecognisesOnlyStubPaths)
{
    ASSERT_TRUE(wiki_recents::is_stub_path("/x/Roma.wiki"));

    ASSERT_FALSE(wiki_recents::is_stub_path("/x/archive.zim"));
    ASSERT_FALSE(wiki_recents::is_stub_path("/x/book.epub"));
    ASSERT_FALSE(wiki_recents::is_stub_path(""));
    ASSERT_FALSE(wiki_recents::is_stub_path(".wiki")) << "a bare suffix names nothing";
}

TEST(WikiRecents, ReadsAStub)
{
    const auto path = temp_stub("bewiki_test_ok.wiki",
                                "/mnt/SDCARD/Roms/WIKI/it.zim\nImpero_bizantino\n8675309\n");

    std::string zim;
    std::string article;
    DocAddr address = 0;
    ASSERT_TRUE(wiki_recents::read_stub(path.string(), zim, article, address));

    ASSERT_EQ(zim, "/mnt/SDCARD/Roms/WIKI/it.zim");
    ASSERT_EQ(article, "Impero_bizantino");
    ASSERT_EQ(address, 8675309u) << "resuming must land where the reader was";

    std::filesystem::remove(path);
}

TEST(WikiRecents, TreatsAMissingAddressAsTheStart)
{
    const auto path = temp_stub("bewiki_test_noaddr.wiki", "/x/it.zim\nRoma\n");

    std::string zim;
    std::string article;
    DocAddr address = 12345;
    ASSERT_TRUE(wiki_recents::read_stub(path.string(), zim, article, address));
    ASSERT_EQ(article, "Roma");
    ASSERT_EQ(address, 0u);

    std::filesystem::remove(path);
}

TEST(WikiRecents, RejectsMissingOrMalformedStubs)
{
    std::string zim;
    std::string article;
    DocAddr address = 0;

    ASSERT_FALSE(wiki_recents::read_stub("/nonexistent/x.wiki", zim, article, address));

    const auto only_zim = temp_stub("bewiki_test_short.wiki", "/x/it.zim\n");
    ASSERT_FALSE(wiki_recents::read_stub(only_zim.string(), zim, article, address))
        << "no article line";
    std::filesystem::remove(only_zim);

    const auto blank_article = temp_stub("bewiki_test_blank.wiki", "/x/it.zim\n\n42\n");
    ASSERT_FALSE(wiki_recents::read_stub(blank_article.string(), zim, article, address));
    std::filesystem::remove(blank_article);
}
