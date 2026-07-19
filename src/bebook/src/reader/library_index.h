#ifndef LIBRARY_INDEX_H_
#define LIBRARY_INDEX_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class StateStore;

struct LibraryEntry
{
    std::filesystem::path path;
    std::string book_id;      // md5 from the reader
    std::string title;        // falls back to filename stem
    std::string author;
    uint64_t size_bytes;
    int64_t  mtime;
    int64_t  last_opened;     // 0 if never
    int      progress_percent;
    bool     has_cover;
};

// A persisted view of a books directory, so the home screen never has to open every zip
// on launch. Everything except index_one() is directory-listing cheap.
class LibraryIndex
{
    StateStore &store;
    std::filesystem::path books_dir;
    std::filesystem::path index_path;
    std::filesystem::path covers_dir;
    std::vector<LibraryEntry> library_entries;
    bool dirty = false;

    LibraryEntry &entry_for(const std::filesystem::path &path);

    // Writes the cover in the shape MainUI expects beside the book, so that books
    // launched as OnionOS "roms" show art in the game list and in Recents.
    void write_box_art(const std::filesystem::path &path) const;

public:
    LibraryIndex(StateStore &store, std::filesystem::path books_dir);
    LibraryIndex(const LibraryIndex &) = delete;
    LibraryIndex &operator=(const LibraryIndex &) = delete;

    // Cheap: reads the cached index only. Never opens a zip.
    const std::vector<LibraryEntry> &entries() const;
    std::vector<LibraryEntry> recents(size_t max) const;   // sorted by last_opened desc

    // Returns paths needing (re)indexing: new files, or size/mtime changed.
    std::vector<std::filesystem::path> stale_paths() const;

    // Opens ONE book, extracts metadata + cover, updates the entry. Slow; call off the
    // render thread.
    void index_one(const std::filesystem::path &path);

    void note_opened(const std::filesystem::path &path, int progress_percent);

    std::filesystem::path cover_path(const LibraryEntry &) const;   // <store>/covers/<id>.png

    void flush();
};

#endif
