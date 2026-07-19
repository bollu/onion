#include "../xhtml_parser.h"

#include <gtest/gtest.h>

static void ASSERT_TOKENS_EQ(const std::vector<std::unique_ptr<DocToken>> &actual_tokens, const std::vector<std::unique_ptr<DocToken>> &expected_tokens)
{
    auto actual_it = actual_tokens.begin();
    auto expected_it = expected_tokens.begin();
    int i = 0;
    while (actual_it != actual_tokens.end() && expected_it != expected_tokens.end())
    {
        const auto &actual = *actual_it;
        const auto &expected = *expected_it;
    
        EXPECT_EQ(actual->type, expected->type) << i << ": Type didn't match";
        EXPECT_EQ(actual->address, expected->address) << i << ": Address didn't match";
        ASSERT_EQ(*actual.get(), *expected.get()) << i << ": Token didn't match";
    
        ++actual_it;
        ++expected_it;
        ++i;
    }

    ASSERT_EQ(actual_tokens.size(), expected_tokens.size());
}

static std::vector<std::unique_ptr<DocToken>> _parse_xhtml_tokens(const char *xml)
{
    std::vector<std::unique_ptr<DocToken>> tokens;
    std::unordered_map<std::string, DocAddr> ids;
    parse_xhtml_tokens(xml, "/base/file.xhtml", 0, tokens, ids);
    return tokens;
}

TEST(XHTML_PARSER, basic_text_valid_xhtml)
{
    const char *xml = (
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>"
        "<!DOCTYPE html>"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\" xml:lang=\"en\" lang=\"en\">"
        "    <head>\n"
        "        <title>Title</title>\n"
        "    </head>\n"
        "    <body>\n"
        "        Text\n"
        "    </body>\n"
        "</html>"
    );
  
    std::vector<std::unique_ptr<DocToken>> expected_tokens;
    expected_tokens.push_back(std::make_unique<TextDocToken>(0, "Text"));
  
    ASSERT_TOKENS_EQ(
        _parse_xhtml_tokens(xml),
        expected_tokens
    );
}

TEST(XHTML_PARSER, whitespace_compaction)
{
    const char *xml = (
        "<html><body>"
        "  This  <i>  has  </i>  some  <i>extra</i>  white<i> <i/>space  "
        "</body></html>"
    );
  
    std::vector<std::unique_ptr<DocToken>> expected_tokens;
    expected_tokens.push_back(std::make_unique<TextDocToken>(0, "This has some extra white space"));
  
    ASSERT_TOKENS_EQ(
        _parse_xhtml_tokens(xml),
        expected_tokens
    );
}

TEST(XHTML_PARSER, line_break)
{
    const char *xml = (
        "<html><body>"
        "<div>Line 1</div>"
        "<div>Line 2</div>"
        "</body></html>"
    );
  
    std::vector<std::unique_ptr<DocToken>> expected_tokens;
    expected_tokens.push_back(std::make_unique<TextDocToken>(0, "Line 1"));
    expected_tokens.push_back(std::make_unique<TextDocToken>(5, "Line 2"));
  
    ASSERT_TOKENS_EQ(
        _parse_xhtml_tokens(xml),
        expected_tokens
    );
}

TEST(XHTML_PARSER, section_compaction)
{
    const char *xml = (
        "<html><body>"
        "  <p>"
        "    <p>"
        "      <div>"
        "         Some text."
        "      </div>"
        "    </p>"
        "    <p>"
        "       Some more."
        "    </p>"
        "  </p>"
        "</body></html>"
    );
  
    std::vector<std::unique_ptr<DocToken>> expected_tokens;
    expected_tokens.push_back(std::make_unique<TextDocToken>(0,  ""          ));
    expected_tokens.push_back(std::make_unique<TextDocToken>(0,  "Some text."));
    expected_tokens.push_back(std::make_unique<TextDocToken>(9,  ""          ));
    expected_tokens.push_back(std::make_unique<TextDocToken>(9,  "Some more."));
    expected_tokens.push_back(std::make_unique<TextDocToken>(18, ""          ));
  
    ASSERT_TOKENS_EQ(
        _parse_xhtml_tokens(xml),
        expected_tokens
    );
}

