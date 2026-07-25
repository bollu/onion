#include "./reading_list_view.h"

#include "reader/system_styling.h"
#include "reader/view_stack.h"
#include "reader/views/selection_menu.h"

#include "sys/keymap.h"

#include <set>
#include <utility>

namespace
{

const char *READ_MARK = "\xE2\x9C\x93 ";   // check mark
const char *UNREAD_MARK = "  ";

}

struct ReadingListViewState
{
    std::vector<ReadingListSection> sections;
    SystemStyling &styling;
    ViewStack &view_stack;
    std::function<void(const std::string &)> on_open;

    std::set<std::string> read_paths;

    // The section menu is this view's own body; article menus are pushed above it.
    std::shared_ptr<SelectionMenu> menu;
    uint32_t section_cursor = 0;

    bool is_done = false;

    ReadingListViewState(std::vector<ReadingListSection> sections,
                         SystemStyling &styling,
                         ViewStack &view_stack,
                         std::function<void(const std::string &)> on_open)
        : sections(std::move(sections)),
          styling(styling),
          view_stack(view_stack),
          on_open(std::move(on_open))
    {
    }
};

ReadingListView::ReadingListView(std::vector<ReadingListSection> sections,
                                 SystemStyling &styling,
                                 ViewStack &view_stack,
                                 std::function<void(const std::string &)> on_open)
    : state(std::make_unique<ReadingListViewState>(std::move(sections), styling, view_stack,
                                                   std::move(on_open)))
{
    show_sections();
}

ReadingListView::~ReadingListView() = default;

void ReadingListView::show_sections()
{
    std::vector<std::string> names;
    names.reserve(state->sections.size());

    for (const auto &section : state->sections)
    {
        uint32_t read = 0;
        for (const auto &entry : section.entries)
        {
            if (state->read_paths.count(entry.path) > 0)
            {
                ++read;
            }
        }

        names.push_back(section.name + "  (" + std::to_string(read) + "/" +
                        std::to_string(section.entries.size()) + ")");
    }

    if (state->menu == nullptr)
    {
        state->menu = std::make_shared<SelectionMenu>(names, state->styling);
        state->menu->set_on_selection([this](uint32_t index) {
            state->section_cursor = index;
            show_section(index);
        });
    }
    else
    {
        state->menu->set_entries(names);
    }

    state->menu->set_cursor_pos(state->section_cursor);
}

void ReadingListView::show_section(uint32_t index)
{
    if (index >= state->sections.size())
    {
        return;
    }

    const auto &section = state->sections[index];

    std::vector<std::string> names;
    names.reserve(section.entries.size());
    for (const auto &entry : section.entries)
    {
        const bool read = state->read_paths.count(entry.path) > 0;
        names.push_back(std::string(read ? READ_MARK : UNREAD_MARK) + entry.title);
    }

    auto menu = std::make_shared<SelectionMenu>(names, state->styling);
    menu->set_on_selection([this, index](uint32_t entry_index) {
        const auto &entries = state->sections[index].entries;
        if (entry_index < entries.size() && state->on_open)
        {
            state->on_open(entries[entry_index].path);
        }
    });
    menu->set_close_on_select();

    state->view_stack.push(menu);
}

bool ReadingListView::render(SDL_Surface *dest_surface, bool force_render)
{
    return state->menu != nullptr && state->menu->render(dest_surface, force_render);
}

bool ReadingListView::is_done()
{
    return state->is_done;
}

void ReadingListView::on_focus()
{
    // Coming back from an article menu, the read marks and per-section counts may have
    // changed, so rebuild the section list.
    show_sections();
}

void ReadingListView::on_keypress(SDLKey key)
{
    if (state->menu == nullptr)
    {
        return;
    }

    // B here means "leave the app": this is the root view, with nothing behind it.
    if (key == SW_BTN_B)
    {
        state->is_done = true;
        return;
    }

    state->menu->on_keypress(key);
}

void ReadingListView::on_keyheld(SDLKey key, uint32_t held_time_ms)
{
    if (state->menu != nullptr)
    {
        state->menu->on_keyheld(key, held_time_ms);
    }
}

void ReadingListView::set_read(const std::string &path, bool read)
{
    if (read)
    {
        state->read_paths.insert(path);
    }
    else
    {
        state->read_paths.erase(path);
    }
}
