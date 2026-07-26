#include "reader/word_preview.h"

#include "lexicon/lexicon_service.h"

std::string summarize_word(const lexicon::LexiconService &lexicon, const std::string &surface)
{
    auto entries = lexicon.lemmatize(surface);
    if (entries.empty())
    {
        return "";
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

    const std::string sep = "  \xE2\x80\x94  ";  // spaced em dash

    if (!gloss.empty())
    {
        return lemma + sep + gloss;
    }
    if (!entry.morphology_human.empty())
    {
        return lemma + sep + entry.morphology_human;
    }
    return lemma;
}
