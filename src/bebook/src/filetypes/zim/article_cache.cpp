#include "./article_cache.h"

namespace zim
{

ArticleCache::ArticleCache(uint32_t capacity)
    : capacity(capacity == 0 ? 1 : capacity)
{
}

std::shared_ptr<const WikiArticle> ArticleCache::get_or_parse(ZimFile &zim, const std::string &path)
{
    // has() first: LRUCache::operator[] default-constructs an iterator on a miss and
    // then dereferences it.
    if (cache.has(path))
    {
        return cache[path];
    }

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
