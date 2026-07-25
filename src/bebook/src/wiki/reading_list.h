#ifndef READING_LIST_H_
#define READING_LIST_H_

#include <string>
#include <vector>

struct ReadingListEntry
{
    std::string title;
    std::string path;
};

struct ReadingListSection
{
    std::string name;
    std::vector<ReadingListEntry> entries;
};

// Parses the curated list: tab-separated `section, title, zim path`, one per line.
// Blank lines and lines beginning with # are ignored, as are malformed ones -- a single
// bad row should cost one article, not the whole list. Sections keep first-seen order.
std::vector<ReadingListSection> parse_reading_list(const std::string &text);

// False if the file cannot be read. A file that parses to nothing yields an empty list.
bool load_reading_list(const std::string &path, std::vector<ReadingListSection> &out);

#endif
