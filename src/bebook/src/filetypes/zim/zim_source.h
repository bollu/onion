#ifndef ZIM_SOURCE_H_
#define ZIM_SOURCE_H_

#include <cstdint>
#include <cstdio>
#include <string>

namespace zim
{

// Random-access bytes behind a ZIM. The seam exists so tests can drive ZimFile from a
// synthetic archive in memory, with no 112MB file present.
class ZimSource
{
public:
    virtual ~ZimSource() = default;

    // False on short read or error; `dst` is then unspecified.
    virtual bool read(void *dst, uint64_t len, uint64_t offset) const = 0;
    virtual uint64_t size() const = 0;

    bool read_string(std::string &dst, uint64_t len, uint64_t offset) const;
};

class FileZimSource : public ZimSource
{
    std::FILE *fp;
    uint64_t file_size;

public:
    explicit FileZimSource(const std::string &path);
    ~FileZimSource() override;

    FileZimSource(const FileZimSource &) = delete;
    FileZimSource &operator=(const FileZimSource &) = delete;

    bool ok() const;
    bool read(void *dst, uint64_t len, uint64_t offset) const override;
    uint64_t size() const override;
};

class MemoryZimSource : public ZimSource
{
    std::string data;

public:
    explicit MemoryZimSource(std::string data);

    bool read(void *dst, uint64_t len, uint64_t offset) const override;
    uint64_t size() const override;
};

}

#endif
