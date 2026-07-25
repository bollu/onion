#include "./zim_source.h"

#include <cstring>
#include <utility>

namespace zim
{

bool ZimSource::read_string(std::string &dst, uint64_t len, uint64_t offset) const
{
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

    if (std::fseek(fp, 0, SEEK_END) != 0)
    {
        std::fclose(fp);
        fp = nullptr;
        return;
    }

    const long end = std::ftell(fp);
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

    if (std::fseek(fp, static_cast<long>(offset), SEEK_SET) != 0)
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
