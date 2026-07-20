#include "rom_screen.h"

#include "screenshot.h"

// Onion's shared hash, via -I../common (see the Makefile).
#include "utils/hash.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{

// Where Onion keeps Game Switcher previews. Shipped as part of the Saves layout
// (static/configs/Saves/CurrentProfile/romScreens), so it is not created here: its
// absence means this is not a device with that layout, and inventing the directory
// would just leave litter. Same reasoning as LibraryIndex::write_box_art().
const char *ROM_SCREENS_DIR = "/mnt/SDCARD/Saves/CurrentProfile/romScreens";

/**
 * The hash the Game Switcher names preview files by, from Onion's shared
 * utils/hash.h -- the same function the switcher and keymon use, not a copy, so it
 * cannot drift out of step with them.
 *
 * The wrapper exists for one reason: that implementation reads up to 8 bytes past the
 * string, which its own comment warns about ("Add 8 more bytes to the buffer being
 * hashed"). Passing a std::string's buffer straight in would be an out-of-bounds
 * read, so the input is copied into a zero-padded buffer first.
 */
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
    if (!std::filesystem::is_directory(ROM_SCREENS_DIR, ec))
    {
        return false;
    }

    const std::string key = rom_path.string();
    const std::filesystem::path out =
        std::filesystem::path(ROM_SCREENS_DIR) /
        (std::to_string(hash_rom_path(key)) + ".png");

    return write_surface_png(surface, out.string());
}
