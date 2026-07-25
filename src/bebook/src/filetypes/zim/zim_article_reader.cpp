#include "./zim_article_reader.h"

#include "./zim_token_iter.h"

#include "filetypes/epub/epub_doc_addr.h"

#include <algorithm>
#include <utility>

namespace zim
{

ZimArticleReader::ZimArticleReader(std::shared_ptr<ZimFile> zim,
                                   std::shared_ptr<ArticleCache> cache,
                                   std::string path)
    : zim(std::move(zim)), cache(std::move(cache)), article_path(std::move(path))
{
}

// The DocReaderCache is deliberately ignored. EPubReader writes a per-book blob keyed by
// get_id(); doing that here would leave one store entry per article ever opened.
bool ZimArticleReader::open(DocReaderCache &)
{
    if (zim == nullptr || cache == nullptr)
    {
        return false;
    }

    article = cache->get_or_parse(*zim, article_path);
    return article != nullptr;
}

bool ZimArticleReader::is_open() const
{
    return article != nullptr;
}

std::string ZimArticleReader::get_id() const
{
    return article_path;
}

const std::vector<TocItem> &ZimArticleReader::get_table_of_contents() const
{
    return article != nullptr ? article->toc : empty_toc;
}

TocPosition ZimArticleReader::get_toc_position(const DocAddr &address) const
{
    if (article == nullptr || article->toc_addrs.empty())
    {
        return {0, get_global_progress_percent(address)};
    }

    const auto &addrs = article->toc_addrs;
    const auto it = std::upper_bound(addrs.begin(), addrs.end(), address);
    const uint32_t index = it == addrs.begin()
        ? 0
        : static_cast<uint32_t>(std::distance(addrs.begin(), it) - 1);

    // Progress within this section, so the title bar tracks the heading it names.
    const uint32_t from = get_text_number(addrs[index]);
    const uint32_t to = (index + 1 < addrs.size())
        ? get_text_number(addrs[index + 1])
        : article->address_width;
    const uint32_t at = get_text_number(address);

    uint32_t percent = 0;
    if (to > from && at > from)
    {
        percent = std::min<uint32_t>(100, (at - from) * 100 / (to - from));
    }

    return {index, percent};
}

DocAddr ZimArticleReader::get_toc_item_address(uint32_t toc_item_index) const
{
    if (article == nullptr || toc_item_index >= article->toc_addrs.size())
    {
        return make_address();
    }
    return article->toc_addrs[toc_item_index];
}

uint32_t ZimArticleReader::get_global_progress_percent(const DocAddr &address) const
{
    if (article == nullptr || article->address_width == 0)
    {
        return 0;
    }
    return std::min<uint32_t>(100, get_text_number(address) * 100 / article->address_width);
}

std::shared_ptr<TokenIter> ZimArticleReader::get_iter(DocAddr address) const
{
    return std::make_shared<ZimTokenIter>(article, address);
}

// No images, so nothing to load.
std::vector<char> ZimArticleReader::load_resource(const std::filesystem::path &) const
{
    return {};
}

const std::string &ZimArticleReader::path() const
{
    return article_path;
}

const std::string &ZimArticleReader::title() const
{
    return article != nullptr ? article->title : article_path;
}

bool ZimArticleReader::has_content() const
{
    return article != nullptr && !article->tokens.empty();
}

std::string ZimArticleReader::resolve_link(const std::string &target) const
{
    if (zim == nullptr || target.empty())
    {
        return {};
    }

    uint32_t index = 0;
    ZimDirent dirent;
    if (!zim->find_content(target, index, dirent))
    {
        return {};
    }
    return dirent.path;
}

}
