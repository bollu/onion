#include "reader/word_preview.h"

#include "dict/match_detail.h"
#include "lexicon/english_inflect.h"
#include "lexicon/italian_article.h"
#include "lexicon/lexicon_service.h"

namespace
{
const char *const SEP = " \xC2\xB7 ";       // " · "
const char *const ARROW = " \xE2\x86\x92 ";  // " → "
}

WordPreview summarize_word(const lexicon::LexiconService &lexicon, const std::string &surface)
{
    WordPreview out;

    auto entries = lexicon.lemmatize(surface);
    if (entries.empty())
    {
        return out;
    }

    const lexicon::LemmaEntry &entry = entries.front();
    const std::string &lemma = entry.lemma;

    std::string gloss;
    auto en = lexicon.lookup_it_en(lemma);
    if (!en.empty())
    {
        gloss = en.front().gloss;
    }
    else
    {
        auto it = lexicon.lookup_it_it(lemma);
        if (!it.empty())
        {
            gloss = it.front().gloss;
        }
    }

    // The left zone reads as the pairing being learned: the Italian exactly as it appears on
    // the page, and the English in the same person and tense. "loro fecero → they made"
    // teaches what "fecero ... to make" leaves the reader to work out for themselves.
    const int person = lexicon::person_index_for_features(entry.features);

    std::string italian;
    if (person >= 0)
    {
        italian = std::string(lexicon::PERSON_LABELS[person]) + " ";
    }
    italian += surface;

    // A noun gets its article, which is how a dictionary shows gender without a grammatical
    // abbreviation to decode -- and gender is the one thing a learner has to memorise per
    // noun. Only on the headword itself: "il cani" would be wrong, and an inflected form is
    // not what an article agrees with here.
    if (entry.pos == "NOUN" && surface == lemma)
    {
        italian = lexicon::with_article(surface, lexicon.noun_gender(lemma));
    }

    std::string english = gloss;
    if (entry.is_verb() && person >= 0)
    {
        const std::string conjugated = lexicon::conjugate_gloss(
            gloss, person, dict::tense_key_for_features(entry.features));
        if (!conjugated.empty())
        {
            english = conjugated;
        }
    }

    out.meaning = english.empty() ? italian : italian + ARROW + english;

    // The right zone carries what is true of the word rather than of this occurrence: its
    // dictionary form, and the tense in shorthand.
    if (lemma != surface)
    {
        out.grammar = lemma;
    }
    const std::string abbrev = lexicon::abbreviate_morphology(entry.pos, entry.features);
    if (!abbrev.empty())
    {
        out.grammar += out.grammar.empty() ? abbrev : SEP + abbrev;
    }

    return out;
}
