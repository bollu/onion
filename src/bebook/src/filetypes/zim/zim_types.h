#ifndef ZIM_TYPES_H_
#define ZIM_TYPES_H_

#include <cstdint>
#include <string>

namespace zim
{

// Explicit little-endian loads. The device build is -marm -march=armv7ve, where casting a
// buffer to a packed struct and loading a 64-bit field at an odd offset misreads or faults;
// it would still pass in the x86 test container.
inline uint16_t rd_u16(const uint8_t *p)
{
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                 static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t rd_u32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) |
           static_cast<uint32_t>(p[1]) << 8 |
           static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

inline uint64_t rd_u64(const uint8_t *p)
{
    return static_cast<uint64_t>(rd_u32(p)) |
           static_cast<uint64_t>(rd_u32(p + 4)) << 32;
}

const uint32_t ZIM_MAGIC = 0x044d495a;
const uint64_t ZIM_HEADER_SIZE = 80;

// Mime type sentinels stored in a dirent's mime field.
const uint16_t MIME_REDIRECT = 0xffff;
const uint16_t MIME_LINKTARGET = 0xfffe;
const uint16_t MIME_DELETED = 0xfffd;

struct ZimHeader
{
    uint16_t major = 0;
    uint16_t minor = 0;
    uint32_t entry_count = 0;
    uint32_t cluster_count = 0;
    uint64_t path_ptr_pos = 0;
    uint64_t title_idx_pos = 0;
    uint64_t cluster_ptr_pos = 0;
    uint64_t mime_list_pos = 0;
    uint32_t main_page = 0;
    uint32_t layout_page = 0;
    uint64_t checksum_pos = 0;

    // Minor version 1 introduced the C/M/W/X namespaces, replacing A/I/M/-.
    bool new_namespace_scheme() const { return minor >= 1; }
    bool has_main_page() const { return main_page != 0xffffffffu; }
};

struct ZimDirent
{
    uint16_t mime = 0;
    char ns = '\0';
    uint32_t revision = 0;

    uint32_t redirect_index = 0;
    uint32_t cluster = 0;
    uint32_t blob = 0;

    std::string path;
    std::string title;

    bool is_redirect() const { return mime == MIME_REDIRECT; }
    bool is_linktarget() const { return mime == MIME_LINKTARGET; }
    bool is_deleted() const { return mime == MIME_DELETED; }
    bool is_article() const { return !is_redirect() && !is_linktarget() && !is_deleted(); }

    // An empty stored title means the title is the path.
    const std::string &display_title() const { return title.empty() ? path : title; }
};

}

#endif
