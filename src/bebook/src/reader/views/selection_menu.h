#ifndef SELECTION_MENU_H_
#define SELECTION_MENU_H_

#include "reader/view.h"
#include "util/throttled.h"

#include "text/font.h"

#include <functional>
#include <string>
#include <vector>

struct SystemStyling;

class SelectionMenu: public View
{
    bool needs_render = true;

    std::vector<std::string> entries;
    // Optional, parallel to `entries`. Empty means a flat list, which is how every menu but
    // the table of contents wants to be drawn -- so those lay out exactly as before.
    std::vector<uint32_t> levels;
    // Optional. When set, a header row shows it plus the cursor position, and the list
    // loses one row to make space. Menus that set no title lay out exactly as before.
    std::string title;
    uint32_t cursor_pos = 0;
    uint32_t scroll_pos = 0;
    bool close_on_select = false;

    SystemStyling &styling;
    const uint32_t styling_sub_id;

    const int line_padding = 4;
    int line_height;
    uint32_t num_display_lines() const;
    uint32_t excess_pxl_y() const;

    Throttled scroll_throttle;

    bool _is_done = false;
    std::function<void(uint32_t)> on_selection;
    std::function<void(uint32_t)> on_focus;
    std::function<void(SDLKey, SelectionMenu&)> default_on_keypress;

    void on_move_down(uint32_t step);
    void on_move_up(uint32_t step);
    void on_select_entry();

public:

    SelectionMenu(SystemStyling &styling);
    SelectionMenu(std::vector<std::string> entries, SystemStyling &styling);
    virtual ~SelectionMenu();

    void set_entries(std::vector<std::string> new_entries);

    // Nesting depth per entry, 0 for top level. Must be the same length as the entries or
    // it is ignored; a short list would silently mis-indent the tail.
    void set_levels(std::vector<uint32_t> new_levels);

    // Names the list and shows the cursor position. Without one, several full-screen
    // menus are indistinguishable, and nothing says how far the list runs or whether B
    // goes back a level or leaves.
    void set_title(const std::string &title);
    void set_on_selection(std::function<void(uint32_t)> callback);
    void set_on_focus(std::function<void(uint32_t)> callback);
    // Define fallback keypress handler
    void set_default_on_keypress(std::function<void(SDLKey, SelectionMenu &)> callback);
    void set_close_on_select();

    void set_cursor_pos(const std::string &entry);
    void set_cursor_pos(uint32_t pos);
    uint32_t get_cursor_pos() const;

    void close();

    bool render(SDL_Surface *dest_surface, bool force_render) override;
    bool is_done() override;
    void on_keypress(SDLKey key) override;
    void on_keyheld(SDLKey key, uint32_t held_time_ms) override;
};

#endif
