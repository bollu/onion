#include "./match_detail.h"

#include <vector>

namespace
{

// Split the '+'-joined feature string, e.g. "sub+impf+1+s" -> {sub, impf, 1, s}.
std::vector<std::string> split_features(const std::string &features)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : features)
    {
        if (c == '+')
        {
            if (!cur.empty()) { out.push_back(cur); }
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    if (!cur.empty()) { out.push_back(cur); }
    return out;
}

bool has(const std::vector<std::string> &tokens, const char *want)
{
    for (const auto &t : tokens)
    {
        if (t == want) { return true; }
    }
    return false;
}

}

namespace dict
{



const lexicon::ConjTable *pick_table(
    const std::vector<lexicon::ConjTable> &tables, const std::string &key)
{
    if (tables.empty())
    {
        return nullptr;
    }
    for (const auto &t : tables)
    {
        if (t.tense == key)
        {
            return &t;
        }
    }
    // The implied tense is missing for this verb (defective, or a build with fewer
    // tenses). Showing the first is better than showing none.
    return &tables.front();
}

bool has_more_to_show(
    size_t analyses, size_t conj_tables, size_t senses, size_t senses_shown)
{
    // More than one reading of the surface form, more tenses than the one on screen, or
    // senses that did not fit.
    return analyses > 1 || conj_tables > 1 || senses > senses_shown;
}

}
