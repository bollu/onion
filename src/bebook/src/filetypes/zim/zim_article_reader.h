#ifndef ZIM_ARTICLE_READER_H_
#define ZIM_ARTICLE_READER_H_

#include "./article_cache.h"
#include "./wiki_html_parser.h"
#include "./zim_file.h"

#include "doc_api/doc_reader.h"

#include <memory>
#include <optional>
#include <string>

namespace zim
{

// A DocReader over ONE article, not the whole archive. The archive has no linear order to
// scroll through -- the token stream has to end where the article does, the TOC has to be
// this article's headings rather than 195k entries, and "34% through Roma" is the progress
// a reader wants. Navigation creates a new reader; the ZimFile and ArticleCache are shared.
class ZimArticleReader : public DocReader
{
public:
    ZimArticleReader(std::shared_ptr<ZimFile> zim,
                     std::shared_ptr<ArticleCache> cache,
                     std::string path);

    using DocReader::open;
    bool open(DocReaderCache &cache) override;
    bool is_open() const override;

    std::string get_id() const override;

    const std::vector<TocItem> &get_table_of_contents() const override;
    TocPosition get_toc_position(const DocAddr &address) const override;
    DocAddr get_toc_item_address(uint32_t toc_item_index) const override;

    uint32_t get_global_progress_percent(const DocAddr &address) const override;

    std::shared_ptr<TokenIter> get_iter(DocAddr address = 0) const override;

    std::vector<char> load_resource(const std::filesystem::path &path) const override;

    const std::string &path() const;
    const std::string &title() const;

    // False when the article parsed to nothing. Kiwix landing pages are link portals
    // built from tables, so the cleaner empties them; opening one shows a blank page.
    bool has_content() const;

    // Canonicalises a link target found in this article, following redirects. Empty if
    // the target is not in the archive, which is how a red link is detected.
    std::string resolve_link(const std::string &target) const;

private:
    std::shared_ptr<ZimFile> zim;
    std::shared_ptr<ArticleCache> cache;
    std::string article_path;
    std::shared_ptr<const WikiArticle> article;
    std::vector<TocItem> empty_toc;
};

}

#endif
