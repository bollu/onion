#include "../percent_decode.h"

#include <gtest/gtest.h>

using namespace zim;

TEST(PercentDecode, PassesThroughPlainText)
{
    ASSERT_EQ(percent_decode(""), "");
    ASSERT_EQ(percent_decode("Roma"), "Roma");
    ASSERT_EQ(percent_decode("Impero_bizantino"), "Impero_bizantino");
}

TEST(PercentDecode, DecodesUtf8Escapes)
{
    ASSERT_EQ(percent_decode("Citt%C3%A0_del_Vaticano"), "Città_del_Vaticano");
    ASSERT_EQ(percent_decode("Perch%C3%A9"), "Perché");
    ASSERT_EQ(percent_decode("%C3%88_cos%C3%AC"), "È_così");
}

TEST(PercentDecode, AcceptsLowercaseHex)
{
    ASSERT_EQ(percent_decode("Citt%c3%a0"), "Città");
}

TEST(PercentDecode, DecodesAnEscapeAtTheVeryEnd)
{
    ASSERT_EQ(percent_decode("%C3"), "\xC3") << "a lone escape is still well formed";
    ASSERT_EQ(percent_decode("x%20"), "x ");
}

TEST(PercentDecode, PassesMalformedEscapesThrough)
{
    ASSERT_EQ(percent_decode("100%"), "100%");
    ASSERT_EQ(percent_decode("50%z"), "50%z");
    ASSERT_EQ(percent_decode("%zz"), "%zz");
    ASSERT_EQ(percent_decode("%C"), "%C") << "one hex digit short of an escape";
    ASSERT_EQ(percent_decode("a%%41b"), "a%Ab");
}

TEST(HrefToZimPath, AcceptsPlainArticleLinks)
{
    ASSERT_EQ(href_to_zim_path("Albano_Laziale"), "Albano_Laziale");
    ASSERT_EQ(href_to_zim_path("./Albano_Laziale"), "Albano_Laziale");
    ASSERT_EQ(href_to_zim_path("Citt%C3%A0_del_Vaticano"), "Città_del_Vaticano");
}

TEST(HrefToZimPath, KeepsApostrophes)
{
    ASSERT_EQ(href_to_zim_path("Classificazione_sismica_dell'Italia"),
              "Classificazione_sismica_dell'Italia");
    ASSERT_EQ(href_to_zim_path("Citt%C3%A0_metropolitane_d'Italia"),
              "Città_metropolitane_d'Italia");
}

TEST(HrefToZimPath, StripsFragments)
{
    ASSERT_EQ(href_to_zim_path("Roma#Storia"), "Roma");
    ASSERT_EQ(href_to_zim_path("./Roma#Storia"), "Roma");
}

TEST(HrefToZimPath, RejectsNonArticleLinks)
{
    ASSERT_EQ(href_to_zim_path(""), "");
    ASSERT_EQ(href_to_zim_path("#Storia"), "") << "fragment only";
    ASSERT_EQ(href_to_zim_path("https://it.wikipedia.org/wiki/Roma"), "");
    ASSERT_EQ(href_to_zim_path("//example.org/x"), "");
    ASSERT_EQ(href_to_zim_path("./_mw_/ext.cite.styles.css"), "");
    ASSERT_EQ(href_to_zim_path("./_res_/favicon.png"), "");
    ASSERT_EQ(href_to_zim_path("/mnt/absolute"), "");
    ASSERT_EQ(href_to_zim_path("../up"), "");
    ASSERT_EQ(href_to_zim_path("Roma#"), "Roma") << "an empty fragment still names the article";
}
