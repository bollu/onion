#include "./zip_utils.h"

#include <zip.h>
#include <iostream>

namespace
{

// Reads `size` bytes of `filepath` into `out`, which the caller has already sized.
// Returns false if the entry cannot be stat'd or opened.
bool read_zip_entry(zip_t *zip, const std::string &filepath, char *out, zip_uint64_t &size)
{
    if (zip == nullptr)
    {
        throw std::runtime_error("Zip is not open");
    }

    zip_file_t *fp = zip_fopen(zip, filepath.c_str(), 0);
    if (fp == nullptr)
    {
        std::cerr << "Unable to open " << filepath << " in epub" << std::endl;
        return false;
    }

    auto read_size = zip_fread(fp, out, size);
    if (read_size < 0 || (zip_uint64_t)read_size != size)
    {
        std::cerr << "Read unexpected number of bytes for " << filepath << " in epub"
            << " expected " << size
            << " got " << read_size
            << std::endl;
        size = read_size > 0 ? (zip_uint64_t)read_size : 0;
    }

    zip_fclose(fp);
    return true;
}

// Writes the uncompressed size of `filepath` to `out_size`. False if it is not in the
// archive or the archive does not record a size for it.
bool zip_entry_size(zip_t *zip, const std::string &filepath, zip_uint64_t &out_size)
{
    if (zip == nullptr)
    {
        throw std::runtime_error("Zip is not open");
    }

    zip_stat_t stats;
    if (zip_stat(zip, filepath.c_str(), 0, &stats) != 0 || !(stats.valid & ZIP_STAT_SIZE))
    {
        std::cerr << "Unable to get size of " << filepath << " in epub" << std::endl;
        return false;
    }

    out_size = stats.size;
    return true;
}

} // namespace

std::vector<char> read_zip_file(zip_t *zip, const std::string &filepath)
{
    zip_uint64_t size = 0;
    if (!zip_entry_size(zip, filepath, size))
    {
        return {};
    }

    std::vector<char> buffer(size);
    if (!read_zip_entry(zip, filepath, buffer.data(), size))
    {
        return {};
    }

    buffer.resize(size);
    return buffer;
}

std::string read_zip_file_str(zip_t *zip, const std::string &filepath)
{
    zip_uint64_t size = 0;
    if (!zip_entry_size(zip, filepath, size))
    {
        return {};
    }

    // std::string is the right container for text entries: data() is NUL-terminated for
    // the strlen-based XML parsers, while size() stays the true byte count.
    std::string buffer(size, '\0');
    if (!read_zip_entry(zip, filepath, &buffer[0], size))
    {
        return {};
    }

    buffer.resize(size);
    return buffer;
}
