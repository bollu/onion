#ifndef WIKI_HTML_PARSER_H_
#define WIKI_HTML_PARSER_H_

#include "doc_api/doc_reader.h"
#include "doc_api/doc_token.h"

#include <memory>
#include <string>
#include <vector>

namespace zim
{

// One article, parsed once and then read-only. Shared by pointer because the token
// iterators hand out raw DocToken pointers into `tokens`.
struct WikiArticle
{
    std::string path;
    std::string title;

    std::vector<std::unique_ptr<DocToken>> tokens;

    // Section headings, parallel arrays: toc[i] is at toc_addrs[i].
    std::vector<TocItem> toc;
    std::vector<DocAddr> toc_addrs;

    uint32_t address_width = 0;
};

// Converts one article's Parsoid HTML into tokens, dropping the furniture Wikipedia wraps
// prose in: tables and infoboxes, reference superscripts, navboxes, coordinates, images.
bool parse_wiki_article(const char *html, const std::string &path, WikiArticle &out);

// Exposed for tests: true if this tag/class/id combination is furniture, not prose.
bool wiki_element_is_skipped(const std::string &tag, const std::string &klass, const std::string &id);

}

#endif
