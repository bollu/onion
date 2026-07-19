#ifndef HYPHENATE_H_
#define HYPHENATE_H_

#include <cstdint>
#include <vector>

namespace text
{

// Liang's hyphenation algorithm with the TeX en-US patterns.
//
// Byte offsets within `word` after which a hyphen may be inserted.
// Offsets are strictly increasing, > 0 and < length, and always on a UTF-8 boundary.
//
// The en-US \lefthyphenmin=2 / \righthyphenmin=3 defaults are honoured, so no
// offset is smaller than 2 or larger than length - 3.
//
// Words containing anything outside [A-Za-z], words shorter than 5 bytes and
// absurdly long words yield no break points at all. Pure function over static
// const data: no allocation beyond the returned vector, and safe to call from
// any thread.
std::vector<uint16_t> hyphenate_word(const char *word, uint32_t length);

}  // namespace text

#endif
