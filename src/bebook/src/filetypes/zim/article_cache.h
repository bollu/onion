#ifndef ARTICLE_CACHE_H_
#define ARTICLE_CACHE_H_

#include "./wiki_html_parser.h"
#include "./zim_file.h"

#include "util/lru_cache.h"

#include <memory>
#include <string>

namespace zim
{

// Parsed articles, kept so that going back is instant. Without it, every B press
// re-parses the HTML, which on a Cortex-A7 is the difference between instant and a
// visible stall on a large article.
class ArticleCache
{
public:
    // Room for the article being read, a couple of steps of back-history, and the link the
    // cursor is resting on -- prefetching into a cache of three would evict the history it
    // exists to protect. The largest article in the Italian corpus parses to ~276 KB of
    // text, so the extra slots cost single-digit megabytes on a 128 MB device.
    explicit ArticleCache(uint32_t capacity = 5);

    // Never null on success. Returns null if the path is absent or unparseable.
    std::shared_ptr<const WikiArticle> get_or_parse(ZimFile &zim, const std::string &path);

    void clear();

private:
    const uint32_t capacity;
    LRUCache<std::string, std::shared_ptr<const WikiArticle>> cache;
};

}

#endif
