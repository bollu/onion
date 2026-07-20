#include "rom_screen.h"

#include "screenshot.h"

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
 * The hash the Game Switcher names preview files by.
 *
 * Ported from src/common/utils/hash.h -- bebook builds on its own and shares no
 * headers with the rest of the tree. It must stay bit-identical to that version or
 * the switcher simply will not find what is written here.
 *
 * That implementation reads up to 8 bytes past the end of the string, which its own
 * comment calls out ("Add 8 more bytes to the buffer being hashed"). Rather than
 * reproduce that hazard, the input is copied into a zero-padded buffer first: the
 * padding bytes are never mixed in for lengths over 8, and for shorter ones the
 * original masks them off with _PADr_KAZE, so the result is unchanged either way.
 */
uint32_t fnv1a_pippip_yurii(const std::string &input)
{
    const uint32_t PRIME = 591798841;

    std::vector<char> buffer(input.size() + 8, '\0');
    std::memcpy(buffer.data(), input.data(), input.size());

    const char *str = buffer.data();
    const size_t wrdlen = input.size();

    uint64_t hash64 = 14695981039346656037ULL;

    if (wrdlen > 8)
    {
        size_t cycles = ((wrdlen - 1) >> 4) + 1;
        size_t nd_head = wrdlen - (cycles << 3);

        for (; cycles--; str += 8)
        {
            uint64_t chunk = 0;
            std::memcpy(&chunk, str, sizeof(chunk));
            hash64 = (hash64 ^ chunk) * PRIME;

            std::memcpy(&chunk, str + nd_head, sizeof(chunk));
            hash64 = (hash64 ^ chunk) * PRIME;
        }
    }
    else
    {
        uint64_t chunk = 0;
        std::memcpy(&chunk, str, sizeof(chunk));
        // _PADr_KAZE: keep only the bytes the string actually occupies.
        const unsigned shift = static_cast<unsigned>((8 - wrdlen) << 3);
        chunk = (chunk << shift) >> shift;
        hash64 = (hash64 ^ chunk) * PRIME;
    }

    uint32_t hash32 = static_cast<uint32_t>(hash64 ^ (hash64 >> 32));
    return hash32 ^ (hash32 >> 16);
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
        (std::to_string(fnv1a_pippip_yurii(key)) + ".png");

    return write_surface_png(surface, out.string());
}
