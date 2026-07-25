#ifndef DOC_TOKEN_H_
#define DOC_TOKEN_H_

#include "./doc_addr.h"
#include "text/text_types.h"

#include <filesystem>
#include <string>
#include <vector>

enum class TokenType
{
    Text,
    Header,
    Image,
    ListItem,
};

// A byte span of a token's text that links elsewhere. `target` is opaque here; the wiki
// reader treats it as a percent-decoded, namespace-less ZIM path.
struct LinkRun
{
    uint32_t offset;
    uint32_t length;
    std::string target;

    bool operator==(const LinkRun &other) const
    {
        return offset == other.offset && length == other.length && target == other.target;
    }
};

struct DocToken
{
    TokenType type;
    DocAddr address;

    DocToken(TokenType type, DocAddr address);
    virtual bool operator==(const DocToken &other) const;
    virtual std::string to_string() const = 0;

protected:
    std::string common_to_string(std::string data) const;
};

struct TextDocToken : public DocToken
{
    std::string text;
    // Empty means the whole token is Regular, which is the overwhelmingly common case.
    std::vector<text::StyleRun> style_runs;
    // Empty for every format but the wiki reader.
    std::vector<LinkRun> link_runs;

    TextDocToken(DocAddr address, const std::string &text, std::vector<text::StyleRun> style_runs = {}, std::vector<LinkRun> link_runs = {});
    bool operator==(const DocToken &other) const override;
    std::string to_string() const override;
};

struct HeaderDocToken : public DocToken
{
    std::string text;
    std::vector<text::StyleRun> style_runs;
    std::vector<LinkRun> link_runs;

    HeaderDocToken(DocAddr address, const std::string &text, std::vector<text::StyleRun> style_runs = {}, std::vector<LinkRun> link_runs = {});
    bool operator==(const DocToken &other) const override;
    std::string to_string() const override;
};

struct ImageDocToken : public DocToken
{
    std::filesystem::path path;

    ImageDocToken(DocAddr address, const std::filesystem::path &path);
    bool operator==(const DocToken &other) const override;
    std::string to_string() const override;
};

struct ListItemDocToken : public DocToken
{
    std::string text;
    int nest_level;
    std::vector<text::StyleRun> style_runs;
    std::vector<LinkRun> link_runs;

    ListItemDocToken(DocAddr address, const std::string &text, int nest_level, std::vector<text::StyleRun> style_runs = {}, std::vector<LinkRun> link_runs = {});
    bool operator==(const DocToken &other) const override;
    std::string to_string() const override;
};

std::string to_string(TokenType type);

#endif
