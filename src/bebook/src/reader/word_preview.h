#ifndef READER_WORD_PREVIEW_H_
#define READER_WORD_PREVIEW_H_

#include <string>
#include <vector>

namespace lexicon { class LexiconService; }

// The two-line peek shown above the text while a word is under the cursor:
//
//   loro fecero → they made, created, brought about      <- subject + glosses, bright
//   fare · pass. rem.                                    <- grammar, dim
//
// Two lines because one could not hold both: the meaning and the grammar competed for a
// single line and one of them was always elided. Given a line each, the meaning can also
// carry more than the first of what are often dozens of senses -- "fare" has 64, and its
// first is "to do" when the sentence in front of you may well want "to make".
//
// `glosses` are already inflected to the person and tense of the Italian, and already have
// the repeated pronoun removed from the second onward, so the renderer can join as many as
// fit without knowing any grammar. How many fit is the renderer's business: it depends on
// the width, which this does not know.
struct WordPreview
{
    std::string subject;               // "loro fecero", "il cane", or a bare unknown word
    std::vector<std::string> glosses;  // "they made", "created", ...
    std::string grammar;               // "fare · pass. rem.", or "? forse: ..." when unknown

    bool empty() const { return subject.empty() && glosses.empty() && grammar.empty(); }
};

// Summarize `surface`. It->En glosses are preferred, then It->It. A word the lexicon does
// not know still comes back with its own text and the nearest suggestion, rather than empty:
// the panel is permanent, and a blank line reads as a broken app rather than an unknown word.
WordPreview summarize_word(const lexicon::LexiconService &lexicon, const std::string &surface);

#endif
