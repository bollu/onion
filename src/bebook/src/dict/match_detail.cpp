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

std::string tense_key_for_features(const std::string &features)
{
    const std::vector<std::string> t = split_features(features);

    // Only the indicative has tables here, so a subjunctive or imperative form has no
    // tense of its own to show and falls back to the present.
    if (has(t, "sub") || has(t, "impr"))
    {
        return "presente";
    }

    if (has(t, "impf")) { return "imperfetto"; }
    if (has(t, "fut"))  { return "futuro_semplice"; }
    if (has(t, "cond")) { return "condizionale"; }

    // A participle is the half of a compound tense the reader is most likely holding.
    if (has(t, "part")) { return "passato_prossimo"; }

    return "presente";
}

int person_index_for_features(const std::string &features)
{
    const std::vector<std::string> t = split_features(features);

    int person = -1;
    if (has(t, "1")) { person = 0; }
    else if (has(t, "2")) { person = 1; }
    else if (has(t, "3")) { person = 2; }

    if (person < 0)
    {
        return -1;
    }

    // Plural shifts by three: io tu lui | noi voi loro.
    if (has(t, "p")) { return person + 3; }
    if (has(t, "s")) { return person; }
    return person;
}

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
