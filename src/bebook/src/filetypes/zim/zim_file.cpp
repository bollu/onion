#include "./zim_file.h"

#include "./zim_decompress.h"

#include <algorithm>
#include <utility>

namespace
{

// Decompressed clusters are ~2MB each at zim-tools defaults. Four is enough to keep an
// article and its neighbours warm without crowding a 128MB device.
const uint64_t CLUSTER_CACHE_BUDGET = 8 * 1024 * 1024;

const uint32_t DIRENT_CACHE_ENTRIES = 512;

// Dirents are a fixed part plus three NUL-terminated strings. Read a window and grow it
// only if the strings run past the end.
const uint64_t DIRENT_WINDOW = 512;
const uint64_t DIRENT_WINDOW_MAX = 64 * 1024;

}

namespace zim
{

ZimFile::ZimFile(std::unique_ptr<ZimSource> src)
    : src(std::move(src)),
      opened(false),
      cluster_cache_bytes(0)
{
}

ZimFile::~ZimFile() = default;

bool ZimFile::is_open() const
{
    return opened;
}

const ZimHeader &ZimFile::header() const
{
    return hdr;
}

const std::string &ZimFile::last_error() const
{
    return error;
}

char ZimFile::content_namespace() const
{
    return hdr.new_namespace_scheme() ? 'C' : 'A';
}

std::string ZimFile::mime_type(uint16_t index) const
{
    if (index >= mime_types.size())
    {
        return {};
    }
    return mime_types[index];
}

bool ZimFile::open()
{
    if (!read_header())
    {
        return false;
    }
    if (!read_mime_list())
    {
        return false;
    }
    if (!read_pointer_list(hdr.path_ptr_pos, hdr.entry_count, path_ptrs))
    {
        error = "could not read the path pointer list";
        return false;
    }
    if (!read_pointer_list(hdr.cluster_ptr_pos, hdr.cluster_count, cluster_ptrs))
    {
        error = "could not read the cluster pointer list";
        return false;
    }

    opened = true;
    return true;
}

bool ZimFile::read_header()
{
    uint8_t buf[ZIM_HEADER_SIZE];
    if (!src->read(buf, ZIM_HEADER_SIZE, 0))
    {
        error = "file is shorter than a ZIM header";
        return false;
    }

    if (rd_u32(buf) != ZIM_MAGIC)
    {
        error = "not a ZIM file (bad magic)";
        return false;
    }

    hdr.major = rd_u16(buf + 4);
    if (hdr.major != 5 && hdr.major != 6)
    {
        error = "unsupported ZIM major version " + std::to_string(hdr.major);
        return false;
    }

    hdr.minor = rd_u16(buf + 6);
    hdr.entry_count = rd_u32(buf + 24);
    hdr.cluster_count = rd_u32(buf + 28);
    hdr.path_ptr_pos = rd_u64(buf + 32);
    hdr.title_idx_pos = rd_u64(buf + 40);
    hdr.cluster_ptr_pos = rd_u64(buf + 48);
    hdr.mime_list_pos = rd_u64(buf + 56);
    hdr.main_page = rd_u32(buf + 64);
    hdr.layout_page = rd_u32(buf + 68);
    hdr.checksum_pos = rd_u64(buf + 72);

    const uint64_t file_size = src->size();
    if (hdr.path_ptr_pos > file_size || hdr.cluster_ptr_pos > file_size ||
        hdr.mime_list_pos > file_size)
    {
        error = "ZIM header points outside the file";
        return false;
    }

    // The counts drive allocations of count * 8 bytes, so a corrupt entry_count of
    // 0xFFFFFFFF would ask for 34GB. Each pointer list has to fit in the file it lives in.
    if (static_cast<uint64_t>(hdr.entry_count) * 8 > file_size - hdr.path_ptr_pos)
    {
        error = "ZIM header claims more entries than the file can hold";
        return false;
    }
    if (static_cast<uint64_t>(hdr.cluster_count) * 8 > file_size - hdr.cluster_ptr_pos)
    {
        error = "ZIM header claims more clusters than the file can hold";
        return false;
    }

    return true;
}

bool ZimFile::read_mime_list()
{
    // Terminated by an empty string; bounded so a corrupt file cannot spin.
    uint64_t at = hdr.mime_list_pos;
    const uint64_t end = std::min(src->size(), hdr.mime_list_pos + 64 * 1024);

    mime_types.clear();
    while (at < end)
    {
        std::string chunk;
        const uint64_t want = std::min<uint64_t>(1024, end - at);
        if (!src->read_string(chunk, want, at))
        {
            error = "could not read the mime list";
            return false;
        }

        const auto nul = chunk.find('\0');
        if (nul == std::string::npos)
        {
            error = "unterminated mime list entry";
            return false;
        }
        if (nul == 0)
        {
            return true;
        }

        mime_types.push_back(chunk.substr(0, nul));
        at += nul + 1;
    }

    error = "mime list ran past the end of the file";
    return false;
}

bool ZimFile::read_pointer_list(uint64_t pos, uint32_t count, std::vector<uint64_t> &out)
{
    out.clear();
    if (count == 0)
    {
        return true;
    }

    const uint64_t bytes = static_cast<uint64_t>(count) * 8;
    std::string raw;
    if (!src->read_string(raw, bytes, pos))
    {
        return false;
    }

    out.reserve(count);
    const uint8_t *p = reinterpret_cast<const uint8_t *>(raw.data());
    for (uint32_t i = 0; i < count; ++i)
    {
        out.push_back(rd_u64(p + i * 8));
    }
    return true;
}

bool ZimFile::parse_dirent(uint64_t offset, ZimDirent &out) const
{
    const uint64_t file_size = src->size();
    if (offset + 12 > file_size)
    {
        return false;
    }

    uint64_t window = std::min<uint64_t>(DIRENT_WINDOW, file_size - offset);
    std::string buf;

    while (true)
    {
        if (!src->read_string(buf, window, offset))
        {
            return false;
        }

        const uint8_t *p = reinterpret_cast<const uint8_t *>(buf.data());
        out.mime = rd_u16(p);
        out.ns = static_cast<char>(p[3]);
        out.revision = rd_u32(p + 4);

        size_t fixed = 0;
        if (out.is_redirect())
        {
            if (buf.size() < 12)
            {
                return false;
            }
            out.redirect_index = rd_u32(p + 8);
            out.cluster = 0;
            out.blob = 0;
            fixed = 12;
        }
        else
        {
            if (buf.size() < 16)
            {
                return false;
            }
            out.cluster = rd_u32(p + 8);
            out.blob = rd_u32(p + 12);
            out.redirect_index = 0;
            fixed = 16;
        }

        const auto path_end = buf.find('\0', fixed);
        if (path_end != std::string::npos)
        {
            const auto title_end = buf.find('\0', path_end + 1);
            if (title_end != std::string::npos)
            {
                out.path = buf.substr(fixed, path_end - fixed);
                out.title = buf.substr(path_end + 1, title_end - path_end - 1);
                return true;
            }
        }

        // Strings ran past the window. Widen, unless we already hit the file end or the cap.
        if (window >= file_size - offset || window >= DIRENT_WINDOW_MAX)
        {
            return false;
        }
        window = std::min({window * 4, file_size - offset, DIRENT_WINDOW_MAX});
    }
}

bool ZimFile::dirent(uint32_t entry_index, ZimDirent &out) const
{
    if (entry_index >= path_ptrs.size())
    {
        return false;
    }

    if (dirent_cache.has(entry_index))
    {
        out = dirent_cache[entry_index];
        return true;
    }

    if (!parse_dirent(path_ptrs[entry_index], out))
    {
        error = "could not parse dirent " + std::to_string(entry_index);
        return false;
    }

    while (dirent_cache.size() >= DIRENT_CACHE_ENTRIES)
    {
        dirent_cache.pop();
    }
    dirent_cache.put(entry_index, out);
    return true;
}

bool ZimFile::find_by_path(char ns, const std::string &path, uint32_t &out_index) const
{
    uint32_t lo = 0;
    uint32_t hi = static_cast<uint32_t>(path_ptrs.size());

    while (lo < hi)
    {
        const uint32_t mid = lo + (hi - lo) / 2;

        ZimDirent probe;
        if (!dirent(mid, probe))
        {
            return false;
        }

        const unsigned char probe_ns = static_cast<unsigned char>(probe.ns);
        const unsigned char want_ns = static_cast<unsigned char>(ns);

        bool less;
        if (probe_ns != want_ns)
        {
            less = probe_ns < want_ns;
        }
        else
        {
            less = probe.path.compare(path) < 0;
        }

        if (less)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }

    if (lo >= path_ptrs.size())
    {
        return false;
    }

    ZimDirent hit;
    if (!dirent(lo, hit) || hit.ns != ns || hit.path != path)
    {
        return false;
    }

    out_index = lo;
    return true;
}

bool ZimFile::resolve(uint32_t entry_index, uint32_t &out_index, int max_hops) const
{
    uint32_t at = entry_index;
    for (int hop = 0; hop <= max_hops; ++hop)
    {
        ZimDirent d;
        if (!dirent(at, d))
        {
            return false;
        }
        if (!d.is_redirect())
        {
            if (!d.is_article())
            {
                return false;
            }
            out_index = at;
            return true;
        }
        if (d.redirect_index == at)
        {
            return false;
        }
        at = d.redirect_index;
    }

    error = "redirect chain too long";
    return false;
}

std::shared_ptr<const ClusterData> ZimFile::load_cluster(uint32_t cluster) const
{
    if (cluster >= cluster_ptrs.size())
    {
        return nullptr;
    }

    if (cluster_cache.has(cluster))
    {
        return cluster_cache[cluster];
    }

    const uint64_t start = cluster_ptrs[cluster];
    uint64_t end = src->size();
    if (cluster + 1 < cluster_ptrs.size())
    {
        end = cluster_ptrs[cluster + 1];
    }
    else if (hdr.checksum_pos > start && hdr.checksum_pos <= src->size())
    {
        end = hdr.checksum_pos;
    }

    // `end` comes from the next cluster pointer, which is unvalidated file data: clamp it
    // rather than letting end - start ask for a read past the end of the archive.
    end = std::min(end, src->size());

    if (end <= start || start >= src->size())
    {
        error = "cluster " + std::to_string(cluster) + " has a bad extent";
        return nullptr;
    }

    uint8_t info = 0;
    if (!src->read(&info, 1, start))
    {
        return nullptr;
    }

    const Compression algo = static_cast<Compression>(info & 0x0F);
    const bool extended = (info & 0x10) != 0;

    if (!compression_supported(algo))
    {
        error = std::string("unsupported cluster compression ") + compression_name(algo) +
                " (" + std::to_string(info & 0x0F) + ")";
        return nullptr;
    }

    std::string body;
    if (!src->read_string(body, end - start - 1, start + 1))
    {
        return nullptr;
    }

    auto out = std::make_shared<ClusterData>();
    if (!zim_decompress(algo, body.data(), body.size(), out->data))
    {
        error = "could not decompress cluster " + std::to_string(cluster);
        return nullptr;
    }

    const size_t width = extended ? 8 : 4;
    if (out->data.size() < width)
    {
        error = "cluster " + std::to_string(cluster) + " is too small for an offset table";
        return nullptr;
    }

    const uint8_t *p = reinterpret_cast<const uint8_t *>(out->data.data());
    const uint64_t first = extended ? rd_u64(p) : rd_u32(p);
    if (first < width || first % width != 0 || first > out->data.size())
    {
        error = "cluster " + std::to_string(cluster) + " has a bad first blob offset";
        return nullptr;
    }

    const uint64_t n_offsets = first / width;
    out->offsets.reserve(static_cast<size_t>(n_offsets));
    for (uint64_t i = 0; i < n_offsets; ++i)
    {
        const uint64_t at = extended ? rd_u64(p + i * 8) : rd_u32(p + i * 4);
        if (at > out->data.size() || (i > 0 && at < out->offsets[static_cast<size_t>(i) - 1]))
        {
            error = "cluster " + std::to_string(cluster) + " has a non-monotonic offset table";
            return nullptr;
        }
        out->offsets.push_back(at);
    }

    const uint64_t added = out->data.size();
    while (cluster_cache.size() > 0 && cluster_cache_bytes + added > CLUSTER_CACHE_BUDGET)
    {
        cluster_cache_bytes -= cluster_cache.back_value()->data.size();
        cluster_cache.pop();
    }
    cluster_cache.put(cluster, out);
    cluster_cache_bytes += added;

    return out;
}

bool ZimFile::read_blob(uint32_t cluster, uint32_t blob, std::string &out) const
{
    const auto data = load_cluster(cluster);
    if (data == nullptr)
    {
        return false;
    }

    // offsets holds n_blobs + 1 entries, so blob i needs i + 1 to exist.
    if (static_cast<size_t>(blob) + 1 >= data->offsets.size())
    {
        error = "blob " + std::to_string(blob) + " is out of range for cluster " +
                std::to_string(cluster);
        return false;
    }

    const uint64_t from = data->offsets[blob];
    const uint64_t to = data->offsets[static_cast<size_t>(blob) + 1];
    out.assign(data->data, static_cast<size_t>(from), static_cast<size_t>(to - from));
    return true;
}

bool ZimFile::find_content(const std::string &path, uint32_t &out_index, ZimDirent &out_dirent) const
{
    uint32_t found = 0;
    if (!find_by_path(content_namespace(), path, found))
    {
        return false;
    }

    uint32_t resolved = 0;
    if (!resolve(found, resolved))
    {
        return false;
    }

    if (!dirent(resolved, out_dirent))
    {
        return false;
    }

    out_index = resolved;
    return true;
}

bool ZimFile::read_content(const std::string &path, std::string &out) const
{
    uint32_t index = 0;
    ZimDirent d;
    if (!find_content(path, index, d))
    {
        return false;
    }
    return read_blob(d.cluster, d.blob, out);
}

}
