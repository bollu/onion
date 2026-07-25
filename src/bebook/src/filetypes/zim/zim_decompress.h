#ifndef ZIM_DECOMPRESS_H_
#define ZIM_DECOMPRESS_H_

#include <cstdint>
#include <string>

namespace zim
{

// Low nibble of a cluster's leading info byte. 2 and 3 are legacy and were never used by
// any shipped ZIM; 4 (LZMA) is not built in yet.
enum class Compression
{
    Default = 0,
    None = 1,
    Zlib = 2,
    Bzip2 = 3,
    Lzma = 4,
    Zstd = 5,
};

const char *compression_name(Compression algo);
bool compression_supported(Compression algo);

// Decompresses all of `in` into `out`. Cluster sizes are not recorded anywhere, and zstd
// frames written by a streaming encoder do not carry one, so this grows the output rather
// than asking for a size up front.
bool zim_decompress(Compression algo, const char *in, size_t in_len, std::string &out);

}

#endif
