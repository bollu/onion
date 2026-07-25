#include "./percent_decode.h"

namespace
{

int hex_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

bool starts_with(const std::string &s, const char *prefix)
{
    return s.compare(0, std::string(prefix).size(), prefix) == 0;
}

}

namespace zim
{

std::string percent_decode(const std::string &s)
{
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '%' || i + 2 >= s.size())
        {
            out.push_back(s[i]);
            continue;
        }

        const int hi = hex_value(s[i + 1]);
        const int lo = hex_value(s[i + 2]);
        if (hi < 0 || lo < 0)
        {
            out.push_back(s[i]);
            continue;
        }

        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
    }

    return out;
}

std::string href_to_zim_path(const std::string &href)
{
    if (href.empty() || href[0] == '#' || href.find("://") != std::string::npos)
    {
        return {};
    }

    // mwoffliner's own assets, never article content.
    if (starts_with(href, "./_") || starts_with(href, "_mw_/") || starts_with(href, "_res_/"))
    {
        return {};
    }

    std::string path = href;
    if (starts_with(path, "./"))
    {
        path = path.substr(2);
    }

    const auto hash = path.find('#');
    if (hash != std::string::npos)
    {
        path = path.substr(0, hash);
    }

    if (path.empty() || starts_with(path, "/") || starts_with(path, "../"))
    {
        return {};
    }

    return percent_decode(path);
}

}
