#ifndef ROM_SCREEN_H_
#define ROM_SCREEN_H_

#include <SDL/SDL.h>
#include <filesystem>

// Saves the current frame as the Game Switcher's preview for this book.
//
// The Game Switcher looks for Saves/CurrentProfile/romScreens/<hash>.png before it
// falls back to the box art in Imgs/, so writing one here is what makes a book show
// the page it was left on rather than its cover.
//
// `rom_path` must be the path MainUI launched with, since that is what it recorded
// in the recent list and what the switcher hashes.
//
// Returns false if it was not written, including the ordinary case of not running on
// a device with Onion's Saves layout.
bool write_rom_screen(const SDL_Surface *surface, const std::filesystem::path &rom_path);

#endif
