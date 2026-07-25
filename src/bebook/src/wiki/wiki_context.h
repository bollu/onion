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

    // The archive's own landing page, or empty if it has none.
    std::string main_page_path() const;

    // Canonical path for a link target, empty when the target is not in the archive.
    std::string resolve(const std::string &target) const;

    const std::string &zim_path() const;

private:
    std::shared_ptr<zim::ZimFile> zim;
    std::shared_ptr<zim::ArticleCache> cache;
    std::string path;
    std::string last_error;
};

#endif
