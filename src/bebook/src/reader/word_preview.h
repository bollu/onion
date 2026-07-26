#ifndef READER_WORD_PREVIEW_H_
#define READER_WORD_PREVIEW_H_

#include <string>

namespace lexicon { class LexiconService; }

// A one-line dictionary summary of `surface`, for the in-book word-select preview HUD.
// Format: "lemma — first gloss" (It->En preferred, then It->It, then the word's human
// morphology, then bare lemma). Returns "" when the word resolves to nothing, so the caller
// can hide the preview line. Truncation/eliding to the screen width is the renderer's job.
std::string summarize_word(const lexicon::LexiconService &lexicon, const std::string &surface);

#endif
