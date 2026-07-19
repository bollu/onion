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

    TextDocToken(DocAddr address, const std::string &text, std::vector<text::StyleRun> style_runs = {});
    bool operator==(const DocToken &other) const override;
    std::string to_string() const override;
};

struct HeaderDocToken : public DocToken
{
    std::string text;
    std::vector<text::StyleRun> style_runs;

    HeaderDocToken(DocAddr address, const std::string &text, std::vector<text::StyleRun> style_runs = {});
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

    ListItemDocToken(DocAddr address, const std::string &text, int nest_level, std::vector<text::StyleRun> style_runs = {});
    bool operator==(const DocToken &other) const override;
    std::string to_string() const override;
};

std::string to_string(TokenType type);

#endif
