#ifndef ROM_SCREEN_H_
#define ROM_SCREEN_H_

#include <SDL/SDL.h>
#include <filesystem>

// Saves the frame as this book's Game Switcher preview. rom_path must be what
// MainUI launched with: that is the string the switcher hashes. False if not written.
bool write_rom_screen(const SDL_Surface *surface, const std::filesystem::path &rom_path);

#endif
