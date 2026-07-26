#include "reader/word_preview.h"

#include "dict/match_detail.h"
#include "lexicon/english_inflect.h"
#include "lexicon/italian_article.h"
#include "lexicon/lexicon_service.h"

namespace
{
const char *const SEP = " \xC2\xB7 ";  // " · "

// How many senses are worth carrying at all. The renderer takes what fits from these; the
// cap only stops a word like "fare" (64 senses) from building a string nobody could read.
const size_t MAX_GLOSSES = 6;
}

WordPreview summarize_word(const lexicon::LexiconService &lexicon, const std::string &surface)
{
    WordPreview out;

    auto entries = lexicon.lemmatize(surface);
    if (entries.empty())
    {
        // Unknown: show the word and the nearest thing the lexicon does know. A blank line
        // here reads as the app being broken rather than the word being unrecognised, and
        // proper nouns and rare inflections land in this branch constantly.
        out.subject = surface;
        const auto near = lexicon.suggest(surface, 1);
        if (!near.empty())
        {
            out.grammar = "? forse: " + near.front().lemma;
        }
        return out;
    }

    const lexicon::LemmaEntry &entry = entries.front();
    const std::string &lemma = entry.lemma;

    auto senses = lexicon.lookup_it_en(lemma);
    if (senses.empty())
    {
        senses = lexicon.lookup_it_it(lemma);
    }

    // The subject is the Italian exactly as it appears on the page, said the way a learner
    // needs to hear it: a verb with its pronoun, a noun with its article.
    const int person = lexicon::person_index_for_features(entry.features);
    if (person >= 0)
    {
        out.subject = std::string(lexicon::PERSON_LABELS[person]) + " ";
    }
    out.subject += surface;

    // Gender is the one thing a learner has to memorise per noun, and an article states it
    // without an abbreviation to decode. Only on the headword: "il cani" would be wrong.
    if (entry.pos == "NOUN" && surface == lemma)
    {
        out.subject = lexicon::with_article(surface, lexicon.noun_gender(lemma));
    }

    // Inflect each gloss to match, then drop the pronoun from all but the first: "they made,
    // created" rather than "they made, they created", which is what saying it per gloss gave.
    const std::string tense = dict::tense_key_for_features(entry.features);

    for (const auto &sense : senses)
    {
        if (out.glosses.size() >= MAX_GLOSSES)
        {
            break;
        }

        std::string text = sense.gloss;
        if (entry.is_verb() && person >= 0)
        {
            const std::string conjugated = lexicon::conjugate_gloss(text, person, tense);
            if (!conjugated.empty())
            {
                text = conjugated;
                if (!out.glosses.empty())
                {
                    // conjugate_gloss puts the English pronoun first; past the first gloss
                    // it is the same word every time and only costs width.
                    const std::string en_pronoun = text.substr(0, text.find(' ') + 1);
                    text = text.substr(en_pronoun.size());
                }
            }
        }

        if (!text.empty())
        {
            out.glosses.push_back(text);
        }
    }

    // Line two: what is true of the word rather than of this occurrence.
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
