#include "rom_screen.h"

#include "screenshot.h"

#include "reader/cover_extract.h"

extern "C" {
#include "extern/rotozoom/SDL_rotozoom.h"
}

#include "utils/hash.h"   // shared with the Game Switcher, via -I../common

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace
{

// Created if absent, as keymon does for RetroArch via file_open_ensure_path().
const char *ROM_SCREENS_DIR = "/mnt/SDCARD/Saves/CurrentProfile/romScreens";

const char *RECENTLIST_PATH = "/mnt/SDCARD/Roms/recentlist.json";
const char *RECENTLIST_HIDDEN_PATH = "/mnt/SDCARD/Roms/recentlist-hidden.json";

// Extracts "key":"value" from one JSON line, the way state.h and gs_history.h both do.
std::string json_field(const std::string &line, const std::string &key)
{
    const std::string needle = "\"" + key + "\":\"";
    const auto start = line.find(needle);
    if (start == std::string::npos)
    {
        return {};
    }
    const auto from = start + needle.size();
    const auto end = line.find('"', from);
    return end == std::string::npos ? std::string{} : line.substr(from, end - from);
}

// The rompath as the Game Switcher sees it: first entry of the recent list, launcher
// prefix stripped. Hashing argv[1] instead only works while MainUI happens to record
// the same spelling it passes, and a mismatch is silent -- the tile just never appears.
std::string recent_rompath()
{
    for (const char *path : {RECENTLIST_HIDDEN_PATH, RECENTLIST_PATH})
    {
        std::ifstream file(path);
        std::string line;
        if (!file || !std::getline(file, line))
        {
            continue;
        }

        if (line.find("\"type\":5") == std::string::npos &&
            line.find("\"type\":17") == std::string::npos &&
            line.find("\"type\": 5") == std::string::npos &&
            line.find("\"type\": 17") == std::string::npos)
        {
            continue;
        }

        std::string rompath = json_field(line, "rompath");
        const auto colon = rompath.find(':');
        if (colon != std::string::npos)
        {
            rompath = rompath.substr(colon + 1);
        }
        if (!rompath.empty())
        {
            return rompath;
        }
    }
    return {};
}



// Cover on the left, the page just read on the right, each scaled to fit its half.
// Null when the book has no cover, so the caller falls back to the page alone.
surface_unique_ptr compose_tile(const SDL_Surface *page,
                                const std::filesystem::path &book_path)
{
    const int w = page->w;
    const int h = page->h;
    const int half = w / 2;

    surface_unique_ptr cover = extract_cover(book_path, half, h);
    if (!cover)
    {
        return nullptr;
    }

    surface_unique_ptr tile{SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32,
                                                 page->format->Rmask, page->format->Gmask,
                                                 page->format->Bmask, page->format->Amask)};
    if (!tile)
    {
        return nullptr;
    }
    SDL_FillRect(tile.get(), nullptr, SDL_MapRGB(tile->format, 0, 0, 0));

    const double cover_zoom =
        std::min((double)half / cover->w, (double)h / cover->h);
    surface_unique_ptr cover_scaled{
        zoomSurface(cover.get(), cover_zoom, cover_zoom, SMOOTHING_ON)};
    if (cover_scaled)
    {
        SDL_Rect at{(Sint16)((half - cover_scaled->w) / 2),
                    (Sint16)((h - cover_scaled->h) / 2), 0, 0};
        SDL_BlitSurface(cover_scaled.get(), nullptr, tile.get(), &at);
    }

    // zoomSurface will not take a const surface, and only reads from it.
    surface_unique_ptr page_scaled{
        zoomSurface(const_cast<SDL_Surface *>(page), 0.5, 0.5, SMOOTHING_ON)};
    if (page_scaled)
    {
        SDL_Rect at{(Sint16)half, (Sint16)((h - page_scaled->h) / 2), 0, 0};
        SDL_BlitSurface(page_scaled.get(), nullptr, tile.get(), &at);
    }

    return tile;
}

} // namespace

uint32_t hash_rom_path(const std::string &input)
{
    std::vector<char> padded(input.size() + 8, '\0');
    std::memcpy(padded.data(), input.data(), input.size());
    return FNV1A_Pippip_Yurii(padded.data(), input.size());
}

namespace
{

bool write_tile_png(const SDL_Surface *surface, const std::string &key)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(ROM_SCREENS_DIR, ec) &&
        !std::filesystem::create_directories(ROM_SCREENS_DIR, ec))
    {
        return false;
    }

    const std::filesystem::path out =
        std::filesystem::path(ROM_SCREENS_DIR) /
        (std::to_string(hash_rom_path(key)) + ".png");
    return write_surface_png(surface, out.string());
}

} // namespace

bool write_rom_screen_plain(const SDL_Surface *surface, const std::string &rom_path)
{
    if (surface == nullptr || rom_path.empty())
    {
        return false;
    }
    return write_tile_png(surface, rom_path);
}

bool write_rom_screen(const SDL_Surface *surface, const std::filesystem::path &rom_path)
{
    if (surface == nullptr || rom_path.empty())
    {
        return false;
    }

    // Prefer the recent list, which is the exact string the switcher hashes.
    std::string key = recent_rompath();
    if (key.empty())
    {
        key = rom_path.string();
        const auto colon = key.find(':');
        if (colon != std::string::npos)
        {
            key = key.substr(colon + 1);
        }
    }

    surface_unique_ptr tile = compose_tile(surface, rom_path);
    return write_tile_png(tile ? tile.get() : surface, key);
}
