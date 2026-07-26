#include "../breadcrumb.h"

#include <gtest/gtest.h>

using namespace wiki;

namespace
{
// One "pixel" per character, so the tests read as character budgets. The real caller passes
// a font measurement; the shape of the arithmetic is identical.
int by_chars(const std::string &s)
{
    // Count code points, not bytes: the separators are multi-byte, and counting bytes would
    // make them ~3x their apparent width and skew every budget below.
    int n = 0;
    for (char c : s)
    {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) { ++n; }
    }
    return n;
}
}

TEST(Breadcrumb, EmptyTrailIsEmpty)
{
    ASSERT_EQ(breadcrumb({}, 100, by_chars), "");
}

TEST(Breadcrumb, ASingleArticleIsJustItsTitle)
{
    ASSERT_EQ(breadcrumb({"Roma"}, 100, by_chars), "Roma");
}

TEST(Breadcrumb, TheWholeTrailWhenItFits)
{
    ASSERT_EQ(breadcrumb({"Roma", "Impero romano"}, 100, by_chars),
              "Roma \xE2\x80\xBA Impero romano");
}

TEST(Breadcrumb, DropsFromTheOldestEndAndSaysSo)
{
    // Only the last hop fits, so the earlier ones collapse into the leading marker rather
    // than the current article being cut.
    const std::string out = breadcrumb(
        {"Storia", "Roma", "Impero romano", "Giulio Cesare"}, 30, by_chars);
    ASSERT_EQ(out.rfind("\xE2\x80\xA6\xE2\x80\xBA ", 0), 0u) << "should lead with the marker: " << out;
    ASSERT_NE(out.find("Giulio Cesare"), std::string::npos) << out;
    ASSERT_EQ(out.find("Storia"), std::string::npos) << "oldest should have gone: " << out;
    ASSERT_LE(by_chars(out), 30) << out;
}

TEST(Breadcrumb, TheCurrentArticleSurvivesEvenWhenItAloneOverruns)
{
    // A bare "…›" would say nothing about where you are. Let it overrun and leave the
    // eliding to the bar that draws it.
    const std::string out = breadcrumb({"Roma", "Una voce molto lunga"}, 5, by_chars);
    ASSERT_EQ(out, "Una voce molto lunga");
}

TEST(Breadcrumb, TheMarkerIsCountedBeforeAnAncestorIsAccepted)
{
    // The bug this guards: accepting an ancestor on the strength of the un-marked width,
    // then prepending "…› " and overrunning. Budget fits "Roma › Cesare" (13) exactly but
    // not with the 3-char marker, and a third entry means the marker is required.
    const std::string out = breadcrumb({"Storia", "Roma", "Cesare"}, 13, by_chars);
    ASSERT_LE(by_chars(out), 13) << out;
}
