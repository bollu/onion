#ifndef PERCENT_DECODE_H_
#define PERCENT_DECODE_H_

#include <string>

namespace zim
{

// Percent-decodes in place of a full URL parse: ZIM hrefs are bare relative paths.
// Malformed escapes are passed through verbatim rather than dropped.
std::string percent_decode(const std::string &s);

// Turns an href found in article HTML into a ZIM path, or empty if it does not name one.
// Rejects external, fragment-only and resource links; strips a leading "./" and any
// "#fragment"; percent-decodes what remains.
std::string href_to_zim_path(const std::string &href);

}

#endif
