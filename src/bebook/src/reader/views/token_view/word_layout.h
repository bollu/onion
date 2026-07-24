#ifndef WORD_LAYOUT_H_
#define WORD_LAYOUT_H_

#include <cstdint>
#include <string>
#include <vector>

// A word inside a laid-out line, as a byte range [start, end) into the line's text.
struct WordSpan
{
    uint32_t start;
    uint32_t end;

    bool operator==(const WordSpan &o) const { return start == o.start && end == o.end; }
};

// Split a line of text into selectable words for the dictionary picker.
//
// A word is a maximal run of letters, keeping word-internal and trailing apostrophes so
// Italian elisions stay whole ("l'acqua", "dell'", "un'ora"). Leading apostrophes /
// quote marks are dropped. Letters are ASCII a-z/A-Z and Latin-1 accented letters
// (à è é ì ò ù and friends); typographic punctuation (curly quotes, dashes, ellipsis)
// counts as a separator. Both straight (U+0027) and typographic (U+2019) apostrophes
// are recognised.
std::vector<WordSpan> tokenize_words(const std::string &text);

#endif
