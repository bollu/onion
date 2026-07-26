#include "./config.h"
#include "./font_catalog.h"
#include "./settings_store.h"
#include "./shoulder_keymap.h"
#include "./state_store.h"
#include "./system_styling.h"
#include "./color_theme_def.h"
#include "./view_stack.h"
#include "./library_index.h"
#include "./views/popup_view.h"
#include "./views/reader_bootstrap_view.h"
#include "./views/settings_view.h"
#include "./views/token_view/token_view_styling.h"
#include "filetypes/open_doc.h"
#include "sys/game_switcher.h"
#include "sys/keymap.h"
#include "sys/screen.h"
#include "util/budget.h"
#include "util/fps_limiter.h"
#include "util/job_runner.h"
#include "util/held_key_tracker.h"
#include "util/key_value_file.h"
#include "util/math.h"
#include "util/rom_screen.h"
#include "util/screenshot.h"
#include "util/sdl_font_cache.h"
#include "util/task_queue.h"
#include "util/timer.h"

#include <libxml/parser.h>
#include <SDL/SDL.h>

#include <csignal>
#include <iostream>
#include <set>

namespace
{

// The book being read and how far into it, recorded into the library at shutdown.
// Tracked here rather than from the launch argument so a resumed book
// records progress too.
struct OpenedBook
{
    std::filesystem::path path;
    int progress_percent = -1;   // -1 until the reader reports one
};

void initialize_views(
    ViewStack &view_stack,
    StateStore &state_store,
    SystemStyling &sys_styling,
    TokenViewStyling &token_view_styling,
    TaskQueue &task_queue,
    LibraryIndex &library,
    std::optional<std::filesystem::path> requested_book_path,
    // Owned by main(), which records it into the library at shutdown.
    OpenedBook &opened
)
{
    auto load_book = [&view_stack, &state_store, &sys_styling, &token_view_styling, &task_queue, &library, &opened](std::filesystem::path path) {
        // Say so on screen: with no shelf to fall back to, a bare return is a black flash.
        if (!std::filesystem::exists(path))
        {
            std::cerr << path << " does not exist" << std::endl;
            view_stack.push(std::make_shared<PopupView>("File not found", SYSTEM_FONT, sys_styling));
            return;
        }
        if (!file_type_is_supported(path))
        {
            std::cerr << path << " filetype is not supported" << std::endl;
            view_stack.push(std::make_shared<PopupView>("Unsupported file", SYSTEM_FONT, sys_styling));
            return;
        }

        library.note_opened(path, 0);
        opened.path = path;

        view_stack.push(
            std::make_shared<ReaderBootstrapView>(
                path,
                sys_styling,
                token_view_styling,
                view_stack,
                state_store,
                [&task_queue](task_func task){ task_queue.submit(task); },
                [&opened](int percent){ opened.progress_percent = percent; }
            )
        );
    };

    // Books are opened as games: MainUI passes the path. Without one, resume the last
    // book so the app is never a dead end.
    if (requested_book_path)
    {
        load_book(*requested_book_path);
    }
    else if (state_store.get_current_book_path())
    {
        load_book(*state_store.get_current_book_path());
    }
    else
    {
        view_stack.push(std::make_shared<PopupView>("No book selected", SYSTEM_FONT, sys_styling));
    }
}

// MENU opens the GameSwitcher and START returns to the main menu, both handled in the event
// loop below. keymon does not open the GameSwitcher for our apps (they run in MODE_APPS, where
// its menu handler is a no-op), so bebook requests it itself via request_game_switcher() and
// quits, letting runtime.sh launch the fullscreen switcher -- see sys/game_switcher.h.

bool quit = false;

void signal_handler(int)
{
    quit = true;
}

const char *CONFIG_KEY_STORE_PATH = "store_path";
// Where the library lives. Separate from the browse path so the two can differ:
// on device the library is Roms/EBOOK, but the file browser can still go anywhere.
const char *CONFIG_KEY_BOOKS_PATH = "books_path";

std::unordered_map<std::string, std::string> load_config_with_defaults()
{
    auto config = load_key_value(CONFIG_FILE_PATH);
    config.try_emplace(CONFIG_KEY_STORE_PATH, FALLBACK_STORE_PATH);
    return config;
}

} // namespace

