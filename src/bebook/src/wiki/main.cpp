#include "./wiki_config.h"
#include "./wiki_context.h"
#include "./views/article_view.h"

#include "reader/color_theme_def.h"
#include "reader/config.h"
#include "reader/font_catalog.h"
#include "reader/settings_store.h"
#include "reader/shoulder_keymap.h"
#include "reader/state_store.h"
#include "reader/system_styling.h"
#include "reader/view_stack.h"
#include "reader/views/popup_view.h"
#include "reader/views/settings_view.h"
#include "reader/views/token_view/token_view_styling.h"

#include "sys/keymap.h"
#include "sys/screen.h"
#include "util/fps_limiter.h"
#include "util/held_key_tracker.h"
#include "util/key_value_file.h"
#include "util/math.h"
#include "util/screenshot.h"
#include "util/sdl_font_cache.h"
#include "util/timer.h"

#include <libxml/parser.h>
#include <SDL/SDL.h>

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

namespace
{

bool quit = false;

void signal_handler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        quit = true;
    }
}

const char *STORE_KEY_LAST_PATH = "wiki_last_path";
const char *STORE_KEY_LAST_ADDRESS = "wiki_last_address";

std::unordered_map<std::string, std::string> load_config_with_defaults()
{
    auto config = load_key_value(WIKI_CONFIG_FILE_PATH);
    config.try_emplace(WIKI_CONFIG_KEY_STORE_PATH, WIKI_STORE_PATH);
    config.try_emplace(WIKI_CONFIG_KEY_ZIM_PATH, WIKI_DEFAULT_ZIM_PATH);
    return config;
}

bool ends_with(const std::string &s, const std::string &suffix)
{
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}

