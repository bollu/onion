#include "./reading_list.h"

#include "util/str_utils.h"

#include <fstream>
#include <sstream>

namespace
{

// Splits on tabs without collapsing them, so an empty field stays empty rather than
// shifting every later column left.
std::vector<std::string> split_tabs(const std::string &line)
{
    std::vector<std::string> fields;
    size_t start = 0;
    while (true)
    {
        const size_t tab = line.find('\t', start);
        if (tab == std::string::npos)
        {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return fields;
}

}

std::vector<ReadingListSection> parse_reading_list(const std::string &text)
{
    std::vector<ReadingListSection> sections;
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line))
    {
        // Tolerate CRLF, since the list is hand-edited and may round-trip through Windows.
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        const std::string trimmed = strip_whitespace(line);
        if (trimmed.empty() || trimmed[0] == '#')
        {
            continue;
        }

        const auto fields = split_tabs(line);
        if (fields.size() < 3)
        {
            continue;
        }

        const std::string section = strip_whitespace(fields[0]);
        const std::string title = strip_whitespace(fields[1]);
        const std::string path = strip_whitespace(fields[2]);
        if (section.empty() || title.empty() || path.empty())
        {
            continue;
        }

        auto it = sections.begin();
        for (; it != sections.end(); ++it)
        {
            if (it->name == section)
            {
                break;
            }
        }
        if (it == sections.end())
        {
            sections.push_back(ReadingListSection{section, {}});
            it = sections.end() - 1;
        }

        it->entries.push_back(ReadingListEntry{title, path});
    }

    return sections;
}

bool load_reading_list(const std::string &path, std::vector<ReadingListSection> &out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = parse_reading_list(buffer.str());
    return true;
}
