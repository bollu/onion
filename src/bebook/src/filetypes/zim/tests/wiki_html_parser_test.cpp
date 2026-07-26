#include "../wiki_html_parser.h"

#include "doc_api/doc_token.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace zim;

namespace
{

std::string wrap(const std::string &body)
{
    return "<html><head><title>Test</title></head><body>"
           "<div class=\"mw-content-ltr mw-parser-output\">" +
           body +
           "</div></body></html>";
}

WikiArticle parse(const std::string &body)
{
    WikiArticle out;
    EXPECT_TRUE(parse_wiki_article(wrap(body).c_str(), "Test_Path", out));
    return out;
}

struct Piece
{
    TokenType type;
    std::string text;
    std::vector<LinkRun> links;
    std::vector<text::StyleRun> styles;
    int nest_level = 0;
};

// Flattens the token stream, dropping the empty separator tokens.
std::vector<Piece> pieces_of(const WikiArticle &article)
{
    std::vector<Piece> out;
    for (const auto &token : article.tokens)
    {
        Piece p;
        p.type = token->type;

        if (token->type == TokenType::Text)
        {
            const auto *t = static_cast<const TextDocToken *>(token.get());
            p.text = t->text;
            p.links = t->link_runs;
            p.styles = t->style_runs;
        }
        else if (token->type == TokenType::Header)
        {
            const auto *t = static_cast<const HeaderDocToken *>(token.get());
            p.text = t->text;
            p.links = t->link_runs;
            p.styles = t->style_runs;
        }
        else if (token->type == TokenType::ListItem)
        {
            const auto *t = static_cast<const ListItemDocToken *>(token.get());
            p.text = t->text;
            p.links = t->link_runs;
            p.styles = t->style_runs;
            p.nest_level = t->nest_level;
        }
        else
        {
            continue;
        }

        if (!p.text.empty())
        {
            out.push_back(p);
        }
    }
    return out;
}

// What a link run actually covers, which is the assertion that matters.
std::string covered(const Piece &p, size_t i)
{
    return p.text.substr(p.links[i].offset, p.links[i].length);
}

}

