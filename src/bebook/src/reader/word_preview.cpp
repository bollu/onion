#include "reader/word_preview.h"

#include "lexicon/lexicon_service.h"

namespace
{
const char *const SEP = " \xC2\xB7 ";  // " · " (U+00B7)
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

    auto en = lexicon.lookup_it_en(lemma);
    if (!en.empty())
    {
        out.meaning = en.front().gloss;
    }
    else
    {
        auto it = lexicon.lookup_it_it(lemma);
        if (!it.empty())
        {
            out.meaning = it.front().gloss;
        }
    }

    // "io faccio": the pronoun makes a conjugated form readable as the person it is, which
    // is usually the thing a learner is trying to work out.
    const int person = lexicon::person_index_for_features(entry.features);
    if (person >= 0)
    {
        out.grammar = std::string(lexicon::PERSON_LABELS[person]) + " ";
    }
    out.grammar += surface;

    // The dictionary form, but only when it is not already what is on the page -- for an
    // uninflected word "cane · cane" would be noise.
    if (lemma != surface)
    {
        out.grammar += SEP + lemma;
    }

    const std::string abbrev = lexicon::abbreviate_morphology(entry.pos, entry.features);
    if (!abbrev.empty())
    {
        out.grammar += SEP + abbrev;
    }

    return out;
}
