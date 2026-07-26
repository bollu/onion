#include "./epub_metadata.h"
#include "./libxml_iter.h"
#include "./xhtml_string_util.h"
#include "util/str_utils.h"

#include <libxml/parser.h>

#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>

namespace
{
// xmlGetProp returns a heap xmlChar* the caller must xmlFree; every raw call site here used
// to leak it (the library indexer parses every book's OPF, so it added up). This copies the
// value into a std::string and frees it, returning "" when the attribute is absent.
std::string get_prop(xmlNodePtr node, const char *name)
{
    xmlChar *value = xmlGetProp(node, BAD_CAST name);
    if (!value)
    {
        return {};
    }
    std::string out(reinterpret_cast<const char *>(value));
    xmlFree(value);
    return out;
}

// Minimal percent-decoding for epub hrefs ("chapter%201.xhtml" -> "chapter 1.xhtml"), so a
// resource with a space or other reserved character resolves against the zip's real path.
// Kept local so the reader link needn't pull in the ZIM module's copy; malformed escapes are
// left as-is.
std::string decode_href(const std::string &s)
{
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            const int hi = hex(s[i + 1]);
            const int lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}
}  // namespace

NavPoint::NavPoint(const std::string &label)
    : NavPoint(label, "", "")
{
}

NavPoint::NavPoint(const std::string &label, const std::string &src, const std::string &src_absolute)
    : label(label), src(src), src_absolute(src_absolute)
{
}

NavPoint::NavPoint(const std::string &label, const std::string &src, const std::string &src_absolute, std::vector<NavPoint> children)
    : label(label), src(src), src_absolute(src_absolute), children(children)
{
}

bool NavPoint::operator==(const NavPoint &other) const
{
    return label == other.label &&
        src == other.src &&
        src_absolute == other.src_absolute &&
        children == other.children;
}

std::string epub_parse_rootfile_path(const char *container_xml)
{
    xmlDocPtr container_doc = xmlReadMemory(container_xml, strlen(container_xml), nullptr, nullptr, 0);
    if (container_doc == nullptr)
    {
        std::cerr << "Unable to parse container xml" << std::endl;
        return {};
    }

    xmlNodePtr node = xmlDocGetRootElement(container_doc);
    node = elem_first_child(elem_first_by_name(node, BAD_CAST "container"));
    node = elem_first_child(elem_first_by_name(node, BAD_CAST "rootfiles"));
    node = elem_first_by_name(node, BAD_CAST "rootfile");

    std::string rootfile_path;
    if (node)
    {
        std::string full_path = get_prop(node, "full-path");
        std::string media_type = get_prop(node, "media-type");

        if (media_type == "application/oebps-package+xml")
        {
            rootfile_path = full_path;
        }
        else
        {
            std::cerr << "Found unsupported docroot media type: " << media_type << std::endl;
        }
    }
    else
    {
        std::cerr << "Unable to find rootfile element" << std::endl;
    }

    xmlFreeDoc(container_doc);
    
    return rootfile_path;
}

