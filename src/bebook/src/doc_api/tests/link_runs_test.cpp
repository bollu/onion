#include "../link_runs.h"

#include <gtest/gtest.h>

namespace
{

// "one two three": [0,3) -> A, [4,7) -> B, [8,13) -> C
std::vector<LinkRun> sample()
{
    return {
        LinkRun{0, 3, "A"},
        LinkRun{4, 3, "B"},
        LinkRun{8, 5, "C"},
    };
}

}

TEST(SliceLinkRuns, KeepsRunsFullyInsideTheSlice)
{
    const auto out = slice_link_runs(sample(), 4, 3);

    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out[0].target, "B");
    ASSERT_EQ(out[0].offset, 0u) << "rebased to the slice";
    ASSERT_EQ(out[0].length, 3u);
}

TEST(SliceLinkRuns, ClipsRunsStraddlingTheEdges)
{
    const auto out = slice_link_runs(sample(), 2, 8);

    ASSERT_EQ(out.size(), 3u);

    ASSERT_EQ(out[0].target, "A");
    ASSERT_EQ(out[0].offset, 0u);
    ASSERT_EQ(out[0].length, 1u) << "only the last byte of A is inside";

    ASSERT_EQ(out[1].target, "B");
    ASSERT_EQ(out[1].offset, 2u);
    ASSERT_EQ(out[1].length, 3u);

    ASSERT_EQ(out[2].target, "C");
    ASSERT_EQ(out[2].offset, 6u);
    ASSERT_EQ(out[2].length, 2u) << "C is cut off by the slice end";
}

TEST(SliceLinkRuns, DropsRunsOutsideTheSlice)
{
    const auto out = slice_link_runs(sample(), 3, 1);
    ASSERT_TRUE(out.empty()) << "the gap between A and B";

    ASSERT_TRUE(slice_link_runs(sample(), 13, 5).empty()) << "past the last run";
    ASSERT_TRUE(slice_link_runs(sample(), 0, 0).empty()) << "an empty slice";
    ASSERT_TRUE(slice_link_runs({}, 0, 10).empty());
}

TEST(SliceLinkRuns, DoesNotMergeAdjacentRunsSharingATarget)
{
    const std::vector<LinkRun> runs = {
        LinkRun{0, 3, "Same"},
        LinkRun{3, 3, "Same"},
    };
    const auto out = slice_link_runs(runs, 0, 6);

    ASSERT_EQ(out.size(), 2u)
        << "text::slice_runs would normalize these together; link runs must not";
}

TEST(SliceLinkRuns, HandlesExactBoundaries)
{
    const auto out = slice_link_runs(sample(), 0, 3);
    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out[0].target, "A");
    ASSERT_EQ(out[0].length, 3u);

    const auto whole = slice_link_runs(sample(), 0, 13);
    ASSERT_EQ(whole.size(), 3u);
    ASSERT_EQ(whole[0].offset, 0u);
    ASSERT_EQ(whole[2].offset, 8u);
}

TEST(ShiftLinkRuns, MovesEveryRunRight)
{
    auto runs = sample();
    shift_link_runs(runs, 2);

    ASSERT_EQ(runs[0].offset, 2u);
    ASSERT_EQ(runs[1].offset, 6u);
    ASSERT_EQ(runs[2].offset, 10u);
    ASSERT_EQ(runs[0].length, 3u) << "lengths are untouched";
}

TEST(ShiftLinkRuns, ShiftingByZeroIsANoOp)
{
    auto runs = sample();
    shift_link_runs(runs, 0);
    ASSERT_EQ(runs, sample());
}

TEST(LinkRunOverlapping, FindsTheRunUnderAWord)
{
    const auto runs = sample();

    ASSERT_NE(link_run_overlapping(runs, 0, 3), nullptr);
    ASSERT_EQ(link_run_overlapping(runs, 0, 3)->target, "A");
    ASSERT_EQ(link_run_overlapping(runs, 4, 7)->target, "B");
    ASSERT_EQ(link_run_overlapping(runs, 8, 13)->target, "C");
}

TEST(LinkRunOverlapping, OverlapNotContainment)
{
    const auto runs = sample();

    // tokenize_words drops leading punctuation, so a word span can start inside a link
    // or extend past it. Either way it should still resolve.
    ASSERT_EQ(link_run_overlapping(runs, 1, 2)->target, "A") << "strictly inside";
    ASSERT_EQ(link_run_overlapping(runs, 2, 5)->target, "A") << "straddles A and the gap";
    ASSERT_EQ(link_run_overlapping(runs, 6, 9)->target, "B") << "first match wins";
}

TEST(LinkRunOverlapping, ReturnsNullWhenNothingOverlaps)
{
    const auto runs = sample();

    ASSERT_EQ(link_run_overlapping(runs, 3, 4), nullptr) << "the gap between A and B";
    ASSERT_EQ(link_run_overlapping(runs, 13, 20), nullptr);
    ASSERT_EQ(link_run_overlapping({}, 0, 5), nullptr);
    ASSERT_EQ(link_run_overlapping(runs, 5, 5), nullptr) << "an empty span touches nothing";
}

TEST(LinkRunOverlapping, IgnoresEmptyRuns)
{
    const std::vector<LinkRun> runs = {
        LinkRun{4, 0, "Empty"},
        LinkRun{4, 3, "Real"},
    };

    ASSERT_NE(link_run_overlapping(runs, 4, 7), nullptr);
    ASSERT_EQ(link_run_overlapping(runs, 4, 7)->target, "Real");
}
