#include "./config.h"
#include "./font_catalog.h"
#include "./settings_store.h"
#include "./shoulder_keymap.h"
#include "./state_store.h"
#include "./system_styling.h"
#include "./color_theme_def.h"
#include "./view_stack.h"
#include "./library_index.h"
#include "./views/file_selector.h"
#include "./views/library_view.h"
#include "./views/reader_bootstrap_view.h"
#include "./views/settings_view.h"
#include "./views/token_view/token_view_styling.h"
#include "filetypes/open_doc.h"
#include "sys/keymap.h"
#include "sys/screen.h"
#include "util/fps_limiter.h"
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

namespace
{

void initialize_views(
    ViewStack &view_stack,
    StateStore &state_store,
    SystemStyling &sys_styling,
    TokenViewStyling &token_view_styling,
    TaskQueue &task_queue,
    LibraryIndex &library,
    std::optional<std::filesystem::path> requested_book_path
)
{
    auto load_book = [&view_stack, &state_store, &sys_styling, &token_view_styling, &task_queue, &library](std::filesystem::path path) {
        if (!std::filesystem::exists(path))
        {
            std::cerr << path << " does not exist" << std::endl;
            return;
        }
        if (!file_type_is_supported(path))
        {
            std::cerr << path << " filetype is not supported" << std::endl;
            return;
        }

        library.note_opened(path, 0);

        view_stack.push(
            std::make_shared<ReaderBootstrapView>(
                path,
                sys_styling,
                token_view_styling,
                view_stack,
                state_store,
                [&task_queue](task_func task){ task_queue.submit(task); }
            )
        );
    };

    if (requested_book_path)
    {
        load_book(*requested_book_path);
    }
    else
    {
        // The file browser is kept, but behind the library: it remains the only way to
        // reach a book outside the library directory.
        auto push_file_browser = [&view_stack, &state_store, &sys_styling, load_book]() {
            auto browse_path = state_store.get_current_browse_path().value_or(DEFAULT_BROWSE_PATH);
            std::shared_ptr<FileSelector> fs = std::make_shared<FileSelector>(
                browse_path,
                sys_styling
            );

            fs->set_on_file_selected(load_book);
            fs->set_on_file_focus([&state_store](std::string path) {
                state_store.set_current_browse_path(path);
            });
            fs->set_on_view_focus([&state_store]() {
                state_store.remove_current_book_path();
            });

            view_stack.push(fs);
        };

        std::shared_ptr<LibraryView> library_view = std::make_shared<LibraryView>(
            library,
            sys_styling,
            [&task_queue](task_func task){ task_queue.submit(task); }
        );
        library_view->set_on_book_selected(load_book);
        library_view->set_on_browse_requested(push_file_browser);

        view_stack.push(library_view);

        if (state_store.get_current_book_path())
        {
            load_book(state_store.get_current_book_path().value());
        }
    }
}

class SystemKeyChordTracker
{
    bool _menu_held = false;
    bool _select_held = false;

    bool _exit_on_menu_release = false;
    bool _exit_requested = false;

public:

    // Report keypress event. Return filtered key code.
    SDLKey on_keypress(SDLKey key)
    {
        // Block any other keys while special key is held
        SDLKey filtered_key = (_menu_held || _select_held) ? SDLK_UNKNOWN : key;

        if (key == SW_BTN_SELECT)
        {
            _select_held = true;
        }

        if (key == SW_BTN_MENU)
        {
            _menu_held = true;
            _exit_on_menu_release = true;
        }
        else
        {
            // Cancel app exit if a menu chord was used (e.g. change brightness)
            _exit_on_menu_release = false;
        }

        return filtered_key;
    }

    // Report keyrelease event.
    void on_keyrelease(SDLKey key)
    {
        if (key == SW_BTN_MENU)
        {
            _menu_held = false;

            if (_exit_on_menu_release)
            {
                _exit_requested = true;
            }
        }
        else if (key == SW_BTN_SELECT)
        {
            _select_held = false;
        }
    }

    bool exit_requested() const
    {
        return _exit_requested;
    }
};

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
        get_valid_theme(settings_get_color_theme(state_store).value_or(DEFAULT_COLOR_THEME)),
        get_valid_shoulder_keymap(settings_get_shoulder_keymap(state_store).value_or(DEFAULT_SHOULDER_KEYMAP))
    );
    sys_styling.subscribe_to_changes([&state_store, &sys_styling](SystemStyling::ChangeId) {
        // Persist changes
        settings_set_color_theme(state_store, sys_styling.get_color_theme());
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

    initialize_views(
        view_stack,
        state_store,
        sys_styling,
        token_view_styling,
        task_queue,
        library,
        requested_book_path
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
    SystemKeyChordTracker chord_tracker;

    auto key_held_callback = [&view_stack](SDLKey key, uint32_t held_ms) {
        view_stack.on_keyheld(key, held_ms);
    };

    // Timing
    Timer idle_timer;
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

    while (!quit)
    {
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

                        SDLKey key = chord_tracker.on_keypress(event.key.keysym.sym);

                        if (key == SW_BTN_POWER)
                        {
                            state_store.flush();
                        }
                        else
                        {
                            view_stack.on_keypress(key);

                            if (key == SW_BTN_X)
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
                case SDL_KEYUP:
                    {
                        SDLKey key = event.key.keysym.sym;
                        chord_tracker.on_keyrelease(key);
                    }
                    break;
                default:
                    break;
            }
        }

        quit = quit || chord_tracker.exit_requested();

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
