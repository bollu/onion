#ifndef ROM_SCREEN_H_
#define ROM_SCREEN_H_

#include <SDL/SDL.h>
#include <filesystem>

// Saves this book's Game Switcher tile: its cover beside the page just read, or the
// page alone when the book has no cover. False if not written.
bool write_rom_screen(const SDL_Surface *surface, const std::filesystem::path &rom_path);

#endif
