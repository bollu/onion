#include "./zim_token_iter.h"

#include <algorithm>
#include <utility>

namespace zim
{

ZimTokenIter::ZimTokenIter(std::shared_ptr<const WikiArticle> article, DocAddr address)
    : article(std::move(article)), index(0)
{
    seek(address);
}

ZimTokenIter::ZimTokenIter(const ZimTokenIter &other)
    : TokenIter(other), article(other.article), index(other.index)
{
}

const DocToken *ZimTokenIter::read(int direction)
{
    if (article == nullptr)
    {
        return nullptr;
    }

    const uint32_t count = static_cast<uint32_t>(article->tokens.size());

    if (direction < 0)
    {
        if (index == 0)
        {
            return nullptr;
        }
        --index;
        return article->tokens[index].get();
    }

    if (index >= count)
    {
        return nullptr;
    }
    return article->tokens[index++].get();
}

void ZimTokenIter::seek(DocAddr address)
{
    index = 0;
    if (article == nullptr)
    {
        return;
    }

    // The last token at or before `address`. Binary rather than linear: tokens are
    // address-sorted by construction, and the scroller seeks on every page jump and every
    // TOC selection, which on a large article walked thousands of tokens each time.
    const auto &tokens = article->tokens;
    const auto it = std::upper_bound(
        tokens.begin(), tokens.end(), address,
        [](DocAddr value, const std::unique_ptr<DocToken> &token) {
            return value < token->address;
        });

    if (it != tokens.begin())
    {
        index = static_cast<uint32_t>(std::distance(tokens.begin(), it) - 1);
    }
}

std::shared_ptr<TokenIter> ZimTokenIter::clone() const
{
    return std::make_shared<ZimTokenIter>(*this);
}

}