int main(int argc, char **argv)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (char* env_screen_width = SDL_getenv("SCREEN_WIDTH")) {
        int new_width = atoi(env_screen_width);
        if (100 < new_width && new_width < 4096)
            SCREEN_WIDTH = static_cast<unsigned int>(new_width);
    }

    if (char* env_screen_height = SDL_getenv("SCREEN_HEIGHT")) {
        int new_height = atoi(env_screen_height);
        if (100 < new_height && new_height < 4096)
            SCREEN_HEIGHT = static_cast<unsigned int>(new_height);
    }

    std::cout << "Screen Size: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT << std::endl;

    // SDL Init
    SDL_Init(SDL_INIT_VIDEO);
    SDL_ShowCursor(SDL_DISABLE);

    // Surfaces
    SDL_Surface *video = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 32, SDL_HWSURFACE);
    SDL_Surface *screen = SDL_CreateRGBSurface(SDL_HWSURFACE, SCREEN_WIDTH, SCREEN_HEIGHT, 32, 0, 0, 0, 0);
    set_render_surface_format(screen->format);

    auto config = load_config_with_defaults();
    StateStore state_store(config[CONFIG_KEY_STORE_PATH]);

    // Preload & check fonts
    auto init_font_name = get_valid_font_name(settings_get_font_name(state_store).value_or(DEFAULT_FONT_NAME));
    auto init_font_size = bound(settings_get_font_size(state_store).value_or(DEFAULT_FONT_SIZE), MIN_FONT_SIZE, MAX_FONT_SIZE);
    if (
        !cached_load_font(SYSTEM_FONT, init_font_size, FontLoadErrorOpt::NoThrow) ||
        !cached_load_font(init_font_name, init_font_size, FontLoadErrorOpt::NoThrow)
    )
    {
        std::cerr << "Failed to load one or more fonts" << std::endl;
        return 1;
    }

    // System styling
    SystemStyling sys_styling(
        init_font_name,
        init_font_size,
        // Hardcoded, not read from settings: the theme is no longer toggleable, so a value
        // persisted by an older build must not resurrect a dark page.
        DEFAULT_COLOR_THEME,
        get_valid_shoulder_keymap(settings_get_shoulder_keymap(state_store).value_or(DEFAULT_SHOULDER_KEYMAP))
    );
    sys_styling.subscribe_to_changes([&state_store, &sys_styling](SystemStyling::ChangeId) {
        // Persist changes
        settings_set_font_name(state_store, sys_styling.get_font_name());
        settings_set_font_size(state_store, sys_styling.get_font_size());
        settings_set_shoulder_keymap(state_store, sys_styling.get_shoulder_keymap());
    });

    // Text Styling
    TokenViewStyling token_view_styling(
        settings_get_show_title_bar(state_store).value_or(DEFAULT_SHOW_PROGRESS),
        settings_get_progress_reporting(state_store).value_or(DEFAULT_PROGRESS_REPORTING),
        settings_get_justify(state_store).value_or(DEFAULT_JUSTIFY),
        settings_get_hyphenate(state_store).value_or(DEFAULT_HYPHENATE)
    );
    token_view_styling.subscribe_to_changes([&token_view_styling, &state_store]() {
        // Persist changes
        settings_set_show_title_bar(state_store, token_view_styling.get_show_title_bar());
        settings_set_progress_reporting(state_store, token_view_styling.get_progress_reporting());
        settings_set_justify(state_store, token_view_styling.get_justify());
        settings_set_hyphenate(state_store, token_view_styling.get_hyphenate());
    });

    // Setup views
    TaskQueue task_queue;
    ViewStack view_stack;

    std::optional<std::filesystem::path> requested_book_path = (
        argc == 2 ? std::optional<std::filesystem::path>(argv[1]) : std::nullopt
    );
    auto books_path = config.count(CONFIG_KEY_BOOKS_PATH)
        ? std::filesystem::path(config[CONFIG_KEY_BOOKS_PATH])
        : std::filesystem::path(DEFAULT_BROWSE_PATH);
    LibraryIndex library(state_store, books_path);

    OpenedBook opened;

    initialize_views(
        view_stack,
        state_store,
        sys_styling,
        token_view_styling,
        task_queue,
        library,
        requested_book_path,
        opened
    );
    quit = view_stack.is_done();

    std::shared_ptr<SettingsView> settings_view = std::make_shared<SettingsView>(
        sys_styling,
        token_view_styling,
        SYSTEM_FONT
    );

    // Track held keys
    HeldKeyTracker held_key_tracker(
        {
            SW_BTN_UP,
            SW_BTN_DOWN,
            SW_BTN_LEFT,
            SW_BTN_RIGHT,
            SW_BTN_L1,
            SW_BTN_R1,
            SW_BTN_L2,
            SW_BTN_R2
        }
    );

    auto key_held_callback = [&view_stack](SDLKey key, uint32_t held_ms) {
        view_stack.on_keyheld(key, held_ms);
    };

    // Timing
    Timer idle_timer;
    // Background work runs in the slack the frame limiter would otherwise sleep away, so
    // the frame rate is unchanged by construction: the deadline handed to the runner is the
    // one limit_fps was already going to enforce.
    JobRunner jobs;

    FPSLimiter limit_fps(TARGET_FPS);
    const uint32_t avg_loop_time = 1000 / TARGET_FPS;

    // Initial render
    view_stack.render(screen, true);
    SDL_BlitSurface(screen, NULL, video, NULL);
    SDL_Flip(video);

    // Development hook: BEBOOK_SCREENSHOT=<path> renders for a moment, writes the frame
    // out as a PNG and exits. Lets the reader be inspected headlessly
    // (SDL_VIDEODRIVER=dummy) and gives the typography work a way to diff pages.
    const char *screenshot_path = SDL_getenv("BEBOOK_SCREENSHOT");
    uint32_t screenshot_frames_left = screenshot_path ? 40 : 0;

    // Frames between cover-indexing steps, so the hitch is spread out.
    static const int COVER_INDEX_INTERVAL = 30;
    int frames_since_index = 0;
    std::set<std::filesystem::path> box_art_attempted;

    while (!quit)
    {
        const uint32_t frame_start = SDL_GetTicks();
        if (screenshot_path && screenshot_frames_left-- == 0)
        {
            task_queue.drain();  // let any async book load finish first

            // BEBOOK_SCREENSHOT_PAGES=N pages forward before capturing, so a page of
            // body text can be diffed rather than whatever the book opens on (usually
            // the cover).
            if (const char *pages = SDL_getenv("BEBOOK_SCREENSHOT_PAGES"))
            {
                for (int i = atoi(pages); i > 0; --i)
                {
                    view_stack.on_keypress(SW_BTN_RIGHT);
                    task_queue.drain();
                }
            }

            view_stack.render(screen, true);
            if (!write_surface_png(screen, screenshot_path))
            {
                std::cerr << "Failed to write " << screenshot_path << std::endl;
            }
            break;
        }

        // Cover art for books never opened. index_one() opens a zip and drain() runs it
        // on this thread, so one book every COVER_INDEX_INTERVAL frames rather than the
        // whole library at once -- the shelf paced it the same way before it went.
        if (++frames_since_index >= COVER_INDEX_INTERVAL)
        {
            frames_since_index = 0;
            const auto stale = library.stale_paths();
            if (!stale.empty())
            {
                const auto path = stale.front();
                task_queue.submit([&library, path]() { library.index_one(path); });
            }
            else
            {
                // Books already in the index are never stale, so a library indexed by a
                // build without box art would stay coverless. Attempt each once per run:
                // a book with no cover inside would otherwise be retried forever.
                for (const auto &path: library.paths_missing_box_art())
                {
                    if (box_art_attempted.insert(path).second)
                    {
                        task_queue.submit([&library, path]() { library.ensure_box_art(path); });
                        break;
                    }
                }
            }
        }

        bool ran_user_code = task_queue.drain();

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
                        else if (key == SW_BTN_START)
                        {
                            // START returns to the main menu from anywhere in the app,
                            // handled here (not per-view) so it is uniform across
                            // bebook/bewiki/bedict and works even over a modal/menu.
                            quit = true;
                        }
                        else if (key == SW_BTN_MENU)
                        {
                            // MENU opens the GameSwitcher: request it and quit, letting
                            // runtime.sh launch the fullscreen switcher once we exit. (An app
                            // cannot overlay the switcher without fighting the framebuffer.)
                            request_game_switcher();
                            quit = true;
                        }
                        else
                        {
                            view_stack.on_keypress(key);

                            // SELECT opens settings; X is the chapter picker, which is
                            // reached far more often while actually reading and so takes
                            // the button that is easier to hit.
                            if (key == SW_BTN_SELECT)
                            {
                                if (view_stack.top_view() != settings_view)
                                {
                                    settings_view->unterminate();
                                    view_stack.push(settings_view);
                                }
                                else
                                {
                                    settings_view->terminate();
                                }
                            }

                            ran_user_code = true;
                        }
                    }
                    break;
                default:
                    break;
            }
        }

        held_key_tracker.accumulate(avg_loop_time); // Pretend perfect loop timing for event firing consistency
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
            // A few milliseconds are held back for the blit and flip that follow a frame
            // whose work overran; when it did not, this is time that was going to be slept.
            jobs.run(Budget::until(frame_start + avg_loop_time - 4));

            limit_fps();
        }

        if (idle_timer.elapsed_sec() >= IDLE_SAVE_TIME_SEC)
        {
            // Make sure state is saved in case device auto-powers down. Don't seem
            // to get a signal on miyoo mini when this happens.
            state_store.flush();
            idle_timer.reset();
        }
    }

    view_stack.shutdown();

    // Once here rather than per page turn: note_opened() scans every entry to stamp this one.
    if (opened.progress_percent >= 0)
    {
        library.note_opened(opened.path, opened.progress_percent);
    }
    library.flush();
    state_store.flush();

    // Leave the page behind as this book's Game Switcher preview, so Recents shows
    // where the reader actually is rather than the cover. Only meaningful when
    // launched with a book path: that is what MainUI recorded in the recent list and
    // what the switcher hashes to find this file. `screen` still holds the last
    // rendered frame.
    if (requested_book_path)
    {
        write_rom_screen(screen, *requested_book_path);
    }

    SDL_FreeSurface(screen);
    SDL_Quit();
    xmlCleanupParser();
    
    return 0;
}
