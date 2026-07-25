#ifndef ZIM_TOKEN_ITER_H_
#define ZIM_TOKEN_ITER_H_

#include "./wiki_html_parser.h"

#include "doc_api/token_iter.h"

#include <memory>

namespace zim
{

// Walks one article's tokens. Holds the article by shared_ptr because read() hands out
// raw DocToken pointers into it and TokenLineScroller keeps two iterators alive for the
// lifetime of the view.
class ZimTokenIter : public TokenIter
{
public:
    ZimTokenIter(std::shared_ptr<const WikiArticle> article, DocAddr address);
    ZimTokenIter(const ZimTokenIter &other);

    const DocToken *read(int direction) override;
    void seek(DocAddr address) override;
    std::shared_ptr<TokenIter> clone() const override;

private:
    std::shared_ptr<const WikiArticle> article;
    uint32_t index;
};

}

#endif
