#include "./library_index.h"

#include "./cover_extract.h"
#include "./ss_doc_reader_cache.h"
#include "./state_store.h"
#include "filetypes/open_doc.h"
#include "filetypes/epub/epub_reader.h"
#include "util/screenshot.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iostream>

namespace
{

constexpr const char *INDEX_FILE_NAME = "library.idx";
constexpr const char *COVERS_DIR_NAME = "covers";
constexpr const char *INDEX_FORMAT_VERSION = "bebook-library-1";

// Thumbnails are sized for the home screen's cover grid, not for full-screen display.
// MainUI's box art limit, per Onion's scraping documentation.
constexpr int BOX_ART_MAX_W = 250;
constexpr int BOX_ART_MAX_H = 360;

constexpr int COVER_MAX_W = 200;
constexpr int COVER_MAX_H = 300;

constexpr int NUM_FIELDS = 9;

// Fields are tab separated, so any tab, newline or backslash in a value has to survive a
// round trip. Titles from real epubs do contain newlines.
std::string escape_field(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (char c: value)
    {
        switch (c)
        {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string unescape_field(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] != '\\' || i + 1 == value.size())
        {
            out += value[i];
            continue;
        }

        switch (value[++i])
        {
            case 't': out += '\t'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            default: out += value[i]; break;
        }
    }
    return out;
}

std::vector<std::string> split_fields(const std::string &line)
{
    std::vector<std::string> fields;
    size_t start = 0;
    while (true)
    {
        size_t sep = line.find('\t', start);
        if (sep == std::string::npos)
        {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, sep - start));
        start = sep + 1;
    }
    return fields;
}

int64_t parse_int(const std::string &value)
{
    try
    {
        return std::stoll(value);
    }
    catch (const std::exception &)
    {
        return 0;
    }
}

// Disk size and modification time, or {0, 0} if the file has gone away. The clock epoch
// is implementation defined, which is fine: the value is only ever compared for equality
// against a previous reading on the same machine.
std::pair<uint64_t, int64_t> stat_file(const std::filesystem::path &path)
{
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec)
    {
        return {0, 0};
    }

    auto mtime = std::filesystem::last_write_time(path, ec);
    if (ec)
    {
        return {size, 0};
    }

    return {size, static_cast<int64_t>(mtime.time_since_epoch().count())};
}

} // namespace

LibraryIndex::LibraryIndex(StateStore &store, std::filesystem::path books_dir)
    : store(store),
      books_dir(std::move(books_dir)),
      index_path(store.get_base_dir() / INDEX_FILE_NAME),
      covers_dir(store.get_base_dir() / COVERS_DIR_NAME)
{
    std::filesystem::create_directories(covers_dir);

    // Load the cache, keyed by path.
    std::vector<LibraryEntry> cached;
    {
        std::ifstream fp(index_path);
        std::string line;
        if (std::getline(fp, line) && line == INDEX_FORMAT_VERSION)
        {
            while (std::getline(fp, line))
            {
                auto fields = split_fields(line);
                if (fields.size() != NUM_FIELDS)
                {
                    continue;
                }

                cached.push_back(LibraryEntry{
                    std::filesystem::path(unescape_field(fields[0])),
                    unescape_field(fields[1]),
                    unescape_field(fields[2]),
                    unescape_field(fields[3]),
                    static_cast<uint64_t>(parse_int(fields[4])),
                    parse_int(fields[5]),
                    parse_int(fields[6]),
                    static_cast<int>(parse_int(fields[7])),
                    fields[8] == "1"
                });
            }
        }
    }

    // Reconcile against the directory. Books that have vanished drop out; books not yet
    // indexed get a placeholder so the home screen has a filename to show immediately,
    // with a zeroed size that makes stale_paths() pick them up.
    std::error_code ec;
    for (const auto &item: std::filesystem::directory_iterator(this->books_dir, ec))
    {
        std::error_code stat_ec;
        if (!item.is_regular_file(stat_ec) || !file_type_is_supported(item.path()))
        {
            continue;
        }

        auto cached_it = std::find_if(
            cached.begin(),
            cached.end(),
            [&item](const LibraryEntry &entry) { return entry.path == item.path(); }
        );

        if (cached_it != cached.end())
        {
            library_entries.push_back(std::move(*cached_it));
        }
        else
        {
            library_entries.push_back(LibraryEntry{
                item.path(),
                "",
                item.path().stem().string(),
                "",
                0, 0, 0, 0, false
            });
            dirty = true;
        }
    }

    if (library_entries.size() != cached.size())
    {
        dirty = true;
    }
}

const std::vector<LibraryEntry> &LibraryIndex::entries() const
{
    return library_entries;
}

std::vector<LibraryEntry> LibraryIndex::recents(size_t max) const
{
    std::vector<LibraryEntry> result;
    for (const auto &entry: library_entries)
    {
        if (entry.last_opened > 0)
        {
            result.push_back(entry);
        }
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const LibraryEntry &a, const LibraryEntry &b) { return a.last_opened > b.last_opened; }
    );

    if (result.size() > max)
    {
        result.resize(max);
    }

    return result;
}

std::vector<std::filesystem::path> LibraryIndex::stale_paths() const
{
    std::vector<std::filesystem::path> stale;
    for (const auto &entry: library_entries)
    {
        auto [size, mtime] = stat_file(entry.path);
        if (entry.book_id.empty() || entry.size_bytes != size || entry.mtime != mtime)
        {
            stale.push_back(entry.path);
        }
    }
    return stale;
}

