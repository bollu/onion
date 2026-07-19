#include "hyphenate.h"

#include "hyphenation_patterns.h"

namespace text
{

namespace
{

// TeX's en-US defaults (\lefthyphenmin, \righthyphenmin), which are also stated in
// the "hyphenmins" block of the upstream pattern file.
constexpr uint32_t LEFT_HYPHEN_MIN = 2;
constexpr uint32_t RIGHT_HYPHEN_MIN = 3;

// Anything longer is not a word we care to break, and bounding it lets the whole
// algorithm run out of two stack buffers.
constexpr uint32_t MAX_WORD_LEN = 100;

// Lowercased word wrapped in the boundary markers TeX writes as '.'.
constexpr uint32_t MAX_BUF_LEN = MAX_WORD_LEN + 2;

// Maps a byte to the trie alphabet: '.' is 0, 'a'..'z' are 1..26.
inline uint8_t symbol_of(char c)
{
    return c == '.' ? 0 : static_cast<uint8_t>(c - 'a' + 1);
}

// Index of `sym` among the children of `node`, or -1. Children are sorted by
// symbol and there are at most ALPHABET_SIZE of them, so a linear scan beats a
// binary search's branch mispredictions on the short lists that dominate.
inline int find_child(const patterns::Node &node, uint8_t sym)
{
    const uint8_t *symbols = patterns::CHILD_SYMBOL + node.child_first;
    for (uint8_t i = 0; i < node.child_count; ++i)
    {
        if (symbols[i] == sym)
        {
            return static_cast<int>(node.child_first) + i;
        }
        if (symbols[i] > sym)
        {
            break;
        }
    }
    return -1;
}

// Three-way compare of an exception entry (whose '-' separators are skipped)
// against a lowercased, NUL-terminated word.
int compare_exception(const char *entry, const char *word)
{
    while (true)
    {
        while (*entry == '-')
        {
            ++entry;
        }
        if (*entry != *word)
        {
            return static_cast<unsigned char>(*entry) - static_cast<unsigned char>(*word);
        }
        if (*entry == 0)
        {
            return 0;
        }
        ++entry;
        ++word;
    }
}

// The exception list is sorted by de-hyphenated word, so binary search applies.
const char *find_exception(const char *word)
{
    uint32_t lo = 0;
    uint32_t hi = patterns::EXCEPTION_COUNT;
    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2;
        int cmp = compare_exception(patterns::EXCEPTION[mid], word);
        if (cmp == 0)
        {
            return patterns::EXCEPTION[mid];
        }
        if (cmp < 0)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return nullptr;
}

}  // namespace

std::vector<uint16_t> hyphenate_word(const char *word, uint32_t length)
{
    std::vector<uint16_t> breaks;

    // LEFT_HYPHEN_MIN + RIGHT_HYPHEN_MIN is the shortest word that can hold a break.
    if (word == nullptr || length < LEFT_HYPHEN_MIN + RIGHT_HYPHEN_MIN ||
        length > MAX_WORD_LEN)
    {
        return breaks;
    }

    // buf holds ".word."; every offset we may return is therefore an ASCII byte
    // boundary, which is trivially a UTF-8 boundary too.
    char buf[MAX_BUF_LEN];
    buf[0] = '.';
    for (uint32_t i = 0; i < length; ++i)
    {
        char c = word[i];
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
        else if (c < 'a' || c > 'z')
        {
            // Digits, punctuation, or any byte of a multi-byte sequence. TeX would
            // stop hyphenating at such a character; we decline the whole word.
            return breaks;
        }
        buf[i + 1] = c;
    }
    buf[length + 1] = '.';

    const uint32_t first = LEFT_HYPHEN_MIN;
    const uint32_t last = length - RIGHT_HYPHEN_MIN;

    // An explicit \hyphenation{...} entry replaces the pattern result entirely.
    {
        char plain[MAX_WORD_LEN + 1];
        for (uint32_t i = 0; i < length; ++i)
        {
            plain[i] = buf[i + 1];
        }
        plain[length] = 0;

        if (const char *entry = find_exception(plain))
        {
            uint32_t offset = 0;
            for (const char *p = entry; *p != 0; ++p)
            {
                if (*p == '-')
                {
                    if (offset >= first && offset <= last)
                    {
                        breaks.push_back(static_cast<uint16_t>(offset));
                    }
                }
                else
                {
                    ++offset;
                }
            }
            return breaks;
        }
    }

    // value[p] scores the gap immediately before buf[p]; a break after word[c]
    // (i.e. at offset c + 1) is value[c + 2].
    uint8_t value[MAX_BUF_LEN + 1] = {0};
    const uint32_t buf_len = length + 2;

    for (uint32_t start = 0; start < buf_len; ++start)
    {
        uint16_t node_index = 0;
        uint32_t end = start + patterns::MAX_PATTERN_LEN;
        if (end > buf_len)
        {
            end = buf_len;
        }

        for (uint32_t i = start; i < end; ++i)
        {
            const patterns::Node &node = patterns::NODE[node_index];
            int child = find_child(node, symbol_of(buf[i]));
            if (child < 0)
            {
                break;
            }
            node_index = patterns::CHILD_NODE[child];

            const patterns::Node &next = patterns::NODE[node_index];
            if (next.value_len == 0)
            {
                continue;
            }
            // The pattern just matched covers buf[start .. i]; its slot k lands on
            // the gap before buf[start + k].
            const uint8_t *v = patterns::VALUE + next.value_first;
            uint32_t at = start + next.value_shift;
            for (uint8_t k = 0; k < next.value_len; ++k, ++at)
            {
                if (v[k] > value[at])
                {
                    value[at] = v[k];
                }
            }
        }
    }

    for (uint32_t offset = first; offset <= last; ++offset)
    {
        if (value[offset + 1] & 1)
        {
            breaks.push_back(static_cast<uint16_t>(offset));
        }
    }

    return breaks;
}

}  // namespace text
