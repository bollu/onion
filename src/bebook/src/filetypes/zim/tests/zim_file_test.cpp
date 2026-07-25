#include "../zim_file.h"
#include "../zim_types.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace zim;

namespace
{

// A real 48-byte zstd frame whose plaintext is a 2-blob u32 offset table followed by
// "zstd-one" and "zstd-two-longer". Precomputed because the vendored zstd is decode-only.
const uint8_t ZSTD_CLUSTER_BODY[] = {
    0x28, 0xb5, 0x2f, 0xfd, 0x24, 0x23, 0x19, 0x01, 0x00, 0x0c, 0x00, 0x00,
    0x00, 0x14, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x7a, 0x73, 0x74,
    0x64, 0x2d, 0x6f, 0x6e, 0x65, 0x7a, 0x73, 0x74, 0x64, 0x2d, 0x74, 0x77,
    0x6f, 0x2d, 0x6c, 0x6f, 0x6e, 0x67, 0x65, 0x72, 0x51, 0x35, 0x65, 0xa9,
};

void put_u16(std::string &s, uint16_t v)
{
    s.push_back(static_cast<char>(v & 0xff));
    s.push_back(static_cast<char>((v >> 8) & 0xff));
}

void put_u32(std::string &s, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
    {
        s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    }
}

void put_u64(std::string &s, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
    {
        s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    }
}

struct DirentSpec
{
    uint16_t mime = 0;
    char ns = 'C';
    std::string path;
    std::string title;
    bool redirect = false;
    uint32_t redirect_index = 0;
    uint32_t cluster = 0;
    uint32_t blob = 0;
};

struct ClusterSpec
{
    uint8_t info = 0x01;
    std::vector<std::string> blobs;
    // When set, used verbatim after the info byte instead of building a table from `blobs`.
    std::string raw_body;
};

std::string serialize_dirent(const DirentSpec &d)
{
    std::string out;
    put_u16(out, d.redirect ? MIME_REDIRECT : d.mime);
    out.push_back('\0');            // parameter length
    out.push_back(d.ns);
    put_u32(out, 0);                // revision

    if (d.redirect)
    {
        put_u32(out, d.redirect_index);
    }
    else
    {
        put_u32(out, d.cluster);
        put_u32(out, d.blob);
    }

    out += d.path;
    out.push_back('\0');
    out += d.title;
    out.push_back('\0');
    return out;
}

std::string serialize_cluster(const ClusterSpec &c)
{
    std::string out;
    out.push_back(static_cast<char>(c.info));

    if (!c.raw_body.empty())
    {
        out += c.raw_body;
        return out;
    }

    const bool extended = (c.info & 0x10) != 0;
    const uint64_t width = extended ? 8 : 4;

    std::vector<uint64_t> offsets;
    offsets.push_back((c.blobs.size() + 1) * width);
    for (const auto &b : c.blobs)
    {
        offsets.push_back(offsets.back() + b.size());
    }

    for (uint64_t o : offsets)
    {
        if (extended)
        {
            put_u64(out, o);
        }
        else
        {
            put_u32(out, static_cast<uint32_t>(o));
        }
    }
    for (const auto &b : c.blobs)
    {
        out += b;
    }
    return out;
}

// Lays out header | mime list | dirents | path pointers | cluster pointers | clusters.
std::string build_zim(const std::vector<DirentSpec> &dirents,
                      const std::vector<ClusterSpec> &clusters,
                      const std::vector<std::string> &mimes = {"text/html", "image/png"},
                      uint16_t major = 6,
                      uint16_t minor = 1)
{
    std::string mime_list;
    for (const auto &m : mimes)
    {
        mime_list += m;
        mime_list.push_back('\0');
    }
    mime_list.push_back('\0');

    std::string dirent_blob;
    std::vector<uint64_t> dirent_offsets;
    const uint64_t dirent_start = ZIM_HEADER_SIZE + mime_list.size();
    for (const auto &d : dirents)
    {
        dirent_offsets.push_back(dirent_start + dirent_blob.size());
        dirent_blob += serialize_dirent(d);
    }

    const uint64_t path_ptr_pos = dirent_start + dirent_blob.size();
    const uint64_t cluster_ptr_pos = path_ptr_pos + dirents.size() * 8;
    const uint64_t cluster_start = cluster_ptr_pos + clusters.size() * 8;

    std::string cluster_blob;
    std::vector<uint64_t> cluster_offsets;
    for (const auto &c : clusters)
    {
        cluster_offsets.push_back(cluster_start + cluster_blob.size());
        cluster_blob += serialize_cluster(c);
    }

    std::string header;
    put_u32(header, ZIM_MAGIC);
    put_u16(header, major);
    put_u16(header, minor);
    header.append(16, '\0');                                  // uuid
    put_u32(header, static_cast<uint32_t>(dirents.size()));
    put_u32(header, static_cast<uint32_t>(clusters.size()));
    put_u64(header, path_ptr_pos);
    put_u64(header, 0);                                       // title index, unused here
    put_u64(header, cluster_ptr_pos);
    put_u64(header, ZIM_HEADER_SIZE);                         // mime list
    put_u32(header, 0);                                       // main page
    put_u32(header, 0xffffffffu);                             // layout page
    put_u64(header, 0);                                       // no checksum
    EXPECT_EQ(header.size(), ZIM_HEADER_SIZE);

    std::string path_ptrs;
    for (uint64_t o : dirent_offsets)
    {
        put_u64(path_ptrs, o);
    }
    std::string cluster_ptrs;
    for (uint64_t o : cluster_offsets)
    {
        put_u64(cluster_ptrs, o);
    }

    return header + mime_list + dirent_blob + path_ptrs + cluster_ptrs + cluster_blob;
}

// Sorted by (namespace, path), as a real ZIM's path pointer list is.
std::vector<DirentSpec> standard_dirents()
{
    std::vector<DirentSpec> d;
    d.push_back({0, 'C', "Alpha", "Alpha Title", false, 0, 0, 0});
    d.push_back({0, 'C', "Beta", "", true, 0, 0, 0});
    d.push_back({0, 'C', "Cycle", "", true, 2, 0, 0});
    d.push_back({MIME_DELETED, 'C', "Dead", "", false, 0, 0, 0});
    d.push_back({0, 'C', "Gamma", "", false, 0, 1, 1});
    d.push_back({0, 'M', "Counter", "", false, 0, 0, 1});
    return d;
}

std::vector<ClusterSpec> standard_clusters()
{
    std::vector<ClusterSpec> c;
    c.push_back({0x01, {"hello", "world!!"}, ""});           // uncompressed, u32 offsets
    c.push_back({0x11, {"one", "twotwo"}, ""});              // uncompressed, u64 offsets
    return c;
}

std::unique_ptr<ZimFile> open_zim(const std::string &bytes)
{
    auto file = std::unique_ptr<ZimFile>(
        new ZimFile(std::unique_ptr<ZimSource>(new MemoryZimSource(bytes))));
    file->open();
    return file;
}

}

