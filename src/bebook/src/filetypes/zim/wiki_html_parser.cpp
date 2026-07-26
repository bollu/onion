#include "./wiki_html_parser.h"

#include "./percent_decode.h"

#include "filetypes/epub/epub_doc_addr.h"
#include "filetypes/epub/libxml_iter.h"
#include "filetypes/epub/xhtml_string_util.h"

#include "doc_api/token_addressing.h"
#include "text/styled_text.h"
#include "util/str_utils.h"

#include <libxml/HTMLparser.h>
#include <libxml/parser.h>

#include <cstring>
#include <set>

namespace
{

// Tags that never carry prose we want, or whose content is markup furniture.
const std::set<std::string> SKIP_TAGS = {
    "table", "style", "script", "link", "meta", "sup", "cite", "img", "figure",
    "figcaption", "audio", "video", "map", "math", "base", "noscript", "form", "input",
    "select", "textarea", "button", "svg", "head", "sub",
};

// Whole class tokens on a skipped element. Matched with has_token, so "infobox" does not
// match "not-infobox" and does match "infobox sinottico".
const std::set<std::string> SKIP_CLASSES = {
    "infobox", "sinottico", "navbox", "vertical-navbox", "metadata", "noprint", "thumb",
    "thumbinner", "thumbcaption", "magnify", "mw-editsection", "reference", "references",
    "reflist", "mw-references-wrap", "zim-footer", "gallery", "hatnote", "dablink",
    "toc", "mw-empty-elt", "coordinate", "geo", "nomobile", "sisterproject",
    "portalbox", "navigation-not-searchable", "mw-jump-link", "printfooter",
    "catlinks", "mw-hidden-catlinks", "external",
};

const std::set<std::string> SKIP_IDS = {
    "toc", "catlinks", "footer", "mw-navigation", "siteSub", "contentSub", "jump-to-nav",
};

const std::set<std::string> BLOCKING_TAGS = {
    "address", "article", "aside", "blockquote", "dd", "div", "dl", "dt", "fieldset",
    "footer", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hgroup", "hr", "li", "main",
    "nav", "ol", "p", "pre", "section", "ul", "br", "tr",
};

std::string attr_of(xmlNodePtr node, const char *name)
{
    xmlChar *value = xmlGetProp(node, BAD_CAST name);
    if (value == nullptr)
    {
        return {};
    }
    std::string out(reinterpret_cast<const char *>(value));
    xmlFree(value);
    return out;
}

bool any_token_matches(const std::string &token_list, const std::set<std::string> &wanted)
{
    if (token_list.empty())
    {
        return false;
    }
    for (const auto &token : wanted)
    {
        if (has_token(token_list, token))
        {
            return true;
        }
    }
    return false;
}

int heading_level(const std::string &tag)
{
    if (tag == "h1" || tag == "h2") { return 0; }
    if (tag == "h3") { return 1; }
    if (tag == "h4" || tag == "h5" || tag == "h6") { return 2; }
    return -1;
}

struct Node
{
    enum class Type
    {
        InlineText,
        InlineHeader,
        InlineList,
        InlineBreak,
        SectionSeparator,
    };

    Type type;
    DocAddr address;
    std::string text;
    int list_depth;
    int heading_indent;
    text::Style style;
    std::string link_target;

    bool is_inline() const
    {
        return type == Type::InlineText || type == Type::InlineHeader || type == Type::InlineList;
    }
};

class WikiNodeProcessor
{
    int list_depth = 0;
    int header_depth = 0;
    int heading_indent = 0;
    int emphasis_depth = 0;
    int strong_depth = 0;

    // A stack, because links never nest in practice but malformed markup should not
    // leave a target stuck on for the rest of the article.
    std::vector<std::string> link_stack;

    DocAddr current_address;
    std::vector<Node> nodes;

    text::Style current_style() const
    {
        text::Style style = text::Style::Regular;
        style = text::style_with_italic(style, emphasis_depth > 0);
        style = text::style_with_bold(style, strong_depth > 0);
        return style;
    }

    const std::string &current_link() const
    {
        static const std::string none;
        return link_stack.empty() ? none : link_stack.back();
    }

