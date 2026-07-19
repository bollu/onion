#ifndef CONFIG_H_
#define CONFIG_H_

#define TARGET_FPS 20

#define IDLE_SAVE_TIME_SEC 60

#define FONT_DIR            "resources/fonts"
// Charis SIL: a text face designed for readability at low resolution, with real
// italic and bold members rather than synthesised ones. Kerning lives only in its
// GPOS table, which is why the engine shapes with HarfBuzz -- FreeType alone would
// find none at all.
#define DEFAULT_FONT_NAME   "resources/fonts/Charis-Regular.ttf"
#define SYSTEM_FONT         "resources/fonts/DejaVuSansMono.ttf"

#define MIN_FONT_SIZE      18
#define MAX_FONT_SIZE      40
#define DEFAULT_FONT_SIZE  26
#define FONT_SIZE_STEP     2

#define DIALOG_PADDING       25
#define DIALOG_BORDER_WIDTH  3

// Reading page geometry.
//
// The previous 4px side margin was the single most conspicuous thing about the reading
// page: text ran into the bezel with no breathing room. 28px on a 640px panel leaves a
// measure of ~584px, which at the default size is around 60 characters -- close to the
// 45-75 that centuries of book typography converged on.
#define TEXT_MARGIN_X   28

// Leading as a percentage of the font's natural line height. Book texts are set looser
// than the font's own default, which is tuned for UI density.
#define LINE_HEIGHT_PERCENT 125

// First-line indent, as a percentage of the font size (i.e. roughly of an em).
#define PARAGRAPH_INDENT_PERCENT 0

#define DEFAULT_JUSTIFY    true
#define DEFAULT_HYPHENATE  true

#define DEFAULT_COLOR_THEME "night_contrast"

#define CONFIG_FILE_PATH "bebook.cfg"
#define FALLBACK_STORE_PATH ".bebook_store"

#if PLATFORM_MIYOO_MINI
    #define DEFAULT_BROWSE_PATH "/mnt/SDCARD/Roms/EBOOK/"
    #define EXTRA_FONTS_LIST    {"/customer/app/wqy-microhei.ttc"}
#else
    #define DEFAULT_BROWSE_PATH std::filesystem::current_path() / ""
    #define EXTRA_FONTS_LIST    {}
#endif

#define DEFAULT_SHOW_PROGRESS true
#define DEFAULT_SHOULDER_KEYMAP "LR"

#define DEFAULT_PROGRESS_REPORTING ProgressReporting::GLOBAL_PERCENT

#endif