TEST(ZimFile, ParsesTheHeader)
{
    auto z = open_zim(build_zim(standard_dirents(), standard_clusters()));

    ASSERT_TRUE(z->is_open()) << z->last_error();
    ASSERT_EQ(z->header().major, 6);
    ASSERT_EQ(z->header().minor, 1);
    ASSERT_EQ(z->header().entry_count, 6u);
    ASSERT_EQ(z->header().cluster_count, 2u);
    ASSERT_EQ(z->content_namespace(), 'C');
}

TEST(ZimFile, OldMinorVersionUsesTheLegacyNamespace)
{
    auto z = open_zim(build_zim(standard_dirents(), standard_clusters(), {"text/html"}, 5, 0));

    ASSERT_TRUE(z->is_open()) << z->last_error();
    ASSERT_EQ(z->content_namespace(), 'A');
}

TEST(ZimFile, RejectsBadMagic)
{
    std::string bytes = build_zim(standard_dirents(), standard_clusters());
    bytes[0] = 'X';

    auto z = open_zim(bytes);
    ASSERT_FALSE(z->is_open());
    ASSERT_NE(z->last_error().find("magic"), std::string::npos);
}

TEST(ZimFile, RejectsUnsupportedMajorVersion)
{
    auto z = open_zim(build_zim(standard_dirents(), standard_clusters(), {"text/html"}, 7, 1));

    ASSERT_FALSE(z->is_open());
    ASSERT_NE(z->last_error().find("major version"), std::string::npos);
}

TEST(ZimFile, ReadsTheMimeList)
{
    auto z = open_zim(build_zim(standard_dirents(), standard_clusters(),
                                {"text/html", "image/png", "application/octet-stream"}));

    ASSERT_TRUE(z->is_open()) << z->last_error();
    ASSERT_EQ(z->mime_type(0), "text/html");
    ASSERT_EQ(z->mime_type(1), "image/png");
    ASSERT_EQ(z->mime_type(2), "application/octet-stream");
    ASSERT_EQ(z->mime_type(3), "");
}