namespace parse_package
{

std::unordered_map<std::string, ManifestItem> parse_package_manifest(const std::filesystem::path &base_path, xmlNodePtr node)
{
    std::unordered_map<std::string, ManifestItem> manifest;

    node = elem_first_child(elem_first_by_name(node, BAD_CAST "package"));
    node = elem_first_child(elem_first_by_name(node, BAD_CAST "manifest"));
    node = elem_first_by_name(node, BAD_CAST "item");

    while (node)
    {
        std::string id = get_prop(node, "id");
        std::string href = get_prop(node, "href");
        std::string media_type = get_prop(node, "media-type");
        std::string properties = get_prop(node, "properties");
        if (!id.empty() && !href.empty() && !media_type.empty())
        {
            // Hrefs are URL-encoded per the epub spec; decode so a file with a space (%20)
            // resolves against the zip's real path.
            std::string href_decoded = decode_href(href);
            manifest.emplace(
                std::move(id),
                ManifestItem{
                    href_decoded,
                    (base_path / href_decoded).lexically_normal(),
                    std::move(media_type),
                    std::move(properties)
                }
            );
        }
        node = elem_next_by_name(node, BAD_CAST "item");
    }

    return manifest;
}

// Concatenated text of an element's immediate text children, whitespace-stripped.
// dc:title and dc:creator are declared as plain text, but some producers wrap parts of
// the value in inline markup.
std::string element_text(xmlNodePtr node)
{
    std::vector<const char *> substrings;
    for (xmlNodePtr child = elem_first_child(node); child; child = child->next)
    {
        if (child->type == XML_TEXT_NODE && child->content)
        {
            substrings.push_back((const char *)child->content);
        }
    }
    return strip_whitespace(join_strings(substrings));
}

// libxml2 reports the local name for namespace-qualified elements ("title" for
// <dc:title>), but leaves the raw qualified name in place when the producer forgot to
// declare the prefix. Accept both spellings.
xmlNodePtr find_dc_element(xmlNodePtr first_child, const char *local_name)
{
    xmlNodePtr node = elem_first_by_name(first_child, BAD_CAST local_name);
    if (node)
    {
        return node;
    }
    return elem_first_by_name(first_child, BAD_CAST ("dc:" + std::string(local_name)).c_str());
}

// Continues a find_dc_element scan. The two spellings never coexist in one document, so
// following whichever name the current node carries keeps the scan consistent.
xmlNodePtr next_dc_element(xmlNodePtr node)
{
    return elem_next_by_name(node, node->name);
}

std::string parse_creator(xmlNodePtr metadata_child)
{
    std::string first_creator;

    xmlNodePtr node = find_dc_element(metadata_child, "creator");
    while (node)
    {
        std::string name = element_text(node);
        if (!name.empty())
        {
            if (get_prop(node, "role") == "aut")
            {
                return name;
            }
            if (first_creator.empty())
            {
                first_creator = name;
            }
        }
        node = next_dc_element(node);
    }

    return first_creator;
}

// The manifest id of the cover image, from the legacy <meta name="cover" content="ID"/>
// convention. Epub 3 books instead flag the manifest item itself, handled by the caller.
std::string parse_meta_cover_id(xmlNodePtr metadata_child)
{
    xmlNodePtr node = elem_first_by_name(metadata_child, BAD_CAST "meta");
    while (node)
    {
        std::string name = get_prop(node, "name");
        std::string content = get_prop(node, "content");
        if (name == "cover" && !content.empty())
        {
            return content;
        }
        node = elem_next_by_name(node, BAD_CAST "meta");
    }
    return {};
}

EpubMetadata parse_package_metadata(
    xmlNodePtr node,
    const std::unordered_map<std::string, ManifestItem> &manifest
)
{
    EpubMetadata metadata;

    node = elem_first_child(elem_first_by_name(node, BAD_CAST "package"));
    node = elem_first_child(elem_first_by_name(node, BAD_CAST "metadata"));

    if (node)
    {
        metadata.title = element_text(find_dc_element(node, "title"));
        metadata.author = parse_creator(node);

        auto cover_id = parse_meta_cover_id(node);
        auto item = manifest.find(cover_id);
        if (item != manifest.end())
        {
            metadata.cover_href = item->second.href_absolute;
        }
    }

    if (metadata.cover_href.empty())
    {
        for (const auto &[id, item]: manifest)
        {
            if (has_token(item.properties, "cover-image"))
            {
                metadata.cover_href = item.href_absolute;
                break;
            }
        }
    }

    return metadata;
}

std::vector<std::string> parse_package_spine(xmlNodePtr node)
{
    std::vector<std::string> spine_ids;

    node = elem_first_child(elem_first_by_name(node, BAD_CAST "package"));
    node = elem_first_child(elem_first_by_name(node, BAD_CAST "spine"));
    node = elem_first_by_name(node, BAD_CAST "itemref");

    while (node)
    {
        std::string idref = get_prop(node, "idref");
        if (!idref.empty())
        {
            spine_ids.push_back(std::move(idref));
        }

        node = elem_next_by_name(node, BAD_CAST "itemref");
    }

    return spine_ids;
}

}  // namespace parse_package

