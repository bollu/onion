#ifndef ZIM_FILE_H_
#define ZIM_FILE_H_

#include "./zim_source.h"
#include "./zim_types.h"

#include "util/lru_cache.h"

#include <memory>
#include <string>
#include <vector>

namespace zim
{

// One decompressed cluster: the raw bytes plus the blob offset table parsed off its front.
struct ClusterData
{
    std::string data;
    std::vector<uint64_t> offsets;
};

// Read-only access to a ZIM archive. Not thread safe; one instance is shared by every
// ZimArticleReader over the same file so the cluster cache is shared with it.
class ZimFile
{
public:
    explicit ZimFile(std::unique_ptr<ZimSource> src);
    ~ZimFile();

    ZimFile(const ZimFile &) = delete;
    ZimFile &operator=(const ZimFile &) = delete;

    bool open();
    bool is_open() const;

    const ZimHeader &header() const;
    char content_namespace() const;
    std::string mime_type(uint16_t index) const;

    bool dirent(uint32_t entry_index, ZimDirent &out) const;

    // Binary search over the path pointer list, which is sorted by (namespace, path).
    bool find_by_path(char ns, const std::string &path, uint32_t &out_index) const;

    // Walks redirect entries to the article they name. False on a cycle or a broken link.
    bool resolve(uint32_t entry_index, uint32_t &out_index, int max_hops = 8) const;

    bool read_blob(uint32_t cluster, uint32_t blob, std::string &out) const;

    // find_by_path in the content namespace, then resolve, then read.
    bool read_content(const std::string &path, std::string &out) const;
    bool find_content(const std::string &path, uint32_t &out_index, ZimDirent &out_dirent) const;

    // Last failure, for diagnostics. Never part of control flow.
    const std::string &last_error() const;

private:
    bool read_header();
    bool read_mime_list();
    bool read_pointer_list(uint64_t pos, uint32_t count, std::vector<uint64_t> &out);
    bool parse_dirent(uint64_t offset, ZimDirent &out) const;
    std::shared_ptr<const ClusterData> load_cluster(uint32_t cluster) const;

    std::unique_ptr<ZimSource> src;
    ZimHeader hdr;
    bool opened;

    std::vector<std::string> mime_types;
    std::vector<uint64_t> path_ptrs;
    std::vector<uint64_t> cluster_ptrs;

    mutable LRUCache<uint32_t, std::shared_ptr<const ClusterData>> cluster_cache;
    mutable uint64_t cluster_cache_bytes;
    mutable LRUCache<uint32_t, ZimDirent> dirent_cache;
    mutable std::string error;
};

}

#endif
