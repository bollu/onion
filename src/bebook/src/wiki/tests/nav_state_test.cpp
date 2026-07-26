#include "../nav_state.h"

#include <gtest/gtest.h>

using nav::Event;
using nav::State;

TEST(NavState, OpeningAnArticleFromEmpty)
{
    ASSERT_EQ(nav::transition(State::Empty, Event::OpenSucceeded), State::Active);
    ASSERT_TRUE(nav::has_article(State::Active));
    ASSERT_FALSE(nav::is_finished(State::Active));
}

TEST(NavState, AFailedOpenRetiresTheViewRatherThanStranding)
{
    // The bool version left the view on the stack with no article: it drew nothing and
    // returned from on_keypress before the B case, so no button did anything.
    const State after = nav::transition(State::Empty, Event::OpenFailed);

    ASSERT_EQ(after, State::Done);
    ASSERT_TRUE(nav::is_finished(after)) << "must be popped, not left swallowing input";
    ASSERT_FALSE(nav::has_article(after));
}

TEST(NavState, FollowingALinkQueuesRatherThanNavigating)
{
    // Deferral is the whole point: the swap must not happen inside the callback owned by
    // the TokenView it destroys.
    const State after = nav::transition(State::Active, Event::LinkFollowed);

    ASSERT_EQ(after, State::NavigationQueued);
    ASSERT_TRUE(nav::has_article(after)) << "the old article is still on screen until the swap";
    ASSERT_FALSE(nav::is_finished(after));
}

TEST(NavState, QueuedNavigationResolvesBackToActive)
{
    ASSERT_EQ(nav::transition(State::NavigationQueued, Event::QueuedNavSucceeded), State::Active);
}

TEST(NavState, AFailedNavigationStaysOnTheCurrentArticle)
{
    // Previously a failed go_back reported "no history left", which quit the app and
    // discarded the entire back stack.
    const State after = nav::transition(State::NavigationQueued, Event::QueuedNavFailed);

    ASSERT_EQ(after, State::Active);
    ASSERT_FALSE(nav::is_finished(after)) << "a transient read error must not exit the reader";
}

TEST(NavState, BackWithHistoryQueuesTheEarlierArticle)
{
    ASSERT_EQ(nav::transition(State::Active, Event::BackToPrevious), State::NavigationQueued);
}

TEST(NavState, BackWithoutHistoryFinishes)
{
    ASSERT_EQ(nav::transition(State::Active, Event::BackExhausted), State::Done);
}

TEST(NavState, HomeFinishesFromAnyDepth)
{
    ASSERT_EQ(nav::transition(State::Active, Event::HomeRequested), State::Done);
}

TEST(NavState, AFinishedViewCanBeReopened)
{
    // The bug that made the reading list a one-shot: nothing ever cleared is_done, so
    // every later selection was popped the same frame it was pushed.
    const State reopened = nav::transition(State::Done, Event::Reopened);
    ASSERT_EQ(reopened, State::Empty);
    ASSERT_FALSE(nav::is_finished(reopened));

    ASSERT_EQ(nav::transition(reopened, Event::OpenSucceeded), State::Active);
}

TEST(NavState, ReadingListRoundTripSurvivesRepeatedUse)
{
    // list -> article -> back to list -> article, three times over.
    State s = State::Empty;
    for (int i = 0; i < 3; ++i)
    {
        s = nav::transition(s, Event::OpenSucceeded);
        ASSERT_EQ(s, State::Active) << "iteration " << i;

        s = nav::transition(s, Event::BackExhausted);
        ASSERT_EQ(s, State::Done) << "iteration " << i;

        s = nav::transition(s, Event::Reopened);
        ASSERT_EQ(s, State::Empty) << "iteration " << i;
    }
}

TEST(NavState, BrowsingAChainOfLinksAndUnwindingIt)
{
    State s = nav::transition(State::Empty, Event::OpenSucceeded);

    for (int depth = 0; depth < 15; ++depth)
    {
        s = nav::transition(s, Event::LinkFollowed);
        ASSERT_EQ(s, State::NavigationQueued);
        s = nav::transition(s, Event::QueuedNavSucceeded);
        ASSERT_EQ(s, State::Active);
    }

    for (int depth = 0; depth < 15; ++depth)
    {
        s = nav::transition(s, Event::BackToPrevious);
        s = nav::transition(s, Event::QueuedNavSucceeded);
        ASSERT_EQ(s, State::Active) << "depth " << depth;
    }

    ASSERT_EQ(nav::transition(s, Event::BackExhausted), State::Done);
}

TEST(NavState, StrayEventsLeaveTheStateAlone)
{
    // Total by construction: input arriving in a state that does not expect it must not
    // move the machine.
    ASSERT_EQ(nav::transition(State::Empty, Event::LinkFollowed), State::Empty);
    ASSERT_EQ(nav::transition(State::Empty, Event::BackToPrevious), State::Empty);
    ASSERT_EQ(nav::transition(State::Empty, Event::QueuedNavSucceeded), State::Empty);

    ASSERT_EQ(nav::transition(State::Active, Event::OpenFailed), State::Active);
    ASSERT_EQ(nav::transition(State::Active, Event::QueuedNavSucceeded), State::Active);
    ASSERT_EQ(nav::transition(State::Active, Event::Reopened), State::Active)
        << "the list pushing the view it already shows must not reset it";

    ASSERT_EQ(nav::transition(State::NavigationQueued, Event::LinkFollowed), State::NavigationQueued);
    ASSERT_EQ(nav::transition(State::NavigationQueued, Event::BackExhausted), State::NavigationQueued);

    ASSERT_EQ(nav::transition(State::Done, Event::OpenSucceeded), State::Done);
    ASSERT_EQ(nav::transition(State::Done, Event::LinkFollowed), State::Done);
}

TEST(NavState, OnlyDoneIsFinished)
{
    ASSERT_FALSE(nav::is_finished(State::Empty));
    ASSERT_FALSE(nav::is_finished(State::Active));
    ASSERT_FALSE(nav::is_finished(State::NavigationQueued));
    ASSERT_TRUE(nav::is_finished(State::Done));
}

TEST(NavState, OnlyTheArticleStatesDraw)
{
    ASSERT_FALSE(nav::has_article(State::Empty));
    ASSERT_TRUE(nav::has_article(State::Active));
    ASSERT_TRUE(nav::has_article(State::NavigationQueued));
    ASSERT_FALSE(nav::has_article(State::Done));
}
