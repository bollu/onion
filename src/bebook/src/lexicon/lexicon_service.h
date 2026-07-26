#ifndef LEXICON_SERVICE_H_
#define LEXICON_SERVICE_H_

#include <array>
#include <memory>
#include <string>
#include <vector>

// Read-only lookups against the prebuilt Italian lexicon (resources/italian.sqlite,
// produced by tools/build_lexicon.py). Answers the three questions the reader asks about
// a selected word: what is it (form -> lemma + morphology), what does it mean (glosses),
// and how does it conjugate (tables per tense).
namespace lexicon
{

// One way a surface form can be analyzed. A form may be ambiguous (e.g. "sono" is both
// essere 1sg-present and 3pl-present), so lemmatize() returns a list.
struct LemmaEntry
{
    std::string lemma;           // e.g. "fare"
    std::string pos;             // Morph-it! coarse tag: "VER","NOUN","ADJ",...
    std::string features;        // raw feature code, e.g. "impf+1+s", "inf", "base"
    std::string morphology_human;// derived, e.g. "verb · imperfect · io"

    bool is_verb() const { return pos == "VER"; }
};

struct Sense
{
    int sense_no;
    std::string gloss;
};

// A conjugation table for one tense: six forms indexed io..loro (0..5).
struct ConjTable
{
    std::string tense;                    // key, e.g. "imperfetto"
    std::string display_name;             // e.g. "Imperfetto"
    std::array<std::string, 6> forms;
};

// Person labels for rendering a ConjTable, indexed 0..5.
extern const std::array<const char *, 6> PERSON_LABELS;

// A fuzzy-match candidate for a word not found exactly: the closest known lemma, the folded
// form that matched, and the edit distance from the (folded) query. Nearest first.
struct Suggestion
{
    std::string lemma;
    std::string matched;
    int distance;
};

// A search result for the dictionary app: the (accented) surface to show and open, and the
// lemma it belongs to. `word` may be a conjugated form (e.g. "farò" for query "faro").
struct SearchHit
{
    std::string word;
    std::string lemma;
};

class LexiconService
{
public:
    // Opens db_path read-only. Never throws; check ok().
    explicit LexiconService(const std::string &db_path);
    ~LexiconService();

    LexiconService(const LexiconService &) = delete;
    LexiconService &operator=(const LexiconService &) = delete;

    // True if the database opened successfully. When false, all queries return empty.
    bool ok() const;

    // Analyze a surface form (case-insensitive). Empty if unknown.
    std::vector<LemmaEntry> lemmatize(const std::string &surface) const;

    std::vector<Sense> lookup_it_en(const std::string &lemma) const;
    std::vector<Sense> lookup_it_it(const std::string &lemma) const;

    // Full conjugation tables for a verb lemma, in display order. Empty for non-verbs.
    std::vector<ConjTable> conjugations(const std::string &lemma) const;

    // Closest known words to a misspelled/unknown surface, by trigram overlap reranked by
    // edit distance (the "did you mean" fallback; also the seed of a dictionary search app).
    // Nearest first, deduped by lemma. Empty for very short queries or when nothing is close.
    std::vector<Suggestion> suggest(const std::string &surface, int max_results = 6) const;

    // Dictionary-app search over the query (typed without accents): words folding exactly to
    // it (incl. conjugated forms) first, then headword lemmas by prefix, then a fuzzy fallback
    // if still thin. Accent-insensitive; deduped and bounded. For search-as-you-type.
    std::vector<SearchHit> search(const std::string &query, int max_results = 40) const;

    // "m" or "f" for a noun whose gender the build could determine, otherwise "". Gender
    // belongs to the lemma rather than to any one inflected form, so it is not in
    // LemmaEntry::features -- the headword row there is "base" precisely because it is
    // uninflected.
    std::string noun_gender(const std::string &lemma) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Turn a (pos, features) pair into a human string like "verb · imperfect · io".
// Exposed for testing.
std::string describe_morphology(const std::string &pos, const std::string &features);

// The same thing in dictionary shorthand: "pres.", "pass. rem.", "sost.". For the one-line
// peek, where describe_morphology's full names ("verbo · congiuntivo · imperfetto ·
// lui/lei") cost more width than the meaning they sit next to. Empty when the features say
// nothing worth abbreviating. Exposed for testing.
std::string abbreviate_morphology(const std::string &pos, const std::string &features);

// Which of the six persons a form is (index into PERSON_LABELS / ConjTable::forms), or -1
// when it names none -- an infinitive, a participle, a noun.
int person_index_for_features(const std::string &features);

} // namespace lexicon

#endif
