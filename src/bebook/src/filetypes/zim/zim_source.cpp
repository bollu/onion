// Before any header, so off_t is 64-bit on a 32-bit target and fseeko can reach
// past 2GB in the full-Wikipedia archives.
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include "./zim_source.h"

#include <cstring>
#include <utility>

namespace zim
{

bool ZimSource::read_string(std::string &dst, uint64_t len, uint64_t offset) const
{
    // Bounds-checked before resizing, not after: len comes from header fields that a
    // corrupt or truncated archive can set to anything, and resize() would then try to
    // allocate tens of GB and terminate rather than report a bad file.
    const uint64_t total = size();
    if (offset > total || len > total - offset || len > dst.max_size())
    {
        return false;
    }

    dst.resize(static_cast<size_t>(len));
    if (len == 0)
    {
        return true;
    }
    return read(&dst[0], len, offset);
}

FileZimSource::FileZimSource(const std::string &path)
    : fp(std::fopen(path.c_str(), "rb")),
      file_size(0)
{
    if (fp == nullptr)
    {
        return;
    }

    // fseeko/ftello rather than fseek/ftell: long is 32 bits on the device, so anything
    // past 2GB would wrap and silently seek to the wrong place. top_nopic is 836MB, but
    // the full-Wikipedia archives are 2.2GB and up.
    if (fseeko(fp, 0, SEEK_END) != 0)
    {
        std::fclose(fp);
        fp = nullptr;
        return;
    }

    const off_t end = ftello(fp);
    if (end < 0)
    {
        std::fclose(fp);
        fp = nullptr;
        return;
    }
    file_size = static_cast<uint64_t>(end);
}

FileZimSource::~FileZimSource()
{
    if (fp != nullptr)
    {
        std::fclose(fp);
    }
}

bool FileZimSource::ok() const
{
    return fp != nullptr;
}

bool FileZimSource::read(void *dst, uint64_t len, uint64_t offset) const
{
    if (fp == nullptr || offset > file_size || len > file_size - offset)
    {
        return false;
    }
    if (len == 0)
    {
        return true;
    }

    if (fseeko(fp, static_cast<off_t>(offset), SEEK_SET) != 0)
    {
        return false;
    }
    return std::fread(dst, 1, static_cast<size_t>(len), fp) == len;
}

uint64_t FileZimSource::size() const
{
    return file_size;
}

MemoryZimSource::MemoryZimSource(std::string data)
    : data(std::move(data))
{
}

bool MemoryZimSource::read(void *dst, uint64_t len, uint64_t offset) const
{
    if (offset > data.size() || len > data.size() - offset)
    {
        return false;
    }
    if (len > 0)
    {
        std::memcpy(dst, data.data() + offset, static_cast<size_t>(len));
    }
    return true;
}

uint64_t MemoryZimSource::size() const
{
    return data.size();
}

}
