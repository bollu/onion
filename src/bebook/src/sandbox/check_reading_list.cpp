#include "filetypes/zim/zim_file.h"
#include "filetypes/zim/zim_source.h"
#include "wiki/reading_list.h"

#include <iostream>
#include <memory>
#include <string>

// check_reading_list <file.zim> <reading_list.tsv>
//
// Every path in the list is resolved against the archive. A hand-written list will
// contain dead entries -- Wikipedia titles move, and the "top" archives carry only the
// article namespace, so nothing under Portale: exists at all.
void check_reading_list(const std::string &zim_path, const std::string &list_path)
{
    auto source = std::unique_ptr<zim::FileZimSource>(new zim::FileZimSource(zim_path));
    if (!source->ok())
    {
        std::cerr << "cannot open " << zim_path << std::endl;
        return;
    }

    zim::ZimFile zim(std::move(source));
    if (!zim.open())
    {
        std::cerr << "not a usable ZIM: " << zim.last_error() << std::endl;
        return;
    }

    std::vector<ReadingListSection> sections;
    if (!load_reading_list(list_path, sections))
    {
        std::cerr << "cannot read " << list_path << std::endl;
        return;
    }

    size_t total = 0;
    size_t missing = 0;
    size_t empty = 0;

    for (const auto &section : sections)
    {
        std::cout << "== " << section.name << " (" << section.entries.size() << ")" << std::endl;

        for (const auto &entry : section.entries)
        {
            ++total;

            uint32_t index = 0;
            zim::ZimDirent dirent;
            if (!zim.find_content(entry.path, index, dirent))
            {
                ++missing;
                std::cout << "   MISSING  " << entry.path << "  (" << entry.title << ")"
                          << std::endl;
                continue;
            }

            std::string html;
            if (!zim.read_blob(dirent.cluster, dirent.blob, html) || html.empty())
            {
                ++empty;
                std::cout << "   EMPTY    " << entry.path << std::endl;
                continue;
            }

            // A redirect landing somewhere else is fine, but worth seeing: it usually
            // means the list names an old title.
            if (dirent.path != entry.path)
            {
                std::cout << "   redirect " << entry.path << " -> " << dirent.path << std::endl;
            }
        }
    }

    std::cout << "\n" << total << " entries, " << missing << " missing, " << empty << " empty"
              << std::endl;
}
