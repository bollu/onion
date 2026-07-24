#include "./word_layout.h"

#include "util/utf8.h"

namespace
{

bool is_apostrophe(uint32_t cp)
{
    return cp == 0x27      // '  APOSTROPHE
        || cp == 0x2019;   // ’  RIGHT SINGLE QUOTATION MARK
}

// Letters we treat as word characters: ASCII plus the Latin-1 accented range, which
// covers every accented letter Italian uses (à á â è é ê ì í î ò ó ô ù ú û and caps).
// Multiplication/division signs at 0xD7/0xF7 sit in that range but are not letters.
// Anything above Latin-1 (curly quotes, dashes, ellipsis) is deliberately excluded and
// so acts as a word separator.
bool is_letter(uint32_t cp)
{
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'))
    {
        return true;
    }
    if (cp >= 0xC0 && cp <= 0xFF && cp != 0xD7 && cp != 0xF7)
    {
        return true;
    }
    return false;
}

} // namespace

std::vector<WordSpan> tokenize_words(const std::string &text)
{
    std::vector<WordSpan> words;

    const char *s = text.c_str();
    const char *end = s + text.size();
    const char *p = s;

    // -1 == not currently inside a word.
    int64_t word_start = -1;
    uint32_t word_end = 0;  // one past the last kept byte (letter or trailing apostrophe)

    while (p < end)
    {
        const char *cp_start = p;
        uint32_t cp = utf8_decode(&p, end);
        uint32_t byte_off = static_cast<uint32_t>(cp_start - s);
        uint32_t next_off = static_cast<uint32_t>(p - s);

        if (is_letter(cp))
        {
            if (word_start < 0)
            {
                word_start = byte_off;
            }
            word_end = next_off;
        }
        else if (is_apostrophe(cp))
        {
            // Only meaningful once a word has started; extend to keep the apostrophe,
            // but a run of apostrophes with no following letter is trimmed off below by
            // word_end pointing at the last letter+apostrophes we actually kept.
            if (word_start >= 0)
            {
                word_end = next_off;
            }
            // leading apostrophe with no word yet: ignore.
        }
        else
        {
            if (word_start >= 0)
            {
                words.push_back({static_cast<uint32_t>(word_start), word_end});
                word_start = -1;
            }
        }
    }

    if (word_start >= 0)
    {
        words.push_back({static_cast<uint32_t>(word_start), word_end});
    }

    return words;
}
