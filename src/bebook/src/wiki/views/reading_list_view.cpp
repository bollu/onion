#include "./reading_list_view.h"

#include "reader/system_styling.h"
#include "reader/view_stack.h"
#include "reader/views/selection_menu.h"

#include "sys/keymap.h"

#include <map>
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
    std::shared_ptr<SelectionMenu> section_menu;
    uint32_t section_cursor = 0;
    // Where the cursor sat in each section's article list, so returning resumes there.
    std::map<uint32_t, uint32_t> entry_cursor;

    bool is_done = false;
    std::function<void()> on_search;
    std::string archive_name;

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

    uint32_t read = 0;
    for (const auto &section : state->sections)
    {
        for (const auto &entry : section.entries)
        {
            if (state->read_paths.count(entry.path) > 0)
            {
                ++read;
            }
        }
    }
    std::string header = "Letture  " + std::to_string(read) + " lette";
    if (!state->archive_name.empty())
    {
        header += "  \xC2\xB7  " + state->archive_name;  // ·
    }
    state->menu->set_title(header);

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
    menu->set_title(section.name);
    menu->set_on_selection([this, index](uint32_t entry_index) {
        state->entry_cursor[index] = entry_index;
        const auto &entries = state->sections[index].entries;
        if (entry_index < entries.size() && state->on_open)
        {
            state->on_open(entries[entry_index].path);
        }
    });

    // Return to the entry last opened rather than to the top: finishing article 18 of 22
    // and coming back otherwise means seventeen presses to resume where you were.
    const auto remembered = state->entry_cursor.find(index);
    if (remembered != state->entry_cursor.end() && remembered->second < names.size())
    {
        menu->set_cursor_pos(remembered->second);
    }

    // Deliberately not close_on_select: a menu that finishes while the article view is
    // pushed above it is stranded mid-stack, and both pop together when the article
    // exits -- which drops the reader two levels instead of one.
    state->section_menu = menu;
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

    if (key == SW_BTN_Y && state->on_search)
    {
        state->on_search();
        return;
    }

    // B does nothing here. This is the root view, so B has nothing to back out of, and
    // making it quit meant one press too many on the way out of an article dropped the
    // reader out of the app entirely. START leaves.
    if (key == SW_BTN_B)
    {
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
    const bool changed = read ? state->read_paths.insert(path).second
                              : state->read_paths.erase(path) > 0;
    if (!changed)
    {
        return;
    }

    // Pushed rather than pulled: coming back from an article, the section menu is on top,
    // so this view never regains focus and would never rebuild. The counters ticking up
    // are the only progress feedback the app has -- they were frozen for whole sessions.
    show_sections();
    refresh_section_menu();
}

void ReadingListView::refresh_section_menu()
{
    if (state->section_menu == nullptr || state->section_cursor >= state->sections.size())
    {
        return;
    }

    const auto &section = state->sections[state->section_cursor];
    const uint32_t cursor = state->section_menu->get_cursor_pos();

    std::vector<std::string> names;
    names.reserve(section.entries.size());
    for (const auto &entry : section.entries)
    {
        const bool read = state->read_paths.count(entry.path) > 0;
        names.push_back(std::string(read ? READ_MARK : UNREAD_MARK) + entry.title);
    }

    state->section_menu->set_entries(names);
    state->section_menu->set_cursor_pos(cursor);
}

void ReadingListView::set_on_search(std::function<void()> callback)
{
    state->on_search = std::move(callback);
}

void ReadingListView::set_archive_name(const std::string &name)
{
    state->archive_name = name;
    // Rebuild the header now, since the name usually arrives just after construction.
    show_sections();
}
