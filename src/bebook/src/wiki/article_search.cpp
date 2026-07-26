#include "./article_search.h"

#include "filetypes/zim/zim_file.h"
#include "filetypes/zim/zim_types.h"

#include <set>

namespace wiki
{

std::string normalise_query(const std::string &query)
{
    std::string out;
    out.reserve(query.size());
    for (char c : query)
    {
        out += (c == ' ') ? '_' : c;
    }
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z')
    {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    }
    return out;
}

std::vector<ArticleHit> search_titles(
    const zim::ZimFile &zim, const std::string &query, int max_results)
{
    std::vector<ArticleHit> out;
    if (query.empty() || max_results <= 0)
    {
        return out;
    }

    const std::string prefix = normalise_query(query);
    const char ns = zim.content_namespace();

    uint32_t index = 0;
    if (!zim.lower_bound_path(ns, prefix, index))
    {
        return out;
    }

    // Walk forward while the paths still start with the prefix. Bounded independently of
    // the prefix length so a single-letter query cannot walk the whole archive: once
    // enough have been collected there is no reason to keep reading dirents.
    const uint32_t end = zim.entry_count();
    std::set<std::string> seen;
    for (uint32_t i = index; i < end && static_cast<int>(out.size()) < max_results; ++i)
    {
        zim::ZimDirent d;
        if (!zim.dirent(i, d))
        {
            break;
        }
        if (d.ns != ns || d.path.compare(0, prefix.size(), prefix) != 0)
        {
            // Sorted, so the first miss ends the run.
            break;
        }
        if (d.is_linktarget() || d.is_deleted())
        {
            continue;
        }

        // Redirects are kept: they are the archive's own alternate names for an article,
        // which is exactly what someone half-remembering a title types. What they point at
        // is resolved when it is opened, so the path here stays the one that was matched.
        if (seen.insert(d.path).second)
        {
            out.push_back({ d.path, d.title.empty() ? d.path : d.title });
        }
    }

    return out;
}

}
