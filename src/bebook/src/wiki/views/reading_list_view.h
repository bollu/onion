#ifndef READING_LIST_VIEW_H_
#define READING_LIST_VIEW_H_

#include "wiki/reading_list.h"

#include "reader/view.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class SystemStyling;
class ViewStack;
struct ReadingListViewState;

// The launch screen: sections, then articles within a section. Two levels because a flat
// hundred-entry list is a long scroll on a d-pad, and the sections are the point -- they
// are what makes the list a course rather than a pile.
class ReadingListView : public View
{
public:
    ReadingListView(std::vector<ReadingListSection> sections,
                    SystemStyling &styling,
                    ViewStack &view_stack,
                    std::function<void(const std::string &path)> on_open);
    ~ReadingListView();

    bool render(SDL_Surface *dest_surface, bool force_render) override;
    bool is_done() override;
    void on_keypress(SDLKey key) override;
    void on_keyheld(SDLKey key, uint32_t held_time_ms) override;
    void on_focus() override;

    // Marks an article read, so the list shows what is left. Not persisted here; the
    // caller owns the store.
    void set_read(const std::string &path, bool read);

private:
    std::unique_ptr<ReadingListViewState> state;

    void show_sections();
    void show_section(uint32_t index);
    void refresh_section_menu();
};

#endif
