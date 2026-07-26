#include "./article_cache.h"

#include <cstdio>
#include <cstdlib>

namespace zim
{

ArticleCache::ArticleCache(uint32_t capacity)
    : capacity(capacity == 0 ? 1 : capacity)
{
}

std::shared_ptr<const WikiArticle> ArticleCache::get_or_parse(ZimFile &zim, const std::string &path)
{
    // ZIM_TRACE_CACHE=1 prints every lookup, which is how the prefetch is verified: a
    // dwell should MISS (paying the parse off the keypress) and the follow that comes
    // after it should HIT. Without the trace the two are indistinguishable from outside.
    const bool trace = getenv("ZIM_TRACE_CACHE") != nullptr;

    // has() first: LRUCache::operator[] default-constructs an iterator on a miss and
    // then dereferences it.
    if (cache.has(path))
    {
        if (trace) fprintf(stderr, "[cache] HIT  %s\n", path.c_str());
        return cache[path];
    }
    if (trace) fprintf(stderr, "[cache] MISS %s\n", path.c_str());

    std::string html;
    if (!zim.read_content(path, html))
    {
        return nullptr;
    }

    auto article = std::make_shared<WikiArticle>();
    if (!parse_wiki_article(html.c_str(), path, *article))
    {
        return nullptr;
    }

    while (cache.size() >= capacity)
    {
        cache.pop();
    }
    cache.put(path, article);

    return article;
}

void ArticleCache::clear()
{
    while (cache.size() > 0)
    {
        cache.pop();
    }
}

}
