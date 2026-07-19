#ifndef ZIP_UTILS_H_
#define ZIP_UTILS_H_

#include <string>
#include <vector>

typedef struct zip zip_t;

// Reads an entry as raw bytes. size() is the exact uncompressed size, so the result is
// safe to hand to binary consumers (hashers, image decoders).
std::vector<char> read_zip_file(zip_t *zip, const std::string &filepath);

// Reads an entry as text. data() is NUL-terminated for the strlen-based XML parsers.
std::string read_zip_file_str(zip_t *zip, const std::string &filepath);

#endif