TEST(WikiHtmlParser, ExtractsProseAndTitle)
{
    const WikiArticle a = parse("<p>Roma &egrave; una citt&agrave;.</p>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(a.title, "Test");
    ASSERT_EQ(pieces.size(), 1u);
    ASSERT_EQ(pieces[0].type, TokenType::Text);
    ASSERT_EQ(pieces[0].text, "Roma è una città.");
}

TEST(WikiHtmlParser, BuildsLinkRunsWithExactOffsets)
{
    const WikiArticle a = parse(
        "<p>Roma è la <a href=\"Capitale_(citt%C3%A0)\">capitale</a> della "
        "<a href=\"./Italia\">Repubblica Italiana</a>.</p>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(pieces.size(), 1u);
    ASSERT_EQ(pieces[0].text, "Roma è la capitale della Repubblica Italiana.");
    ASSERT_EQ(pieces[0].links.size(), 2u);

    ASSERT_EQ(covered(pieces[0], 0), "capitale");
    ASSERT_EQ(pieces[0].links[0].target, "Capitale_(città)") << "percent-decoded";

    ASSERT_EQ(covered(pieces[0], 1), "Repubblica Italiana");
    ASSERT_EQ(pieces[0].links[1].target, "Italia") << "leading ./ stripped";
}

TEST(WikiHtmlParser, LinkOffsetsAreBytesNotCharacters)
{
    const WikiArticle a = parse("<p>Una <a href=\"Perch%C3%A9\">città perché</a> qui.</p>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(pieces.size(), 1u);
    ASSERT_EQ(covered(pieces[0], 0), "città perché");
    ASSERT_EQ(pieces[0].links[0].length, 14u) << "12 characters, 14 bytes";
    ASSERT_EQ(pieces[0].links[0].target, "Perché");
}

TEST(WikiHtmlParser, AdjacentLinksToDifferentTargetsStaySeparate)
{
    const WikiArticle a = parse(
        "<p><a href=\"Alpha\">uno</a><a href=\"Beta\">due</a></p>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(pieces.size(), 1u);
    ASSERT_EQ(pieces[0].text, "unodue");
    ASSERT_EQ(pieces[0].links.size(), 2u) << "merging these would retarget one of them";
    ASSERT_EQ(pieces[0].links[0].target, "Alpha");
    ASSERT_EQ(pieces[0].links[1].target, "Beta");
    ASSERT_EQ(covered(pieces[0], 0), "uno");
    ASSERT_EQ(covered(pieces[0], 1), "due");
}

TEST(WikiHtmlParser, OneLinkSpanningInlineMarkupIsASingleRun)
{
    const WikiArticle a = parse("<p><a href=\"Alpha\">uno <i>due</i> tre</a> fuori</p>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(pieces.size(), 1u);
    ASSERT_EQ(pieces[0].text, "uno due tre fuori");
    ASSERT_EQ(pieces[0].links.size(), 1u) << "three text nodes, one anchor";
    ASSERT_EQ(covered(pieces[0], 0), "uno due tre");
}

TEST(WikiHtmlParser, LinkAndStyleRunsOverlapWithoutCoinciding)
{
    const WikiArticle a = parse("<p>a <i>b <a href=\"Alpha\">c</a> d</i> e</p>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(pieces.size(), 1u);
    ASSERT_EQ(pieces[0].text, "a b c d e");
    ASSERT_EQ(pieces[0].links.size(), 1u);
    ASSERT_EQ(covered(pieces[0], 0), "c");

    // The italic span is wider than the link; the two must be tracked independently.
    ASSERT_FALSE(pieces[0].styles.empty());
    bool has_italic = false;
    for (const auto &run : pieces[0].styles)
    {
        if (run.style == text::Style::Italic)
        {
            has_italic = true;
            ASSERT_LE(run.offset, pieces[0].links[0].offset);
            ASSERT_GE(run.offset + run.length, pieces[0].links[0].offset + pieces[0].links[0].length);
        }
    }
    ASSERT_TRUE(has_italic);
}

TEST(WikiHtmlParser, IgnoresExternalAndFragmentLinks)
{
    const WikiArticle a = parse(
        "<p>Vedi <a href=\"https://example.org\">fuori</a> e "
        "<a href=\"#Storia\">sotto</a> e <a href=\"Roma\">Roma</a>.</p>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(pieces.size(), 1u);
    ASSERT_EQ(pieces[0].text, "Vedi fuori e sotto e Roma.") << "the text stays, the link goes";
    ASSERT_EQ(pieces[0].links.size(), 1u);
    ASSERT_EQ(pieces[0].links[0].target, "Roma");
}

TEST(WikiHtmlParser, DropsInfoboxTables)
{
    const WikiArticle a = parse(
        "<table class=\"infobox sinottico\"><tbody><tr><th>Stato</th>"
        "<td><a href=\"Italia\">Italia</a></td></tr></tbody></table>"
        "<p>Prosa vera.</p>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(pieces.size(), 1u);
    ASSERT_EQ(pieces[0].text, "Prosa vera.");
    ASSERT_TRUE(pieces[0].links.empty()) << "the infobox link must not survive";
}

TEST(WikiHtmlParser, DropsReferenceSuperscriptsWithoutEatingSpaces)
{
    const WikiArticle a = parse(
        "<p>Roma<sup class=\"reference\"><a href=\"#cite_note-1\">[1]</a></sup> "
        "è antica.</p>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(pieces.size(), 1u);
    ASSERT_EQ(pieces[0].text, "Roma è antica.") << "no [1], and exactly one space";
}

TEST(WikiHtmlParser, DropsEditSectionsAndNavboxes)
{
    const WikiArticle a = parse(
        "<p>Prima<span class=\"mw-editsection\">[modifica]</span>.</p>"
        "<div class=\"navbox\"><p>Navigazione</p></div>"
        "<div class=\"thumb\"><p>Didascalia</p></div>"
        "<p>Dopo.</p>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(pieces.size(), 2u);
    ASSERT_EQ(pieces[0].text, "Prima.");
    ASSERT_EQ(pieces[1].text, "Dopo.");
}

TEST(WikiHtmlParser, BuildsTheTocFromHeadings)
{
    const WikiArticle a = parse(
        "<p>Intro.</p>"
        "<h2 id=\"Storia\">Storia</h2><p>Testo.</p>"
        "<h3 id=\"Antica\">Età antica</h3><p>Testo.</p>"
        "<h4 id=\"Dettaglio\">Dettaglio</h4><p>Testo.</p>"
        "<h2 id=\"Clima\">Clima</h2><p>Testo.</p>");

    ASSERT_EQ(a.toc.size(), 4u);
    ASSERT_EQ(a.toc_addrs.size(), a.toc.size()) << "the arrays are parallel";

    ASSERT_EQ(a.toc[0].display_name, "Storia");
    ASSERT_EQ(a.toc[0].indent_level, 0u);
    ASSERT_EQ(a.toc[1].display_name, "Età antica");
    ASSERT_EQ(a.toc[1].indent_level, 1u);
    ASSERT_EQ(a.toc[2].display_name, "Dettaglio");
    ASSERT_EQ(a.toc[2].indent_level, 2u);
    ASSERT_EQ(a.toc[3].display_name, "Clima");
    ASSERT_EQ(a.toc[3].indent_level, 0u);

    // Addresses advance through the article, so the TOC can be binary searched.
    for (size_t i = 1; i < a.toc_addrs.size(); ++i)
    {
        ASSERT_LT(a.toc_addrs[i - 1], a.toc_addrs[i]);
    }
}

TEST(WikiHtmlParser, EmitsHeadingsAsHeaderTokens)
{
    const WikiArticle a = parse("<h2>Storia</h2><p>Testo.</p>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(pieces.size(), 2u);
    ASSERT_EQ(pieces[0].type, TokenType::Header);
    ASSERT_EQ(pieces[0].text, "Storia");
    ASSERT_EQ(pieces[1].type, TokenType::Text);
}

TEST(WikiHtmlParser, HeadingsAreBold)
{
    // Centring alone did not read as a heading -- a short centred line just looks like a
    // short line -- and an article is mostly navigated by its sections.
    const WikiArticle a = parse("<h2>Storia</h2><p>Testo.</p>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(pieces[0].type, TokenType::Header);
    ASSERT_EQ(pieces[0].styles.size(), 1u);
    ASSERT_EQ(pieces[0].styles[0].offset, 0u);
    ASSERT_EQ(pieces[0].styles[0].length, pieces[0].text.size());
    ASSERT_EQ(pieces[0].styles[0].style, text::Style::Bold);

    ASSERT_TRUE(pieces[1].styles.empty()) << "body text is not touched";
}

TEST(WikiHtmlParser, EmitsListItemsWithLinks)
{
    const WikiArticle a = parse(
        "<ul><li>Primo</li><li>Secondo con <a href=\"Roma\">Roma</a></li></ul>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(pieces.size(), 2u);
    ASSERT_EQ(pieces[0].type, TokenType::ListItem);
    ASSERT_EQ(pieces[0].text, "Primo");
    ASSERT_EQ(pieces[1].text, "Secondo con Roma");

    ASSERT_EQ(pieces[1].links.size(), 1u);
    ASSERT_EQ(covered(pieces[1], 0), "Roma")
        << "offsets are pre-bullet; the bullet shift happens at layout time";
    ASSERT_EQ(pieces[1].links[0].target, "Roma");
}

TEST(WikiHtmlParser, TrimsTrailingSpaceFromLinkRuns)
{
    // The separator compact_strings inserts between nodes must not be underlined, or the
    // rule runs a space past the anchor text it marks.
    const WikiArticle a = parse("<p><a href=\"Roma\">Roma</a> e il Lazio</p>");
    const auto pieces = pieces_of(a);

    ASSERT_EQ(pieces.size(), 1u);
    ASSERT_EQ(pieces[0].links.size(), 1u);
    ASSERT_EQ(covered(pieces[0], 0), "Roma") << "no trailing space";
}

TEST(WikiHtmlParser, FallsBackToBodyWithoutAParserOutputWrapper)
{
    WikiArticle a;
    ASSERT_TRUE(parse_wiki_article(
        "<html><body><p>Senza wrapper.</p></body></html>", "Test_Path", a));

    const auto pieces = pieces_of(a);
    ASSERT_EQ(pieces.size(), 1u);
    ASSERT_EQ(pieces[0].text, "Senza wrapper.");
}

TEST(WikiHtmlParser, TitleFallsBackToThePath)
{
    WikiArticle a;
    ASSERT_TRUE(parse_wiki_article("<html><body><p>Ciao.</p></body></html>", "Impero_romano", a));
    ASSERT_EQ(a.title, "Impero_romano");
}

TEST(WikiHtmlParser, HandlesEmptyAndMalformedInput)
{
    WikiArticle a;
    ASSERT_FALSE(parse_wiki_article(nullptr, "X", a));

    WikiArticle b;
    parse_wiki_article("", "X", b);
    ASSERT_TRUE(b.tokens.empty());

    WikiArticle c;
    ASSERT_TRUE(parse_wiki_article("<p>Non chiuso <b>grassetto", "X", c));
    ASSERT_FALSE(pieces_of(c).empty()) << "recover mode should still yield the text";
}

TEST(WikiElementIsSkipped, MatchesWholeClassTokensOnly)
{
    ASSERT_TRUE(wiki_element_is_skipped("table", "", ""));
    ASSERT_TRUE(wiki_element_is_skipped("sup", "reference", ""));
    ASSERT_TRUE(wiki_element_is_skipped("div", "infobox sinottico", ""));
    ASSERT_TRUE(wiki_element_is_skipped("div", "mw-content-ltr navbox", ""));
    ASSERT_TRUE(wiki_element_is_skipped("div", "", "catlinks"));

    ASSERT_FALSE(wiki_element_is_skipped("p", "", ""));
    ASSERT_FALSE(wiki_element_is_skipped("div", "mw-parser-output", ""));
    ASSERT_FALSE(wiki_element_is_skipped("div", "not-infobox", ""))
        << "substring matching would wrongly skip this";
    ASSERT_FALSE(wiki_element_is_skipped("div", "referenced-work", ""));
}