    void emit(Node::Type type, std::string text = "")
    {
        Node node;
        node.type = type;
        node.address = current_address;
        node.text = std::move(text);
        node.list_depth = list_depth;
        node.heading_indent = heading_indent;
        node.style = current_style();
        node.link_target = current_link();
        nodes.push_back(std::move(node));
    }

public:
    explicit WikiNodeProcessor(DocAddr start)
        : current_address(start)
    {
    }

    void on_text_node(xmlNodePtr node)
    {
        if (node->content == nullptr || xmlStrlen(node->content) == 0)
        {
            return;
        }

        Node::Type type = Node::Type::InlineText;
        if (header_depth > 0)
        {
            type = Node::Type::InlineHeader;
        }
        else if (list_depth > 0)
        {
            type = Node::Type::InlineList;
        }

        const char *content = reinterpret_cast<const char *>(node->content);
        emit(type, content);
        current_address += get_address_width(content);
    }

    // False means skip this element and everything inside it.
    bool on_enter_element(xmlNodePtr node)
    {
        const std::string tag = to_lower(reinterpret_cast<const char *>(node->name));
        const std::string klass = attr_of(node, "class");
        const std::string id = attr_of(node, "id");

        if (zim::wiki_element_is_skipped(tag, klass, id))
        {
            return false;
        }

        if (BLOCKING_TAGS.count(tag) > 0)
        {
            emit(Node::Type::InlineBreak);
        }

        const int level = heading_level(tag);
        if (level >= 0)
        {
            emit(Node::Type::SectionSeparator);
            heading_indent = level;
            ++header_depth;
        }
        else if (tag == "ul" || tag == "ol")
        {
            if (list_depth == 0)
            {
                emit(Node::Type::SectionSeparator);
            }
            ++list_depth;
        }
        else if (tag == "p")
        {
            // No separator between paragraphs. A blank line costs a whole line of an
            // eleven-line screen, so a page of short paragraphs spent most of itself on
            // gaps. The first-line indent marks a new paragraph instead, which is what a
            // printed book does and what the space here is worth. Headings and lists keep
            // their separator, because those are real breaks in the article rather than
            // the next sentence of the same thought.
        }
        else if (tag == "em" || tag == "i" || tag == "dfn" || tag == "var")
        {
            ++emphasis_depth;
        }
        else if (tag == "strong" || tag == "b")
        {
            ++strong_depth;
        }
        else if (tag == "a")
        {
            link_stack.push_back(zim::href_to_zim_path(attr_of(node, "href")));
        }

        return true;
    }

    void on_exit_element(xmlNodePtr node)
    {
        const std::string tag = to_lower(reinterpret_cast<const char *>(node->name));

        const int level = heading_level(tag);
        if (level >= 0)
        {
            emit(Node::Type::SectionSeparator);
            if (header_depth > 0) { --header_depth; }
        }
        else if (tag == "ul" || tag == "ol")
        {
            if (list_depth > 0) { --list_depth; }
            if (list_depth == 0)
            {
                emit(Node::Type::SectionSeparator);
            }
        }
        else if (tag == "p")
        {
            // Closing a paragraph is not a break either; see the open tag above.
        }
        else if (tag == "em" || tag == "i" || tag == "dfn" || tag == "var")
        {
            if (emphasis_depth > 0) { --emphasis_depth; }
        }
        else if (tag == "strong" || tag == "b")
        {
            if (strong_depth > 0) { --strong_depth; }
        }
        else if (tag == "a")
        {
            if (!link_stack.empty()) { link_stack.pop_back(); }
        }

        if (BLOCKING_TAGS.count(tag) > 0)
        {
            emit(Node::Type::InlineBreak);
        }
    }

