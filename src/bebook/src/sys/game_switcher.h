#ifndef SYS_GAME_SWITCHER_H_
#define SYS_GAME_SWITCHER_H_

#include <fstream>

// Ask OnionOS to open the GameSwitcher. Our apps run under runtime.sh, whose loop launches
// the fullscreen GameSwitcher the moment this flag file exists and the running program has
// exited -- exactly how the stock "GameSwitcher (Shortcut)" app works (its whole body is
// `touch .runGameSwitcher`). So the caller's contract is: request it, then quit the app.
//
// keymon can't do this for us: it owns MENU only in MainUI / RetroArch modes, and its overlay
// path assumes a pausable RetroArch. Our SDL apps would fight the switcher for the framebuffer.
// Triggering the flag ourselves and exiting sidesteps all of that.
//
// On a host build the path does not exist, so the open silently no-ops -- harmless, since there
// is no runtime.sh there anyway.
inline void request_game_switcher()
{
    std::ofstream("/mnt/SDCARD/.tmp_update/.runGameSwitcher");
}

#endif
