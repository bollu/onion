#include "rom_screen.h"

#include "screenshot.h"

#include "utils/hash.h"   // shared with the Game Switcher, via -I../common

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{

// Created if absent, as keymon does for RetroArch via file_open_ensure_path().
const char *ROM_SCREENS_DIR = "/mnt/SDCARD/Saves/CurrentProfile/romScreens";

// Zero-padded because FNV1A_Pippip_Yurii reads up to 8 bytes past the string.
uint32_t hash_rom_path(const std::string &input)
{
    std::vector<char> padded(input.size() + 8, '\0');
    std::memcpy(padded.data(), input.data(), input.size());
    return FNV1A_Pippip_Yurii(padded.data(), input.size());
}

} // namespace

bool write_rom_screen(const SDL_Surface *surface, const std::filesystem::path &rom_path)
{
    if (surface == nullptr || rom_path.empty())
    {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(ROM_SCREENS_DIR, ec) &&
        !std::filesystem::create_directories(ROM_SCREENS_DIR, ec))
    {
        return false;
    }

    // MainUI records a launcher-prefixed rompath as "launch:rom"; the switcher hashes
    // what follows the colon, so strip it here too or the name will not match.
    std::string key = rom_path.string();
    const auto colon = key.find(':');
    if (colon != std::string::npos)
    {
        key = key.substr(colon + 1);
    }
    const std::filesystem::path out =
        std::filesystem::path(ROM_SCREENS_DIR) /
        (std::to_string(hash_rom_path(key)) + ".png");

    return write_surface_png(surface, out.string());
}
