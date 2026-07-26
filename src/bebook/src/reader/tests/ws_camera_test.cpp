#include "../views/token_view/ws_camera.h"

#include <gtest/gtest.h>

using namespace ws_camera;

TEST(WsCameraTarget, SitsInTheMiddleOfTheTextArea)
{
    ASSERT_EQ(target_line(10), 5);
    ASSERT_EQ(target_line(11), 5);
}

TEST(WsCameraTarget, DegenerateAreasStillHaveARow)
{
    // A one-line area has only row 0; a zero or negative one must not produce a negative
    // target, which would ask the page to scroll backwards forever.
    ASSERT_EQ(target_line(1), 0);
    ASSERT_EQ(target_line(0), 0);
    ASSERT_EQ(target_line(-3), 0);
}

TEST(WsCameraScroll, ACursorBelowCentreScrollsForward)
{
    // Cursor on row 8 of a 10-line area: the page must move 3 lines further in.
    ASSERT_EQ(scroll_for(8, 10), 3);
}

TEST(WsCameraScroll, ACursorAboveCentreScrollsBack)
{
    ASSERT_EQ(scroll_for(1, 10), -4);
}

TEST(WsCameraScroll, AlreadyCentredAsksForNothing)
{
    // Moving by word along a line must not scroll the page.
    ASSERT_EQ(scroll_for(5, 10), 0);
}

TEST(WsCameraCursor, ScrollingForwardLowersTheCursorRow)
{
    // The text moves up past a stationary cursor, so its row falls by what was scrolled.
    ASSERT_EQ(cursor_after(8, 3), 5);
    ASSERT_EQ(cursor_after(1, -4), 5);
}

TEST(WsCameraCursor, AClampedScrollLeavesTheCursorOffCentre)
{
    // At the top of a document the page cannot scroll back, so the cursor keeps its row and
    // walks the first half-page itself. This is the whole edge behaviour: no special case,
    // just the scroll that actually happened.
    ASSERT_EQ(cursor_after(1, 0), 1);

    // Partially clamped: asked for 4 back, got 2.
    ASSERT_EQ(cursor_after(1, -2), 3);
}

TEST(WsCameraCursor, CentringIsWhatTheTwoComposeTo)
{
    // With room to scroll, any starting row ends on the target.
    const int visible = 10;
    for (int row = 0; row < visible; ++row)
    {
        const int want = scroll_for(row, visible);
        ASSERT_EQ(cursor_after(row, want), target_line(visible)) << "from row " << row;
    }
}