    const std::vector<Node> &get_nodes() const { return nodes; }
    DocAddr end_address() const { return current_address; }
};

void visit(xmlNodePtr node, WikiNodeProcessor &processor)
{
    while (node != nullptr)
    {
        bool descend = true;

        if (node->type == XML_TEXT_NODE)
        {
            processor.on_text_node(node);
        }
        else if (node->type == XML_ELEMENT_NODE)
        {
            descend = processor.on_enter_element(node);
        }

        if (descend)
        {
            visit(node->children, processor);
            if (node->type == XML_ELEMENT_NODE)
            {
                processor.on_exit_element(node);
            }
        }

        node = node->next;
    }
}

// Groups the per-node targets of one paragraph into byte ranges over the joined text.
// Adjacent nodes sharing a target merge, so <a>foo <i>bar</i></a> is one run; adjacent
// nodes with different targets never do.
std::vector<LinkRun> build_link_runs(const std::vector<Node> &nodes,
                                     uint32_t first,
                                     uint32_t group_size,
                                     const std::vector<uint32_t> &node_offsets,
                                     const std::string &text,
                                     uint32_t text_size)
{
    std::vector<LinkRun> runs;

    for (uint32_t j = 0; j < group_size; ++j)
    {
        const std::string &target = nodes[first + j].link_target;
        if (target.empty())
        {
            continue;
        }

        const uint32_t start = node_offsets[j];
        const uint32_t end = (j + 1 < group_size) ? node_offsets[j + 1] : text_size;
        if (end <= start)
        {
            continue;
        }

        if (!runs.empty() && runs.back().target == target &&
            runs.back().offset + runs.back().length == start)
        {
            runs.back().length += end - start;
        }
        else
        {
            runs.push_back(LinkRun{start, end - start, target});
        }
    }

    // Trim the separator space compact_strings inserts between nodes, or an underline
    // runs a space past the anchor text it marks.
    for (auto &run : runs)
    {
        while (run.length > 0 && text[run.offset + run.length - 1] == ' ')
        {
            --run.length;
        }
    }
    return runs;
}

void generate_tokens(const std::vector<Node> &nodes, zim::WikiArticle &out)
{
    auto group_size_at = [&nodes](uint32_t i) -> uint32_t {
        if (!nodes[i].is_inline())
        {
            return 1;
        }
        const Node::Type head_type = nodes[i].type;
        uint32_t size = 1;
        while (++i < nodes.size() && nodes[i].type == head_type)
        {
            ++size;
        }
        return size;
    };

    const uint32_t n = static_cast<uint32_t>(nodes.size());
    bool separator_allowed = false;
    uint32_t i = 0;

    while (i < n)
    {
        const uint32_t group_size = group_size_at(i);
        const Node &head = nodes[i];

        switch (head.type)
        {
            case Node::Type::InlineText:
            case Node::Type::InlineHeader:
            case Node::Type::InlineList:
            {
                std::vector<const char *> substrings;
                substrings.reserve(group_size);
                for (uint32_t j = i; j < i + group_size; ++j)
                {
                    substrings.push_back(nodes[j].text.c_str());
                }

                std::vector<uint32_t> node_offsets;
                const std::string text = compact_strings(substrings, node_offsets);
                if (strip_whitespace(text).empty())
                {
                    break;
                }

                const uint32_t text_size = static_cast<uint32_t>(text.size());

                std::vector<text::StyleRun> style_runs;
                style_runs.reserve(group_size);
                for (uint32_t j = 0; j < group_size; ++j)
                {
                    const uint32_t start = node_offsets[j];
                    const uint32_t end = (j + 1 < group_size) ? node_offsets[j + 1] : text_size;
                    if (end > start)
                    {
                        style_runs.push_back(text::StyleRun{start, end - start, nodes[i + j].style});
                    }
                }
                text::normalize_runs(style_runs);
                if (style_runs.size() == 1 && style_runs[0].style == text::Style::Regular)
                {
                    style_runs.clear();
                }

                std::vector<LinkRun> link_runs =
                    build_link_runs(nodes, i, group_size, node_offsets, text, text_size);

                if (head.type == Node::Type::InlineText)
                {
                    out.tokens.push_back(std::make_unique<TextDocToken>(
                        head.address, text, std::move(style_runs), std::move(link_runs)));
                }
                else if (head.type == Node::Type::InlineHeader)
                {
                    out.toc.push_back(TocItem{strip_whitespace(text),
                                              static_cast<uint32_t>(head.heading_indent)});
                    out.toc_addrs.push_back(head.address);

                    // Bold the whole heading. Centring alone did not read as a heading --
                    // a short centred line looks like a short line -- and an article is
                    // mostly navigated by its sections, so they have to be findable while
                    // scrolling past.
                    style_runs.clear();
                    style_runs.push_back({ 0u, static_cast<uint32_t>(text.size()),
                                           text::Style::Bold });

                    out.tokens.push_back(std::make_unique<HeaderDocToken>(
                        head.address, text, std::move(style_runs), std::move(link_runs)));
                }
                else
                {
                    out.tokens.push_back(std::make_unique<ListItemDocToken>(
                        head.address, text, head.list_depth, std::move(style_runs),
                        std::move(link_runs)));
                }

                separator_allowed = true;
                break;
            }

            case Node::Type::SectionSeparator:
                if (separator_allowed)
                {
                    out.tokens.push_back(std::make_unique<TextDocToken>(head.address, ""));
                    separator_allowed = false;
                }
                break;

            case Node::Type::InlineBreak:
                break;
        }

        i += group_size;
    }
}

// The article body, preferring Parsoid's own wrapper and degrading to <body>.
xmlNodePtr find_content_root(xmlNodePtr root)
{
    for (xmlNodePtr node = root; node != nullptr; node = node->next)
    {
        if (node->type == XML_ELEMENT_NODE)
        {
            const std::string klass = attr_of(node, "class");
            const std::string id = attr_of(node, "id");
            if (has_token(klass, "mw-parser-output") || id == "mw-content-text")
            {
                return node;
            }
        }

        xmlNodePtr found = find_content_root(node->children);
        if (found != nullptr)
        {
            return found;
        }
    }
    return nullptr;
}

std::string extract_title(xmlNodePtr root)
{
    for (xmlNodePtr node = root; node != nullptr; node = node->next)
    {
        if (node->type == XML_ELEMENT_NODE)
        {
            const std::string tag = to_lower(reinterpret_cast<const char *>(node->name));
            const std::string id = attr_of(node, "id");
            if (tag == "title" || id == "firstHeading")
            {
                xmlChar *content = xmlNodeGetContent(node);
                if (content != nullptr)
                {
                    std::string out = strip_whitespace(reinterpret_cast<const char *>(content));
                    xmlFree(content);
                    if (!out.empty())
                    {
                        return out;
                    }
                }
            }
        }

        const std::string found = extract_title(node->children);
        if (!found.empty())
        {
            return found;
        }
    }
    return {};
}

}