bool epub_parse_package_contents(const std::string &rootfile_path, const char *package_xml, PackageContents &out_package)
{
    xmlDocPtr package_doc = xmlReadMemory(package_xml, strlen(package_xml), nullptr, nullptr, 0);
    if (package_doc == nullptr)
    {
        std::cerr << "Unable to parse package doc" << std::endl;
        return false;
    }

    xmlNodePtr node = xmlDocGetRootElement(package_doc);
    std::filesystem::path base_path = std::filesystem::path(rootfile_path).parent_path();

    out_package.id_to_manifest_item = parse_package::parse_package_manifest(base_path, node);
    out_package.spine_ids = parse_package::parse_package_spine(node);
    out_package.metadata = parse_package::parse_package_metadata(node, out_package.id_to_manifest_item);

    // get toc id from spine
    {
        xmlNodePtr spine = elem_first_by_name(
            elem_first_child(elem_first_by_name(node, BAD_CAST "package")),
            BAD_CAST "spine"
        );
        if (spine)
        {
            std::string toc_attr = get_prop(spine, "toc");
            if (!toc_attr.empty())
            {
                out_package.toc_id = std::move(toc_attr);
            }
        }
    }

    xmlFreeDoc(package_doc);

    return true;
}

namespace parse_ncx
{

void parse_nav_point(const std::filesystem::path &base_path, xmlNodePtr node, std::vector<NavPoint> &out)
{
    node = elem_first_child(node); // get children of navPoint

    std::string label, src;
    {
        xmlNodePtr text_node = elem_first_child(elem_first_by_name(
            elem_first_child(
                elem_first_by_name(node, BAD_CAST "navLabel")
            ),
            BAD_CAST "text"
        ));
        if (!text_node || text_node->type != XML_TEXT_NODE || !text_node->content)
        {
            return;
        }

        label = strip_whitespace((const char*)text_node->content);
        if (label.empty())
        {
            return;
        }
    }
    {
        xmlNodePtr content_node = elem_first_by_name(node, BAD_CAST "content");
        if (!content_node)
        {
            return;
        }
        src = get_prop(content_node, "src");
        if (src.empty())
        {
            return;
        }
    }
    const std::string src_decoded = decode_href(src);
    out.emplace_back(
        label,
        src_decoded,
        (base_path / src_decoded).lexically_normal()
    );

    // Look for child elements
    {
        std::vector<NavPoint> &children = out.back().children;

        node = elem_first_by_name(node, BAD_CAST "navPoint");
        while (node)
        {
            parse_nav_point(base_path, node, children);
            node = elem_next_by_name(node, BAD_CAST "navPoint");
        }
    }
}

void parse_nav_map(const std::filesystem::path &base_path, xmlNodePtr node, std::vector<NavPoint> &out)
{
    node = elem_first_child(node); // get children of navMap
    node = elem_first_by_name(node, BAD_CAST "navPoint");
    while (node)
    {
        parse_nav_point(base_path, node, out);
        node = elem_next_by_name(node, BAD_CAST "navPoint");
    }
}

}  // namespace parse_ncx

bool epub_parse_ncx(const std::string &ncx_file_path, const char *ncx_xml, std::vector<NavPoint> &out_navmap)
{
    xmlDocPtr ncx_doc = xmlReadMemory(ncx_xml, strlen(ncx_xml), nullptr, nullptr, 0);
    if (ncx_doc == nullptr)
    {
        std::cerr << "Unable to parse ncx doc" << std::endl;
        return false;
    }

    std::filesystem::path base_path = std::filesystem::path(ncx_file_path).parent_path();

    xmlNodePtr node = xmlDocGetRootElement(ncx_doc);
    node = elem_first_child(elem_first_by_name(node, BAD_CAST "ncx"));
    node = elem_first_by_name(node, BAD_CAST "navMap");
    bool found_navmap = node != nullptr;

    parse_ncx::parse_nav_map(base_path, node, out_navmap);

    xmlFreeDoc(ncx_doc);

    return found_navmap;
}

