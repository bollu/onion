#ifndef WIKI_ARTICLE_SEARCH_H_
#define WIKI_ARTICLE_SEARCH_H_

#include <string>
#include <vector>

namespace zim { class ZimFile; }

namespace wiki
{

struct ArticleHit
{
    std::string path;   // what to open
    std::string title;  // what to show
};

// Turn what was typed into the shape a ZIM path has: spaces become underscores, and the
// first letter is capitalised, because Wikipedia titles are and the path list is sorted
// case-sensitively -- "roma" would land past every capitalised entry and match nothing.
std::string normalise_query(const std::string &query);

// Articles whose path starts with `query`, nearest first, capped at `max_results`.
//
// A prefix walk over the path pointer list rather than a full-text index: the list is
// already sorted by path, so this is a binary search and a short walk, with nothing built
// or held in memory. That matters on a device where the archive has ~194k entries and the
// search runs on every keystroke.
std::vector<ArticleHit> search_titles(
    const zim::ZimFile &zim, const std::string &query, int max_results = 12);

}

#endif