TEST(ZimFile, ReadsDirents)
{
    auto z = open_zim(build_zim(standard_dirents(), standard_clusters()));

    ZimDirent d;
    ASSERT_TRUE(z->dirent(0, d));
    ASSERT_EQ(d.ns, 'C');
    ASSERT_EQ(d.path, "Alpha");
    ASSERT_EQ(d.title, "Alpha Title");
    ASSERT_EQ(d.display_title(), "Alpha Title");
    ASSERT_TRUE(d.is_article());

    ASSERT_TRUE(z->dirent(4, d));
    ASSERT_EQ(d.path, "Gamma");
    ASSERT_EQ(d.title, "");
    ASSERT_EQ(d.display_title(), "Gamma") << "an empty title means the title is the path";

    ASSERT_TRUE(z->dirent(1, d));
    ASSERT_TRUE(d.is_redirect());
    ASSERT_EQ(d.redirect_index, 0u);

    ASSERT_TRUE(z->dirent(3, d));
    ASSERT_TRUE(d.is_deleted());
    ASSERT_FALSE(d.is_article());

    ASSERT_FALSE(z->dirent(6, d)) << "past the last entry";
}

TEST(ZimFile, FindsByPath)
{
    auto z = open_zim(build_zim(standard_dirents(), standard_clusters()));

    uint32_t index = 0;
    ASSERT_TRUE(z->find_by_path('C', "Alpha", index));
    ASSERT_EQ(index, 0u) << "the first entry";

    ASSERT_TRUE(z->find_by_path('C', "Gamma", index));
    ASSERT_EQ(index, 4u) << "the last entry in its namespace";

    ASSERT_TRUE(z->find_by_path('C', "Cycle", index));
    ASSERT_EQ(index, 2u);

    ASSERT_TRUE(z->find_by_path('M', "Counter", index)) << "a different namespace";
    ASSERT_EQ(index, 5u);
}

TEST(ZimFile, FindByPathMisses)
{
    auto z = open_zim(build_zim(standard_dirents(), standard_clusters()));

    uint32_t index = 0;
    ASSERT_FALSE(z->find_by_path('C', "AAAA", index)) << "sorts before every entry";
    ASSERT_FALSE(z->find_by_path('C', "Zzzz", index)) << "sorts after every entry in C";
    ASSERT_FALSE(z->find_by_path('C', "Alph", index)) << "a prefix is not a match";
    ASSERT_FALSE(z->find_by_path('C', "Alphaa", index)) << "an extension is not a match";
    ASSERT_FALSE(z->find_by_path('C', "", index));
    ASSERT_FALSE(z->find_by_path('X', "Alpha", index)) << "right path, wrong namespace";
}

TEST(ZimFile, ResolvesRedirects)
{
    auto z = open_zim(build_zim(standard_dirents(), standard_clusters()));

    uint32_t resolved = 0;
    ASSERT_TRUE(z->resolve(1, resolved)) << z->last_error();
    ASSERT_EQ(resolved, 0u) << "Beta redirects to Alpha";

    ASSERT_TRUE(z->resolve(0, resolved));
    ASSERT_EQ(resolved, 0u) << "an article resolves to itself";

    ASSERT_FALSE(z->resolve(2, resolved)) << "a self-referential redirect must not hang";
    ASSERT_FALSE(z->resolve(3, resolved)) << "a deleted entry is not content";
}

TEST(ZimFile, StopsAtTheRedirectHopLimit)
{
    // A chain longer than the hop limit: each entry points at the next.
    std::vector<DirentSpec> chain;
    for (int i = 0; i < 12; ++i)
    {
        DirentSpec d;
        d.ns = 'C';
        d.path = "R" + std::to_string(i);
        d.redirect = true;
        d.redirect_index = static_cast<uint32_t>(i + 1);
        chain.push_back(d);
    }
    chain.back().redirect = false;

    auto z = open_zim(build_zim(chain, standard_clusters()));

    uint32_t resolved = 0;
    ASSERT_FALSE(z->resolve(0, resolved, 4)) << "should give up rather than walk the chain";
    ASSERT_TRUE(z->resolve(0, resolved, 20));
    ASSERT_EQ(resolved, 11u);
}

TEST(ZimFile, ReadsBlobsFromAU32Cluster)
{
    auto z = open_zim(build_zim(standard_dirents(), standard_clusters()));

    std::string out;
    ASSERT_TRUE(z->read_blob(0, 0, out)) << z->last_error();
    ASSERT_EQ(out, "hello");

    ASSERT_TRUE(z->read_blob(0, 1, out));
    ASSERT_EQ(out, "world!!");
}

TEST(ZimFile, ReadsBlobsFromAU64Cluster)
{
    auto z = open_zim(build_zim(standard_dirents(), standard_clusters()));

    std::string out;
    ASSERT_TRUE(z->read_blob(1, 0, out)) << z->last_error();
    ASSERT_EQ(out, "one");

    ASSERT_TRUE(z->read_blob(1, 1, out)) << "the last cluster runs to the end of the file";
    ASSERT_EQ(out, "twotwo");
}

