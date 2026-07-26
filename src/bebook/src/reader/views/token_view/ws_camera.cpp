#include "./ws_camera.h"

#include <algorithm>

namespace ws_camera
{

int target_line(int visible_lines)
{
    // A one-line area still has a row 0 to sit on; anything smaller has nowhere to be.
    return std::max(0, visible_lines / 2);
}

int scroll_for(int cursor_line, int visible_lines)
{
    return cursor_line - target_line(visible_lines);
}

int cursor_after(int cursor_line, int actually_scrolled)
{
    // Scrolling forward moves the text up past the cursor, so the cursor's row falls by the
    // same amount.
    return cursor_line - actually_scrolled;
}

}
