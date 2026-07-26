#ifndef SYS_GAME_SWITCHER_H_
#define SYS_GAME_SWITCHER_H_

#include <fstream>
#include <unistd.h>

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

// Leave for the switcher immediately.
//
// Nothing in this path polls: runtime.sh runs the app synchronously and calls check_switcher
// the instant it returns, so every millisecond between the button and the switcher is work we
// chose to do. Both things on this device that feel instant avoid that work rather than
// hurrying it -- keymon SIGKILLs MainUI (system_utils.h, kill_mainUI), and for a game it
// paints a full-screen image with `bootScreen &` and lets RetroArch's much slower shutdown
// happen behind it.
//
// So this writes the flag and stops. `_exit` skips atexit handlers, static destructors and
// SDL/libxml teardown that returning from main would run; the kernel reclaims all of it, and
// MainUI being killed outright is the standing proof that the platform tolerates it.
//
// What must NOT be skipped is state the reader would miss -- the reading position above all.
// Flushing is the caller's job, done before calling this, because only the caller knows what
// is worth the milliseconds.
inline void quit_to_game_switcher()
{
    request_game_switcher();
    _exit(0);
}

#endif