int main(int argc, char **argv)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (char *env_screen_width = SDL_getenv("SCREEN_WIDTH"))
    {
        int new_width = atoi(env_screen_width);
        if (100 < new_width && new_width < 4096)
        {
            SCREEN_WIDTH = static_cast<unsigned int>(new_width);
        }
    }

    if (char *env_screen_height = SDL_getenv("SCREEN_HEIGHT"))
    {
        int new_height = atoi(env_screen_height);
        if (100 < new_height && new_height < 4096)
        {
            SCREEN_HEIGHT = static_cast<unsigned int>(new_height);
        }
    }

    SDL_Init(SDL_INIT_VIDEO);
    SDL_ShowCursor(SDL_DISABLE);

    SDL_Surface *video = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 32, SDL_HWSURFACE);
    SDL_Surface *screen = SDL_CreateRGBSurface(SDL_HWSURFACE, SCREEN_WIDTH, SCREEN_HEIGHT, 32, 0, 0, 0, 0);
    set_render_surface_format(screen->format);

    auto config = load_config_with_defaults();
    StateStore state_store(config[WIKI_CONFIG_KEY_STORE_PATH]);

    auto init_font_name = get_valid_font_name(
        settings_get_font_name(state_store).value_or(DEFAULT_FONT_NAME));
    auto init_font_size = bound(
        settings_get_font_size(state_store).value_or(DEFAULT_FONT_SIZE),
        MIN_FONT_SIZE, MAX_FONT_SIZE);
    if (!cached_load_font(SYSTEM_FONT, init_font_size, FontLoadErrorOpt::NoThrow) ||
        !cached_load_font(init_font_name, init_font_size, FontLoadErrorOpt::NoThrow))
    {
        std::cerr << "Failed to load one or more fonts" << std::endl;
        return 1;
    }

    SystemStyling sys_styling(
        init_font_name,
        init_font_size,
        get_valid_theme(settings_get_color_theme(state_store).value_or(DEFAULT_COLOR_THEME)),
        get_valid_shoulder_keymap(
            settings_get_shoulder_keymap(state_store).value_or(DEFAULT_SHOULDER_KEYMAP)));
    sys_styling.subscribe_to_changes([&state_store, &sys_styling](SystemStyling::ChangeId) {
        settings_set_color_theme(state_store, sys_styling.get_color_theme());
        settings_set_font_name(state_store, sys_styling.get_font_name());
        settings_set_font_size(state_store, sys_styling.get_font_size());
        settings_set_shoulder_keymap(state_store, sys_styling.get_shoulder_keymap());
    });

    TokenViewStyling token_view_styling(
        settings_get_show_title_bar(state_store).value_or(DEFAULT_SHOW_PROGRESS),
        settings_get_progress_reporting(state_store).value_or(DEFAULT_PROGRESS_REPORTING),
        settings_get_justify(state_store).value_or(DEFAULT_JUSTIFY),
        settings_get_hyphenate(state_store).value_or(DEFAULT_HYPHENATE));
    token_view_styling.subscribe_to_changes([&token_view_styling, &state_store]() {
        settings_set_show_title_bar(state_store, token_view_styling.get_show_title_bar());
        settings_set_progress_reporting(state_store, token_view_styling.get_progress_reporting());
        settings_set_justify(state_store, token_view_styling.get_justify());
        settings_set_hyphenate(state_store, token_view_styling.get_hyphenate());
    });

    ViewStack view_stack;

    // argv[1] may be a .zim to open, or anything else to ignore. The Game Switcher will
    // later resume by handing back a stub path; see stage 6.
    std::string zim_path = config[WIKI_CONFIG_KEY_ZIM_PATH];
    if (argc >= 2 && ends_with(argv[1], ".zim"))
    {
        zim_path = argv[1];
    }

    // Optional second argument names an article to open, for development.
    const std::string requested_article = argc >= 3 ? argv[2] : "";

    auto context = std::make_shared<WikiContext>();
    std::shared_ptr<ArticleView> article_view;

    if (!context->open(zim_path))
    {
        view_stack.push(std::make_shared<PopupView>(
            context->error().empty() ? ("Nessun archivio: " + zim_path) : context->error(),
            SYSTEM_FONT, sys_styling));
    }
    else
    {
        // Resume where we left off, else the archive's own landing page. Kept as two
        // settings rather than via set_book_address, which is keyed per document and
        // would leave one stored entry per article ever opened.
        std::string start_path = requested_article;
        DocAddr start_address = 0;

        if (start_path.empty())
        {
            start_path = state_store.get_setting(STORE_KEY_LAST_PATH).value_or("");
            if (!start_path.empty())
            {
                const auto saved = state_store.get_setting(STORE_KEY_LAST_ADDRESS);
                if (saved)
                {
                    start_address = std::strtoull(saved->c_str(), nullptr, 10);
                }
            }
        }

        // The archive's landing page is a link portal built from tables, so the cleaner
        // empties it. Only use it if something survives; otherwise say so rather than
        // opening a blank page. The reading list will fill this gap.
        if (start_path.empty())
        {
            const std::string main_page = context->main_page_path();
            if (!main_page.empty())
            {
                auto probe = context->open_article(main_page);
                if (probe != nullptr && probe->has_content())
                {
                    start_path = main_page;
                }
            }
        }

        if (!start_path.empty())
        {
            article_view = std::make_shared<ArticleView>(
                context, start_path, start_address, sys_styling, token_view_styling, view_stack);
        }

        if (article_view && !article_view->current_path().empty())
        {
            article_view->set_on_change([&state_store](const std::string &path, DocAddr at) {
                state_store.set_setting(STORE_KEY_LAST_PATH, path);
                state_store.set_setting(STORE_KEY_LAST_ADDRESS, std::to_string(at));
            });
            view_stack.push(article_view);
        }
        else
        {
            view_stack.push(std::make_shared<PopupView>(
                "Nessuna voce da aprire", SYSTEM_FONT, sys_styling));
        }
    }

    quit = view_stack.is_done();

    std::shared_ptr<SettingsView> settings_view =
        std::make_shared<SettingsView>(sys_styling, token_view_styling, SYSTEM_FONT);

    HeldKeyTracker held_key_tracker({
        SW_BTN_UP, SW_BTN_DOWN, SW_BTN_LEFT, SW_BTN_RIGHT,
        SW_BTN_L1, SW_BTN_R1, SW_BTN_L2, SW_BTN_R2,
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

    const char *screenshot_path = SDL_getenv("BEWIKI_SCREENSHOT");
    uint32_t screenshot_frames_left = screenshot_path ? 40 : 0;

    while (!quit)
    {
        if (screenshot_path && screenshot_frames_left-- == 0)
        {
            if (const char *pages = SDL_getenv("BEWIKI_SCREENSHOT_PAGES"))
            {
                for (int i = atoi(pages); i > 0; --i)
                {
                    view_stack.on_keypress(SW_BTN_RIGHT);
                }
            }
            view_stack.render(screen, true);
            write_surface_png(screen, screenshot_path);
            break;
        }

        bool ran_user_code = false;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_QUIT:
                    quit = true;
                    break;
                case SDL_KEYDOWN:
                {
                    idle_timer.reset();
                    SDLKey key = event.key.keysym.sym;

                    if (key == SW_BTN_POWER)
                    {
                        state_store.flush();
                    }
                    else
                    {
                        // No global X hook here: X is the dictionary once word select is
                        // active, so settings live in the SELECT menu instead.
                        view_stack.on_keypress(key);
                        ran_user_code = true;
                    }
                }
                break;
                default:
                    break;
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

        if (!quit)
        {
            limit_fps();
        }

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
    xmlCleanupParser();

    return 0;
}
