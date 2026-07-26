#ifndef READER_WORD_PREVIEW_H_
#define READER_WORD_PREVIEW_H_

#include <string>

namespace lexicon { class LexiconService; }

// The one-line peek shown above the text while a word is under the cursor, in two zones so
// the renderer can style them apart rather than run them together behind a dash:
//
//   io faccio · fare · pres.                              to do
//   <----------- grammar, dim ----------->    <-- meaning, bright -->
//
// The grammar zone names the form: the person for a conjugated verb, the dictionary form
// when it differs from what is on the page, and the tense in dictionary shorthand. That
// last part is what lets a reader tell "feci" from "faccio" without opening the popup.
struct WordPreview
{
    std::string grammar;
    std::string meaning;

    bool empty() const { return grammar.empty() && meaning.empty(); }
};

// Summarize `surface`. It->En glosses are preferred, then It->It. Both zones come back
// empty when the word resolves to nothing, so the caller can leave the line blank rather
// than hide it -- the panel is permanent, and a disappearing line would shift the text
// under it. Eliding to the available width is the renderer's job.
WordPreview summarize_word(const lexicon::LexiconService &lexicon, const std::string &surface);

#endif
