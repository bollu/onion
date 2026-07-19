#ifndef STR_UTILS_H_
#define STR_UTILS_H_

#include <string>
#include <vector>

std::string to_lower(const std::string &str);

std::string remove_carriage_returns(const std::string &str);

std::string strip_whitespace(const char *str);
std::string strip_whitespace(const std::string &str);

std::string strip_whitespace_left(const std::string &str);
std::string strip_whitespace_right(const std::string &str);

inline bool is_whitespace(char c)
{
    // Note: Changing this will affect addressing behavior
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string convert_tabs_to_space(const std::string &str, int tab_width);

std::string join_strings(const std::vector<const char *> &strings);

// True if `token` appears as a whole entry in a whitespace-separated token list, the
// shape of the epub OPF `properties` attribute. Substring matching is wrong here:
// "cover-image" must not match "not-cover-image", and "nav" must match "nav scripted".
bool has_token(const std::string &token_list, const std::string &token);

#endif
