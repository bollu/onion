#ifndef XHTML_STRING_UTIL_H_
#define XHTML_STRING_UTIL_H_

#include <string>
#include <vector>

// Convert all whitespace chars to space and limit consecutive whitespace to 1 char length
std::string compact_whitespace(const char *str);

// Join multiple strings, applying html whitespace rules
std::string compact_strings(const std::vector<const char*> &strings);

// As above, but also reports where each input string's surviving content begins in the
// result. Inline markup arrives as one text node per styled span, and whitespace
// compaction shifts everything, so this is what lets a style be mapped back onto a byte
// range of the joined paragraph.
//
// out_offsets is resized to strings.size(). An input whose content was entirely absorbed
// by compaction shares the offset of whatever follows it, giving it an empty range.
std::string compact_strings(
    const std::vector<const char*> &strings,
    std::vector<uint32_t> &out_offsets
);

#endif
