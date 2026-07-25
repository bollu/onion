#include "./zim_token_iter.h"

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

    // The last token at or before `address`, matching how the epub iterator seeks.
    const uint32_t count = static_cast<uint32_t>(article->tokens.size());
    for (uint32_t i = 0; i < count; ++i)
    {
        const DocAddr at = article->tokens[i]->address;
        if (at <= address)
        {
            index = i;
        }
        if (at >= address)
        {
            break;
        }
    }
}

std::shared_ptr<TokenIter> ZimTokenIter::clone() const
{
    return std::make_shared<ZimTokenIter>(*this);
}

}
