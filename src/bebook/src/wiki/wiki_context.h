#ifndef WIKI_CONTEXT_H_
#define WIKI_CONTEXT_H_

#include "filetypes/zim/article_cache.h"
#include "filetypes/zim/zim_article_reader.h"
#include "filetypes/zim/zim_file.h"

#include <memory>
#include <string>

// The open archive and its caches, shared by every article reader so that the decompressed
// clusters and parsed articles survive navigation.
class WikiContext
{
public:
    WikiContext();

    bool open(const std::string &zim_path);
    bool is_open() const;
    const std::string &error() const;

    // Null if the article is missing or unparseable.
    std::shared_ptr<zim::ZimArticleReader> open_article(const std::string &path);

    // The open archive, for the title search. Null before open() succeeds. Borrowed, not
    // owned: the search reads dirents and holds nothing.
    zim::ZimFile *zim_file() const;

private:
    std::shared_ptr<zim::ZimFile> zim;
    std::shared_ptr<zim::ArticleCache> cache;
    std::string path;
    std::string last_error;
};

#endif
