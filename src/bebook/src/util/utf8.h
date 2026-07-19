#ifndef UTF_H_
#define UTF_H_

#include <cstdint>

// Step to next character in utf-8 encoded string
inline const char *utf8_step(const char *s)
{
    while ((*++s & 0xC0) == 0x80);
    return s;
}

// Decode one code point, advancing *s past it. Malformed bytes decode to U+FFFD and
// advance by one, so a bad byte can never cause the caller to loop forever.
inline uint32_t utf8_decode(const char **s, const char *end)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(*s);
    const unsigned char *e = reinterpret_cast<const unsigned char *>(end);
    if (p >= e)
    {
        return 0;
    }

    uint32_t c = *p;
    int extra;
    if (c < 0x80)      { *s = reinterpret_cast<const char *>(p + 1); return c; }
    else if ((c & 0xE0) == 0xC0) { c &= 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { c &= 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { c &= 0x07; extra = 3; }
    else { *s = reinterpret_cast<const char *>(p + 1); return 0xFFFD; }

    if (p + extra >= e)
    {
        *s = reinterpret_cast<const char *>(p + 1);
        return 0xFFFD;
    }

    for (int i = 1; i <= extra; ++i)
    {
        if ((p[i] & 0xC0) != 0x80)
        {
            *s = reinterpret_cast<const char *>(p + 1);
            return 0xFFFD;
        }
        c = (c << 6) | (p[i] & 0x3F);
    }

    *s = reinterpret_cast<const char *>(p + extra + 1);
    return c;
}

#endif