TEST(XHTML_PARSER, header_elems)
{
    const char *xml = (
        "<html><body>"
        "  <h1>heading <span>1</span></h1>"
        "  <h1>heading 2</h1>"
        "  <h6>heading 3</h6>"
        "  <p>"
        "    Some text"
        "  </p>"
        "</body></html>"
    );

    std::vector<std::unique_ptr<DocToken>> expected_tokens;
    expected_tokens.push_back(std::make_unique<TextDocToken>(0, ""));
    expected_tokens.push_back(std::make_unique<HeaderDocToken>(0, "heading 1"));
    expected_tokens.push_back(std::make_unique<TextDocToken>(8, ""));
    expected_tokens.push_back(std::make_unique<HeaderDocToken>(8, "heading 2"));
    expected_tokens.push_back(std::make_unique<TextDocToken>(16, ""));
    expected_tokens.push_back(std::make_unique<HeaderDocToken>(16, "heading 3"));
    expected_tokens.push_back(std::make_unique<TextDocToken>(24, ""));
    expected_tokens.push_back(std::make_unique<TextDocToken>(24, "Some text"));
    expected_tokens.push_back(std::make_unique<TextDocToken>(32, ""));

    ASSERT_TOKENS_EQ(
        _parse_xhtml_tokens(xml),
        expected_tokens
    );
}

TEST(XHTML_PARSER, pre_elems)
{
    const char *xml = (
        "<html><body>"
            "<span>start</span>"

            "<pre>line1\r\n"
            "<span>line2</span>\r\n"
            "line3</pre>"

            "<pre>line4</pre>"

            "<span>end</span>"
        "</body></html>"
    );

    std::vector<std::unique_ptr<DocToken>> expected_tokens;
    expected_tokens.push_back(std::make_unique<TextDocToken>(0, "start"              ));
    expected_tokens.push_back(std::make_unique<TextDocToken>(5, ""                   ));
    expected_tokens.push_back(std::make_unique<TextDocToken>(5, "line1\nline2\nline3"));
    expected_tokens.push_back(std::make_unique<TextDocToken>(20, ""                  ));
    expected_tokens.push_back(std::make_unique<TextDocToken>(20, "line4"             ));
    expected_tokens.push_back(std::make_unique<TextDocToken>(25, ""                  ));
    expected_tokens.push_back(std::make_unique<TextDocToken>(25, "end"               ));

    ASSERT_TOKENS_EQ(
        _parse_xhtml_tokens(xml),
        expected_tokens
    );
}

TEST(XHTML_PARSER, image_elems)
{
    const char *xml = (
        "<html><body>"
        "<img src=\"foo.png\"></img>"
        "<img src=\"../bar.png\"></img>"
        "<div>Line 2</div>"
        "</body></html>"
    );

    std::vector<std::unique_ptr<DocToken>> expected_tokens;
    expected_tokens.push_back(std::make_unique<ImageDocToken>(0, "/base/foo.png"));
    expected_tokens.push_back(std::make_unique<ImageDocToken>(1, "/bar.png"));
    expected_tokens.push_back(std::make_unique<TextDocToken>(2, "Line 2"));

    ASSERT_TOKENS_EQ(
        _parse_xhtml_tokens(xml),
        expected_tokens
    );
}

TEST(XHTML_PARSER, capture_ids)
{
    const char *xml = (
        "<html><body>"
        "<p id=\"id1\">text1</p>"
        "<p id=\"id2\">text2</p>"
        "</body></html>"
    );

    std::unordered_map<std::string, DocAddr> expected_ids {
        {"id1", 0},
        {"id2", 5},
    };
  
    std::vector<std::unique_ptr<DocToken>> tokens;
    std::unordered_map<std::string, DocAddr> ids;
    ASSERT_TRUE(parse_xhtml_tokens(xml, "", 0, tokens, ids));

    ASSERT_EQ(expected_ids, ids);
}

// Inline styling ------------------------------------------------------------------

