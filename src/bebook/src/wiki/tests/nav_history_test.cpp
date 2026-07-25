#include "../nav_history.h"

#include <gtest/gtest.h>

namespace
{

HistoryEntry entry(const std::string &path, DocAddr address = 0)
{
    return HistoryEntry{path, path, address};
}

}

TEST(NavHistory, StartsEmpty)
{
    NavHistory history;

    ASSERT_FALSE(history.can_go_back());
    ASSERT_EQ(history.size(), 0u);
}

TEST(NavHistory, PopsInReverseOrder)
{
    NavHistory history;
    history.push(entry("Roma"));
    history.push(entry("Impero_romano"));
    history.push(entry("Augusto"));

    ASSERT_TRUE(history.can_go_back());
    ASSERT_EQ(history.pop().path, "Augusto");
    ASSERT_EQ(history.pop().path, "Impero_romano");
    ASSERT_EQ(history.pop().path, "Roma");
    ASSERT_FALSE(history.can_go_back());
}

TEST(NavHistory, CarriesTitleAndAddress)
{
    NavHistory history;
    history.push(HistoryEntry{"Impero_bizantino", "Impero bizantino", 4242});

    const HistoryEntry out = history.pop();
    ASSERT_EQ(out.path, "Impero_bizantino");
    ASSERT_EQ(out.title, "Impero bizantino");
    ASSERT_EQ(out.address, 4242u);
}

TEST(NavHistory, UpdateTopAddressTracksScrolling)
{
    NavHistory history;
    history.push(entry("Roma", 10));
    history.update_top_address(500);

    ASSERT_EQ(history.pop().address, 500u)
        << "going back should land where the reader actually was";
}

TEST(NavHistory, UpdateTopAddressOnAnEmptyHistoryIsSafe)
{
    NavHistory history;
    history.update_top_address(99);

    ASSERT_FALSE(history.can_go_back());
}

TEST(NavHistory, DropsTheOldestBeyondMaxDepth)
{
    NavHistory history(3);
    history.push(entry("A"));
    history.push(entry("B"));
    history.push(entry("C"));
    history.push(entry("D"));

    ASSERT_EQ(history.size(), 3u);
    ASSERT_EQ(history.pop().path, "D");
    ASSERT_EQ(history.pop().path, "C");
    ASSERT_EQ(history.pop().path, "B") << "A was the oldest and should have been dropped";
    ASSERT_FALSE(history.can_go_back());
}

TEST(NavHistory, RevisitingAPathKeepsBothVisits)
{
    // A wiki walk loops constantly; each hop back should undo exactly one hop forward.
    NavHistory history;
    history.push(entry("Roma", 1));
    history.push(entry("Lazio", 2));
    history.push(entry("Roma", 3));

    ASSERT_EQ(history.size(), 3u);
    ASSERT_EQ(history.pop().address, 3u);
    ASSERT_EQ(history.pop().path, "Lazio");
    ASSERT_EQ(history.pop().address, 1u);
}

TEST(NavHistory, ClearEmptiesIt)
{
    NavHistory history;
    history.push(entry("A"));
    history.push(entry("B"));
    history.clear();

    ASSERT_FALSE(history.can_go_back());
    ASSERT_EQ(history.size(), 0u);
}

TEST(NavHistory, ZeroMaxDepthStillHoldsOne)
{
    NavHistory history(0);
    history.push(entry("A"));
    history.push(entry("B"));

    ASSERT_EQ(history.size(), 1u);
    ASSERT_EQ(history.pop().path, "B");
}
