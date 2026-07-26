#include "./english_inflect.h"

#include <unordered_map>
#include <vector>

namespace lexicon
{

namespace
{

const char *const PRONOUNS[6] = { "I", "you", "he/she", "we", "you", "they" };

bool is_vowel(char c)
{
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

bool ends_with(const std::string &s, const std::string &suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The verbs a beginner meets first are the ones English inflects irregularly, so the table
// earns its place immediately: without it the commonest glosses come out as "goed", "haved".
// Anything absent falls through to the regular rules, which are right for the long tail.
struct Irregular { const char *base; const char *past; const char *participle; };
const Irregular IRREGULARS[] = {
    {"be", "was/were", "been"},   {"have", "had", "had"},
    {"do", "did", "done"},        {"make", "made", "made"},
    {"go", "went", "gone"},       {"say", "said", "said"},
    {"tell", "told", "told"},     {"come", "came", "come"},
    {"know", "knew", "known"},    {"give", "gave", "given"},
    {"take", "took", "taken"},    {"see", "saw", "seen"},
    {"find", "found", "found"},   {"think", "thought", "thought"},
    {"get", "got", "got"},        {"put", "put", "put"},
    {"keep", "kept", "kept"},     {"leave", "left", "left"},
    {"feel", "felt", "felt"},     {"hold", "held", "held"},
    {"bring", "brought", "brought"}, {"write", "wrote", "written"},
    {"read", "read", "read"},     {"speak", "spoke", "spoken"},
    {"eat", "ate", "eaten"},      {"drink", "drank", "drunk"},
    {"run", "ran", "run"},        {"stand", "stood", "stood"},
    {"understand", "understood", "understood"},
    {"lose", "lost", "lost"},     {"win", "won", "won"},
    {"buy", "bought", "bought"},  {"sell", "sold", "sold"},
    {"send", "sent", "sent"},     {"build", "built", "built"},
    {"begin", "began", "begun"},  {"become", "became", "become"},
    {"sleep", "slept", "slept"},  {"meet", "met", "met"},
    {"pay", "paid", "paid"},      {"sit", "sat", "sat"},
};

const Irregular *find_irregular(const std::string &verb)
{
    for (const auto &e : IRREGULARS)
    {
        if (verb == e.base) { return &e; }
    }
    return nullptr;
}

// Split "to bring about" into ("bring", " about"). Only the head inflects.
void split_head(const std::string &gloss, std::string &head, std::string &tail)
{
    std::string g = gloss;
    if (g.rfind("to ", 0) == 0)
    {
        g = g.substr(3);
    }
    const size_t sp = g.find(' ');
    if (sp == std::string::npos)
    {
        head = g;
        tail.clear();
    }
    else
    {
        head = g.substr(0, sp);
        tail = g.substr(sp);
    }
}

// A final consonant is doubled before -ed/-ing after a single stressed vowel ("stop" ->
// "stopped"). Stress is not knowable here, so this applies the consonant-vowel-consonant
// shape, which is right far more often than leaving it alone.
bool doubles_final(const std::string &v)
{
    if (v.size() < 3) { return false; }
    const char a = v[v.size() - 3], b = v[v.size() - 2], c = v[v.size() - 1];
    if (is_vowel(c) || c == 'w' || c == 'x' || c == 'y') { return false; }
    return !is_vowel(a) && is_vowel(b);
}

}

std::string english_past(const std::string &verb)
{
    if (verb.empty()) { return ""; }
    if (const Irregular *i = find_irregular(verb)) { return i->past; }
    if (ends_with(verb, "e")) { return verb + "d"; }
    if (ends_with(verb, "y") && verb.size() >= 2 && !is_vowel(verb[verb.size() - 2]))
    {
        return verb.substr(0, verb.size() - 1) + "ied";
    }
    if (doubles_final(verb)) { return verb + verb.back() + "ed"; }
    return verb + "ed";
}

std::string english_participle(const std::string &verb)
{
    if (verb.empty()) { return ""; }
    if (const Irregular *i = find_irregular(verb)) { return i->participle; }
    return english_past(verb);
}

std::string english_third_person(const std::string &verb)
{
    if (verb.empty()) { return ""; }
    if (verb == "be")   { return "is"; }
    if (verb == "have") { return "has"; }
    if (ends_with(verb, "s") || ends_with(verb, "x") || ends_with(verb, "z")
        || ends_with(verb, "ch") || ends_with(verb, "sh"))
    {
        return verb + "es";
    }
    if (ends_with(verb, "y") && verb.size() >= 2 && !is_vowel(verb[verb.size() - 2]))
    {
        return verb.substr(0, verb.size() - 1) + "ies";
    }
    return verb + "s";
}

std::string english_gerund(const std::string &verb)
{
    if (verb.empty()) { return ""; }
    if (ends_with(verb, "ie"))
    {
        return verb.substr(0, verb.size() - 2) + "ying";
    }
    if (ends_with(verb, "e") && verb != "be")
    {
        return verb.substr(0, verb.size() - 1) + "ing";
    }
    if (doubles_final(verb)) { return verb + verb.back() + "ing"; }
    return verb + "ing";
}

std::string conjugate_gloss(const std::string &gloss, int person, const std::string &tense_key)
{
    if (gloss.empty() || person < 0 || person > 5)
    {
        return "";
    }

    // Only a gloss written as an infinitive can be conjugated. Plenty are prose instead --
    // "Used as a copula. to be" -- and inflecting the first word of one of those produced
    // "he/she Useds". A gloss that does not start with "to " is a description of the word,
    // not a verb to put in a person, so it is left exactly as written.
    if (gloss.rfind("to ", 0) != 0)
    {
        return "";
    }

    std::string head, tail;
    split_head(gloss, head, tail);
    if (head.empty())
    {
        return "";
    }

    const std::string pronoun = PRONOUNS[person];
    const bool third_singular = (person == 2);
    // "I was" / "he was", but "you/we/they were".
    const bool was = (person == 0 || person == 2);

    std::string verb;
    if (tense_key == "presente")
    {
        verb = third_singular ? english_third_person(head) : head;
    }
    else if (tense_key == "imperfetto")
    {
        // The Italian imperfect is an ongoing or habitual past; the English progressive
        // carries that better than a bare past, which would collide with passato remoto.
        verb = std::string(was ? "was " : "were ") + english_gerund(head);
    }
    else if (tense_key == "passato_remoto")
    {
        verb = english_past(head);
    }
    else if (tense_key == "passato_prossimo")
    {
        verb = std::string(third_singular ? "has " : "have ") + english_participle(head);
    }
    else if (tense_key == "futuro_semplice")
    {
        verb = "will " + head;
    }
    else if (tense_key == "condizionale")
    {
        verb = "would " + head;
    }
    else
    {
        return "";
    }

    // "was/were" is the irregular past of "be" as a table entry; pick the arm the person
    // actually needs rather than showing both.
    const size_t slash = verb.find("/");
    if (slash != std::string::npos && tense_key == "passato_remoto" && head == "be")
    {
        verb = was ? verb.substr(0, slash) : verb.substr(slash + 1);
    }

    return pronoun + " " + verb + tail;
}

}