namespace parse_nav
{

void _collect_text(xmlNodePtr node, std::vector<const char*> &substrings)
{
    while (node)
    {
        if (node->type == XML_TEXT_NODE)
        {
            if (node->content)
            {
                substrings.push_back((const char*)node->content);
            }
        }
        else if (node->type == XML_ELEMENT_NODE)
        {
            _collect_text(elem_first_child(node), substrings);
        }
        node = node->next;
    }
}

// Grab and join all text inside this node
std::string collect_text(xmlNodePtr node)
{
    std::vector<const char*> substrings;
    _collect_text(node, substrings);

    return compact_strings(substrings);
}

std::optional<NavPoint> try_parse_anchor(xmlNodePtr anchor, const std::filesystem::path &base_path)
{
    if (anchor)
    {
        std::string href = get_prop(anchor, "href");
        if (!href.empty())
        {
            auto label = collect_text(elem_first_child(anchor));
            if (!label.empty())
            {
                const std::string href_decoded = decode_href(href);
                return NavPoint(
                    label,
                    href_decoded,
                    (base_path / href_decoded).lexically_normal()
                );
            }
        }
    }
    return std::nullopt;
}

std::optional<NavPoint> try_parse_span(xmlNodePtr span)
{
    if (span)
    {
        auto label = collect_text(elem_first_child(span));
        if (!label.empty())
        {
            return NavPoint(label);
        }
    }
    return std::nullopt;
}

void parse_ol(xmlNodePtr node, const std::filesystem::path &base_path, std::vector<NavPoint> &out);

void parse_li(xmlNodePtr li_node, const std::filesystem::path &base_path, std::vector<NavPoint> &out)
{
    xmlNodePtr node = elem_first_child(li_node);

    xmlNodePtr anchor_node = elem_first_by_name(node, BAD_CAST "a");
    auto anchor_nav_point = try_parse_anchor(anchor_node, base_path);

    xmlNodePtr span_node = elem_first_by_name(node, BAD_CAST "span");
    auto span_nav_point = try_parse_span(span_node);

    if (anchor_nav_point)
    {
        out.emplace_back(std::move(*anchor_nav_point));
    }
    else if (span_nav_point)
    {
        out.emplace_back(std::move(*span_nav_point));
    }
    else
    {
        std::cerr << "Unable to find <a> or <span> in <li>" << std::endl;
        return;
    }

    // May contain an <ol>
    xmlNodePtr ol_node = elem_first_by_name(node, BAD_CAST "ol");
    if (ol_node)
    {
        parse_ol(ol_node, base_path, out.back().children);
    }
}

void parse_ol(xmlNodePtr ol_node, const std::filesystem::path &base_path, std::vector<NavPoint> &out)
{
    xmlNodePtr node = elem_first_child(ol_node);
    node = elem_first_by_name(node, BAD_CAST "li");

    // Expected to have only <li> elements
    while (node)
    {
        parse_li(node, base_path, out);
        node = elem_next_by_name(node, BAD_CAST "li");
    }
}

}  // namespace parse_nav

// https://www.w3.org/TR/epub/#sec-nav
bool epub_parse_nav(const std::string &nav_file_path, const char *nav_xml, std::vector<NavPoint> &out_navmap)
{
    xmlDocPtr nav_doc = xmlReadMemory(nav_xml, strlen(nav_xml), nullptr, nullptr, 0);
    if (nav_doc == nullptr)
    {
        std::cerr << "Unable to parse nav doc" << std::endl;
        return false;
    }

    std::filesystem::path base_path = std::filesystem::path(nav_file_path).parent_path();

    xmlNodePtr node = xmlDocGetRootElement(nav_doc);
    node = elem_first_child(elem_first_by_name(node, BAD_CAST "html"));
    node = elem_first_child(elem_first_by_name(node, BAD_CAST "body"));
    node = elem_first_by_name(node, BAD_CAST "nav");

    bool found_nav = false;
    while (node)
    {
        if (get_prop(node, "type") == "toc")
        {
            node = elem_first_by_name(elem_first_child(node), BAD_CAST "ol");
            if (node)
            {
                found_nav = true;
                // Should be exactly one <ol> in the <nav>
                parse_nav::parse_ol(node, base_path, out_navmap);
            }
            break;
        }

        node = elem_next_by_name(node, BAD_CAST "nav");
    }

    xmlFreeDoc(nav_doc);

    return found_nav;
}
