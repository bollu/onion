#ifndef TOKEN_VIEW_WS_CAMERA_H_
#define TOKEN_VIEW_WS_CAMERA_H_

// The word cursor is pinned to the middle of the text area and the page scrolls under it,
// like a 2D RPG camera following the player. These are the sign conventions for that,
// separated from TokenView so the arithmetic is testable without a document.
//
// Line numbers are relative to the top of the text area: 0 is the first visible line.
// Scroll deltas are in lines, positive meaning "further into the document".
namespace ws_camera
{

// The row the cursor should rest on, for a text area `visible_lines` tall.
int target_line(int visible_lines);

// How far the page must scroll to bring a cursor at `cursor_line` back to the target.
// Positive scrolls forward. Zero when it is already there.
int scroll_for(int cursor_line, int visible_lines);

// Where the cursor ends up once `actually_scrolled` lines have moved. The caller passes
// what the document really scrolled, which is less than requested at the start and end of
// a document -- that is what leaves the cursor above centre on the first page and below it
// on the last, with no special-casing anywhere.
int cursor_after(int cursor_line, int actually_scrolled);

}

#endif