namespace zim
{

bool wiki_element_is_skipped(const std::string &tag, const std::string &klass, const std::string &id)
{
    if (SKIP_TAGS.count(tag) > 0)
    {
        return true;
    }
    if (!id.empty() && SKIP_IDS.count(id) > 0)
    {
        return true;
    }
    return any_token_matches(klass, SKIP_CLASSES);
}

bool parse_wiki_article(const char *html, const std::string &path, WikiArticle &out)
{
    if (html == nullptr)
    {
        return false;
    }

    // htmlReadMemory rather than xmlReadMemory: Parsoid output carries void elements and
    // named entities that the XML parser rejects even in recover mode.
    htmlDocPtr doc = htmlReadMemory(html, static_cast<int>(std::strlen(html)), nullptr, "UTF-8",
                                    HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING |
                                    HTML_PARSE_RECOVER | HTML_PARSE_NONET);
    if (doc == nullptr)
    {
        return false;
    }

    xmlNodePtr root = xmlDocGetRootElement(doc);

    out.path = path;
    out.title = extract_title(root);
    if (out.title.empty())
    {
        out.title = path;
    }

    xmlNodePtr content = find_content_root(root);
    if (content != nullptr)
    {
        content = content->children;
    }
    else
    {
        // elem_first_by_name scans siblings, so <body> takes two steps down from <html>.
        xmlNodePtr html = elem_first_by_name(root, BAD_CAST "html");
        xmlNodePtr body = elem_first_by_name(elem_first_child(html), BAD_CAST "body");
        content = elem_first_child(body);
        if (content == nullptr)
        {
            content = root;
        }
    }

    WikiNodeProcessor processor(make_address(0));
    visit(content, processor);
    generate_tokens(processor.get_nodes(), out);

    out.address_width = get_text_number(processor.end_address());

    xmlFreeDoc(doc);
    return true;
}

}
