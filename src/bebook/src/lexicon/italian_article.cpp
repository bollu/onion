#include "./italian_article.h"

#include <cctype>

namespace lexicon
{

namespace
{

// Lowercase first letter, treating the Latin-1 accented vowels as their base letter. Only
// the first character matters here, and Italian nouns start with a letter.
std::string lead(const std::string &noun)
{
    if (noun.empty())
    {
        return "";
    }
    const unsigned char c = static_cast<unsigned char>(noun[0]);
    if (c == 0xC3 && noun.size() > 1)
    {
        // Accented capital or lowercase: à è é ì ò ù and friends all begin a vowel.
        return "v";
    }
    return std::string(1, static_cast<char>(std::tolower(c)));
}

bool is_vowel_start(const std::string &noun)
{
    const std::string l = lead(noun);
    return l == "a" || l == "e" || l == "i" || l == "o" || l == "u" || l == "v";
}

bool starts(const std::string &noun, const char *p)
{
    for (size_t i = 0; p[i] != '\0'; ++i)
    {
        if (i >= noun.size() ||
            std::tolower(static_cast<unsigned char>(noun[i])) != static_cast<unsigned char>(p[i]))
        {
            return false;
        }
    }
    return true;
}

// Masculine nouns take "lo" before s+consonant, z, gn, pn, ps, x, y, and i+vowel. Every
// other consonant takes "il". This is the rule a learner is taught, and getting it wrong is
// more conspicuous than showing no article at all.
bool takes_lo(const std::string &noun)
{
    const std::string l = lead(noun);
    if (l == "z" || l == "x" || l == "y")
    {
        return true;
    }
    if (starts(noun, "gn") || starts(noun, "pn") || starts(noun, "ps") || starts(noun, "sc"))
    {
        return true;
    }
    if (l == "s" && noun.size() > 1)
    {
        const char second = static_cast<char>(std::tolower(static_cast<unsigned char>(noun[1])));
        const bool vowel = second == 'a' || second == 'e' || second == 'i'
                        || second == 'o' || second == 'u';
        return !vowel;
    }
    if (l == "i" && noun.size() > 1)
    {
        const char second = static_cast<char>(std::tolower(static_cast<unsigned char>(noun[1])));
        return second == 'a' || second == 'e' || second == 'o' || second == 'u';
    }
    return false;
}

}

std::string definite_article(const std::string &noun, const std::string &gender)
{
    if (noun.empty() || (gender != "m" && gender != "f"))
    {
        return "";
    }

    if (gender == "f")
    {
        return is_vowel_start(noun) ? "l'" : "la";
    }

    // Order matters: an i before another vowel is semiconsonantic and takes "lo" (lo iato,
    // lo iodio), so it has to be settled before the elision rule claims it as a vowel.
    if (takes_lo(noun))
    {
        return "lo";
    }
    // "l'" wins over "lo" for a masculine noun starting with a vowel: l'amico, not lo amico.
    if (is_vowel_start(noun))
    {
        return "l'";
    }
    return "il";
}

std::string with_article(const std::string &noun, const std::string &gender)
{
    const std::string art = definite_article(noun, gender);
    if (art.empty())
    {
        return noun;
    }
    // The elided article joins the noun; the others take a space.
    return art.back() == '\'' ? art + noun : art + " " + noun;
}

}