LibraryEntry &LibraryIndex::entry_for(const std::filesystem::path &path)
{
    auto it = std::find_if(
        library_entries.begin(),
        library_entries.end(),
        [&path](const LibraryEntry &entry) { return entry.path == path; }
    );

    if (it != library_entries.end())
    {
        return *it;
    }

    // Indexing a book outside books_dir is legitimate: the reader can be handed any path
    // on the command line, and its progress is still worth remembering.
    library_entries.push_back(LibraryEntry{
        path, "", path.stem().string(), "", 0, 0, 0, 0, false
    });
    dirty = true;

    return library_entries.back();
}

void LibraryIndex::index_one(const std::filesystem::path &path)
{
    auto reader = create_doc_reader(path);
    if (!reader)
    {
        return;
    }

    // Going through the state store's cache means the expensive part of open() -- the
    // per-spine address widths -- is computed once here rather than again when the book
    // is first read.
    SSDocReaderCache cache(store);
    if (!reader->open(cache))
    {
        std::cerr << "Failed to index " << path << std::endl;
        return;
    }

    LibraryEntry &entry = entry_for(path);
    entry.book_id = reader->get_id();
    entry.title = path.stem().string();
    entry.author.clear();

    if (const auto *epub = dynamic_cast<const EPubReader *>(reader.get()))
    {
        const auto &metadata = epub->get_metadata();
        if (!metadata.title.empty())
        {
            entry.title = metadata.title;
        }
        entry.author = metadata.author;
    }

    auto [size, mtime] = stat_file(path);
    entry.size_bytes = size;
    entry.mtime = mtime;

    entry.has_cover = false;
    if (auto cover = extract_cover(path, COVER_MAX_W, COVER_MAX_H))
    {
        entry.has_cover = write_surface_png(cover.get(), cover_path(entry).string());
    }

    write_box_art(path);

    dirty = true;
}

std::filesystem::path LibraryIndex::box_art_path(const std::filesystem::path &path) const
{
    return path.parent_path() / "Imgs" / (path.stem().string() + ".png");
}

std::vector<std::filesystem::path> LibraryIndex::paths_missing_box_art() const
{
    std::vector<std::filesystem::path> missing;
    for (const auto &entry: library_entries)
    {
        std::error_code ec;
        if (!std::filesystem::exists(box_art_path(entry.path), ec))
        {
            missing.push_back(entry.path);
        }
    }
    return missing;
}

void LibraryIndex::ensure_box_art(const std::filesystem::path &path) const
{
    write_box_art(path);
}

void LibraryIndex::write_box_art(const std::filesystem::path &path) const
{
    // MainUI draws its own game lists -- including Recents -- from
    // <rom dir>/Imgs/<rom basename>.png, so a book only gets a cover there if something
    // puts one on disk. bebook has just decoded that cover anyway, so it writes the
    // MainUI-shaped copy too and the two stay in step.
    //
    // Written only when absent: the file is the user's to replace, and a scraped or
    // hand-made cover should survive re-indexing.
    const std::filesystem::path imgs_dir = path.parent_path() / "Imgs";
    const std::filesystem::path art = box_art_path(path);

    std::error_code ec;
    if (std::filesystem::exists(art, ec))
    {
        return;
    }

    // Inside the configured library the Imgs directory is ours to create -- MainUI
    // reads box art from there, so it existing is the point. Elsewhere, the user is
    // browsing some folder of their own and scattering directories would be litter,
    // so only write when one is already there.
    if (!std::filesystem::is_directory(imgs_dir, ec))
    {
        std::error_code parent_ec;
        const bool in_library =
            std::filesystem::equivalent(path.parent_path(), books_dir, parent_ec) &&
            !parent_ec;
        if (!in_library || !std::filesystem::create_directories(imgs_dir, ec))
        {
            return;
        }
    }

    if (auto cover = extract_cover(path, BOX_ART_MAX_W, BOX_ART_MAX_H))
    {
        write_surface_png(cover.get(), art.string());
    }
}

void LibraryIndex::note_opened(const std::filesystem::path &path, int progress_percent)
{
    // The Miyoo Mini has no battery-backed clock, so wall time can repeat or run
    // backwards between sessions. Forcing the stamp past every existing one keeps
    // recents ordered by actual open order regardless.
    int64_t stamp = static_cast<int64_t>(std::time(nullptr));
    for (const auto &other: library_entries)
    {
        stamp = std::max(stamp, other.last_opened + 1);
    }

    LibraryEntry &entry = entry_for(path);
    entry.last_opened = stamp;
    entry.progress_percent = progress_percent;
    dirty = true;

    // Launching from the games list never reaches index_one(), which is what the shelf
    // uses, so write the cover here too or a book opened that way never gets one.
    write_box_art(path);
}

std::filesystem::path LibraryIndex::cover_path(const LibraryEntry &entry) const
{
    return covers_dir / (entry.book_id + ".png");
}

void LibraryIndex::flush()
{
    if (!dirty)
    {
        return;
    }

    std::ofstream fp(index_path);
    fp << INDEX_FORMAT_VERSION << "\n";
    for (const auto &entry: library_entries)
    {
        fp << escape_field(entry.path.string()) << "\t"
           << escape_field(entry.book_id) << "\t"
           << escape_field(entry.title) << "\t"
           << escape_field(entry.author) << "\t"
           << entry.size_bytes << "\t"
           << entry.mtime << "\t"
           << entry.last_opened << "\t"
           << entry.progress_percent << "\t"
           << (entry.has_cover ? "1" : "0") << "\n";
    }

    dirty = false;
}