TEST(ZimFile, RejectsOutOfRangeBlobsAndClusters)
{
    auto z = open_zim(build_zim(standard_dirents(), standard_clusters()));

    std::string out;
    ASSERT_FALSE(z->read_blob(0, 2, out)) << "offsets holds n+1 entries, so blob 2 of 2 is out";
    ASSERT_FALSE(z->read_blob(9, 0, out));
}

TEST(ZimFile, ReadsAZstdCluster)
{
    std::vector<ClusterSpec> clusters = standard_clusters();
    ClusterSpec zstd_cluster;
    zstd_cluster.info = 0x05;
    zstd_cluster.raw_body.assign(reinterpret_cast<const char *>(ZSTD_CLUSTER_BODY),
                                 sizeof(ZSTD_CLUSTER_BODY));
    clusters.push_back(zstd_cluster);

    auto z = open_zim(build_zim(standard_dirents(), clusters));
    ASSERT_TRUE(z->is_open()) << z->last_error();

    std::string out;
    ASSERT_TRUE(z->read_blob(2, 0, out)) << z->last_error();
    ASSERT_EQ(out, "zstd-one");

    ASSERT_TRUE(z->read_blob(2, 1, out));
    ASSERT_EQ(out, "zstd-two-longer");
}

TEST(ZimFile, ReportsUnsupportedCompressionRatherThanCrashing)
{
    std::vector<ClusterSpec> clusters = standard_clusters();
    ClusterSpec lzma;
    lzma.info = 0x04;
    lzma.raw_body = "not really lzma";
    clusters.push_back(lzma);

    auto z = open_zim(build_zim(standard_dirents(), clusters));

    std::string out;
    ASSERT_FALSE(z->read_blob(2, 0, out));
    ASSERT_NE(z->last_error().find("lzma"), std::string::npos) << z->last_error();
}

TEST(ZimFile, ReadsContentByPathThroughARedirect)
{
    auto z = open_zim(build_zim(standard_dirents(), standard_clusters()));

    std::string out;
    ASSERT_TRUE(z->read_content("Alpha", out)) << z->last_error();
    ASSERT_EQ(out, "hello");

    ASSERT_TRUE(z->read_content("Beta", out)) << "Beta redirects to Alpha";
    ASSERT_EQ(out, "hello");

    ASSERT_TRUE(z->read_content("Gamma", out));
    ASSERT_EQ(out, "twotwo");

    ASSERT_FALSE(z->read_content("Nope", out));
}

TEST(ZimFile, FindContentReportsTheResolvedEntry)
{
    auto z = open_zim(build_zim(standard_dirents(), standard_clusters()));

    uint32_t index = 0;
    ZimDirent d;
    ASSERT_TRUE(z->find_content("Beta", index, d)) << z->last_error();
    ASSERT_EQ(index, 0u) << "the redirect target, not the redirect";
    ASSERT_EQ(d.path, "Alpha");
    ASSERT_EQ(d.display_title(), "Alpha Title");
}

TEST(ZimFile, LongPathsSpanTheDirentReadWindow)
{
    // Forces parse_dirent to widen its window; the path alone exceeds the initial read.
    std::vector<DirentSpec> dirents;
    DirentSpec big;
    big.ns = 'C';
    big.path = std::string(900, 'x');
    big.title = std::string(700, 'y');
    dirents.push_back(big);

    auto z = open_zim(build_zim(dirents, standard_clusters()));
    ASSERT_TRUE(z->is_open()) << z->last_error();

    ZimDirent d;
    ASSERT_TRUE(z->dirent(0, d)) << z->last_error();
    ASSERT_EQ(d.path.size(), 900u);
    ASSERT_EQ(d.title.size(), 700u);

    uint32_t index = 0;
    ASSERT_TRUE(z->find_by_path('C', big.path, index));
    ASSERT_EQ(index, 0u);
}

TEST(ZimFile, EmptyArchiveIsSafe)
{
    auto z = open_zim(build_zim({}, {}));
    ASSERT_TRUE(z->is_open()) << z->last_error();

    uint32_t index = 0;
    ZimDirent d;
    ASSERT_FALSE(z->find_by_path('C', "anything", index));
    ASSERT_FALSE(z->dirent(0, d));

    std::string out;
    ASSERT_FALSE(z->read_blob(0, 0, out));
}

TEST(ZimFile, TruncatedFileDoesNotCrash)
{
    const std::string bytes = build_zim(standard_dirents(), standard_clusters());

    for (size_t len : {size_t(0), size_t(10), size_t(79), size_t(90), bytes.size() / 2})
    {
        auto z = open_zim(bytes.substr(0, len));

        uint32_t index = 0;
        std::string out;
        z->find_by_path('C', "Alpha", index);
        z->read_blob(0, 0, out);
        z->read_content("Alpha", out);
    }
}
