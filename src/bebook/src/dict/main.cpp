#include "./dict_config.h"
#include "./views/search_view.h"

#include "reader/color_theme_def.h"
#include "reader/config.h"
#include "reader/font_catalog.h"
#include "reader/settings_store.h"
#include "reader/shoulder_keymap.h"
#include "reader/state_store.h"
#include "reader/system_styling.h"
#include "reader/view_stack.h"
#include "reader/views/popup_view.h"

#include "lexicon/lexicon_service.h"

#include "sys/game_switcher.h"
#include "sys/keymap.h"
#include "sys/screen.h"
#include "util/fps_limiter.h"
#include "util/held_key_tracker.h"
#include "util/key_value_file.h"
#include "util/math.h"
#include "util/sdl_font_cache.h"
#include "util/timer.h"

#include <SDL/SDL.h>

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace
{
bool quit = false;
void signal_handler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM) quit = true;
}
}

int main(int, char **)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (char *w = SDL_getenv("SCREEN_WIDTH"))
    {
        int nw = atoi(w);
        if (100 < nw && nw < 4096) SCREEN_WIDTH = static_cast<unsigned int>(nw);
    }
    if (char *h = SDL_getenv("SCREEN_HEIGHT"))
    {
        int nh = atoi(h);
        if (100 < nh && nh < 4096) SCREEN_HEIGHT = static_cast<unsigned int>(nh);
    }

    SDL_Init(SDL_INIT_VIDEO);
    SDL_ShowCursor(SDL_DISABLE);

    SDL_Surface *video = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 32, SDL_HWSURFACE);
    SDL_Surface *screen = SDL_CreateRGBSurface(SDL_HWSURFACE, SCREEN_WIDTH, SCREEN_HEIGHT, 32, 0, 0, 0, 0);
    set_render_surface_format(screen->format);

    // Mirrors bewiki: the packaged app ships a bedict.cfg pointing store_path at Saves/;
    // host runs without one fall back to a cwd-relative dotfile.
    auto config = load_key_value(DICT_CONFIG_FILE_PATH);
    config.try_emplace(DICT_CONFIG_KEY_STORE_PATH, DICT_STORE_PATH);
    StateStore state_store(config[DICT_CONFIG_KEY_STORE_PATH]);

    auto init_font_name = get_valid_font_name(
        settings_get_font_name(state_store).value_or(DEFAULT_FONT_NAME));
    auto init_font_size = bound(
        settings_get_font_size(state_store).value_or(DEFAULT_FONT_SIZE),
        MIN_FONT_SIZE, MAX_FONT_SIZE);
    if (!cached_load_font(SYSTEM_FONT, init_font_size, FontLoadErrorOpt::NoThrow) ||
        !cached_load_font(init_font_name, init_font_size, FontLoadErrorOpt::NoThrow))
    {
        std::cerr << "Failed to load fonts" << std::endl;
        return 1;
    }

    SystemStyling sys_styling(
        init_font_name, init_font_size,
        get_valid_theme(settings_get_color_theme(state_store).value_or(DEFAULT_COLOR_THEME)),
        get_valid_shoulder_keymap(
            settings_get_shoulder_keymap(state_store).value_or(DEFAULT_SHOULDER_KEYMAP)));

    lexicon::LexiconService lexicon(LEXICON_DB_PATH);

    ViewStack view_stack;
    if (!lexicon.ok())
    {
        view_stack.push(std::make_shared<PopupView>(
            "Dizionario non trovato", SYSTEM_FONT, sys_styling));
    }
    else
    {
        view_stack.push(std::make_shared<SearchView>(lexicon, sys_styling, view_stack));
    }

    // B is here because it is backspace: holding it to erase a word is the whole point,
    // and nothing else in this app treats B as a one-shot.
    HeldKeyTracker held_key_tracker({
        SW_BTN_UP, SW_BTN_DOWN, SW_BTN_LEFT, SW_BTN_RIGHT,
        SW_BTN_L1, SW_BTN_R1, SW_BTN_L2, SW_BTN_R2,
        SW_BTN_B,
    });
    auto key_held_callback = [&view_stack](SDLKey key, uint32_t held_ms) {
        view_stack.on_keyheld(key, held_ms);
    };

    Timer idle_timer;
    FPSLimiter limit_fps(TARGET_FPS);
    const uint32_t avg_loop_time = 1000 / TARGET_FPS;

    view_stack.render(screen, true);
    SDL_BlitSurface(screen, NULL, video, NULL);
    SDL_Flip(video);

    while (!quit)
    {
        bool ran_user_code = false;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                quit = true;
            }
            else if (event.type == SDL_KEYDOWN)
            {
                idle_timer.reset();
                const SDLKey key = event.key.keysym.sym;
                if (key == SW_BTN_POWER)
                {
                    state_store.flush();
                }
                else if (key == SW_BTN_START)
                {
                    // START returns to the main menu from anywhere, uniform with
                    // bebook/bewiki.
                    quit = true;
                }
                else if (key == SW_BTN_MENU)
                {
                    // MENU opens the GameSwitcher: request it and quit, letting runtime.sh
                    // launch the fullscreen switcher once we exit.
                    request_game_switcher();
                    quit = true;
                }
                else
                {
                    view_stack.on_keypress(key);
                    ran_user_code = true;
                }
            }
        }

        held_key_tracker.accumulate(avg_loop_time);
        ran_user_code = held_key_tracker.for_longest_held(key_held_callback) || ran_user_code;

        if (ran_user_code)
        {
            bool force_render = view_stack.pop_completed_views();
            if (view_stack.is_done())
            {
                quit = true;
            }
            if (view_stack.render(screen, force_render))
            {
                SDL_BlitSurface(screen, NULL, video, NULL);
                SDL_Flip(video);
            }
        }

        if (!quit) limit_fps();

        if (idle_timer.elapsed_sec() >= IDLE_SAVE_TIME_SEC)
        {
            idle_timer.reset();
            state_store.flush();
        }
    }

    view_stack.shutdown();
    state_store.flush();
    SDL_FreeSurface(screen);
    SDL_Quit();
    return 0;
}
