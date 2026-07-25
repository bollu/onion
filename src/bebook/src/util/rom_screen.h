#ifndef ROM_SCREEN_H_
#define ROM_SCREEN_H_

#include <SDL/SDL.h>
#include <filesystem>

// Saves this book's Game Switcher tile: its cover beside the page just read, or the
// page alone when the book has no cover. False if not written.
bool write_rom_screen(const SDL_Surface *surface, const std::filesystem::path &rom_path);

// The surface alone, keyed by `rom_path` exactly as given. For callers that write their
// own recent-list entry and so already know the spelling, and that have no cover to
// compose -- attempting one would mean a pointless archive open per navigation.
bool write_rom_screen_plain(const SDL_Surface *surface, const std::string &rom_path);

// How the Game Switcher names a tile: FNV1A_Pippip_Yurii over the rompath, zero-padded
// because it reads up to 8 bytes past the string.
uint32_t hash_rom_path(const std::string &input);

#endif