namespace
{

// Renders a token's styling as a readable mask, one character per byte of text:
// '.' regular, 'i' italic, 'b' bold, 'B' bold italic. Comparing masks makes an
// off-by-one in the offset mapping obvious, which comparing run vectors does not.
std::string style_mask(const std::string &text, const std::vector<text::StyleRun> &runs)
{
    std::string mask(text.size(), '.');
    for (const auto &run : runs)
    {
        for (uint32_t i = run.offset; i < run.offset + run.length && i < mask.size(); ++i)
        {
            switch (run.style)
            {
                case text::Style::Regular:    mask[i] = '.'; break;
                case text::Style::Italic:     mask[i] = 'i'; break;
                case text::Style::Bold:       mask[i] = 'b'; break;
                case text::Style::BoldItalic: mask[i] = 'B'; break;
            }
        }
    }
    return mask;
}

// Skips the empty text tokens the parser emits as paragraph separators.
const TextDocToken &first_text_token(const std::vector<std::unique_ptr<DocToken>> &tokens)
{
    for (const auto &t : tokens)
    {
        if (t->type == TokenType::Text)
        {
            const auto &text_token = static_cast<const TextDocToken &>(*t);
            if (!text_token.text.empty())
            {
                return text_token;
            }
        }
    }
    throw std::runtime_error("no non-empty text token");
}

} // namespace

TEST(XHTML_PARSER, plain_paragraph_has_no_style_runs)
{
    auto tokens = _parse_xhtml_tokens("<html><body><p>hello world</p></body></html>");
    const auto &token = first_text_token(tokens);

    EXPECT_EQ(token.text, "hello world");
    // Nothing stored when everything is regular, so rendering can skip the styled path.
    EXPECT_TRUE(token.style_runs.empty());
}

TEST(XHTML_PARSER, emphasis_maps_onto_the_compacted_text)
{
    auto tokens = _parse_xhtml_tokens("<html><body><p>say <em>yes</em> now</p></body></html>");
    const auto &token = first_text_token(tokens);

    EXPECT_EQ(token.text, "say yes now");
    EXPECT_EQ(style_mask(token.text, token.style_runs),
              "....iii....");
}

TEST(XHTML_PARSER, strong_and_alternate_spellings)
{
    auto tokens = _parse_xhtml_tokens(
        "<html><body><p>a <strong>b</strong> c <b>d</b> e <i>f</i></p></body></html>"
    );
    const auto &token = first_text_token(tokens);

    EXPECT_EQ(token.text, "a b c d e f");
    EXPECT_EQ(style_mask(token.text, token.style_runs),
              "..b...b...i");
}

TEST(XHTML_PARSER, nested_emphasis_and_strong_combine)
{
    auto tokens = _parse_xhtml_tokens(
        "<html><body><p>x<em>y<strong>z</strong></em></p></body></html>"
    );
    const auto &token = first_text_token(tokens);

    EXPECT_EQ(token.text, "xyz");
    EXPECT_EQ(style_mask(token.text, token.style_runs), ".iB");
}

// Whitespace collapsing shifts every offset after it, which is exactly the case a
// naive length-accumulating implementation gets wrong.
TEST(XHTML_PARSER, style_offsets_survive_whitespace_compaction)
{
    auto tokens = _parse_xhtml_tokens(
        "<html><body><p>   one    <em>two</em>     three   </p></body></html>"
    );
    const auto &token = first_text_token(tokens);

    EXPECT_EQ(token.text, "one two three");
    EXPECT_EQ(style_mask(token.text, token.style_runs),
              "....iii......");
}

TEST(XHTML_PARSER, emphasis_inside_a_heading_is_kept)
{
    auto tokens = _parse_xhtml_tokens("<html><body><h1>a <em>b</em></h1></body></html>");

    const HeaderDocToken *header = nullptr;
    for (const auto &t : tokens)
    {
        if (t->type == TokenType::Header)
        {
            header = static_cast<const HeaderDocToken *>(t.get());
        }
    }
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->text, "a b");
    EXPECT_EQ(style_mask(header->text, header->style_runs), "..i");
}
