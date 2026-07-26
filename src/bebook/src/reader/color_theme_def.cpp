#include "./color_theme_def.h"
#include "./config.h"

#include <string>
#include <utility>
#include <vector>

namespace
{

const std::vector<std::pair<std::string, ColorTheme>> theme_defs = {
    {
        "night_contrast",
        {
            {0, 0, 0, 0},       // background
            {240, 240, 240, 0}, // main text
            {96, 96, 96, 0},    // secondary text
            {150, 38, 200, 0},  // highlight background
            {240, 240, 240, 0}, // highlight text
            {28, 42, 74, 0},    // link background
            {0, 110, 130, 0},   // link highlight background
        }
    },
    {
        "light_contrast",
        {
            {255, 255, 255, 0}, // background
            {0, 0, 0, 0},       // main text
            {160, 160, 160, 0}, // secondary text
            {163, 81, 200, 0},  // highlight background
            {250, 250, 250, 0}, // highlight text
            {214, 230, 250, 0}, // link background
            {40, 140, 160, 0},  // link highlight background
        }
    },
    {
        "light_sepia",
        {
            {250, 240, 220, 0}, // background
            {0, 0, 0, 0},    // main text
            {160, 160, 160, 0}, // secondary text
            {163, 81, 200, 0},  // highlight background
            {250, 240, 220, 0}, // highlight text
            {228, 220, 196, 0}, // link background
            {120, 130, 90, 0},  // link highlight background
        }
    },
    {
        "vampire",
        {
            {0, 0, 0, 0},   // background
            {192, 0, 0, 0}, // main text
            {96, 0, 0, 0},  // secondary text
            {192, 0, 0, 0}, // highlight background
            {0, 0, 0, 0},   // highlight text
            {48, 0, 0, 0},  // link background
            {255, 60, 60, 0}, // link highlight background
        }
    },
};

int get_theme_index(const std::string &name)
{
    for (uint32_t i = 0; i < theme_defs.size(); i++)
    {
        if (theme_defs[i].first == name)
        {
            return i;
        }
    }

    return -1;
}

} // namespace

const ColorTheme& get_color_theme(const std::string &name)
{
    int i = get_theme_index(name);
    if (i < 0)
    {
        i = 0;
    }

    return theme_defs[i].second;
}

std::string get_valid_theme(const std::string &preferred)
{
    int i = get_theme_index(preferred);
    if (i < 0)
    {
        i = get_theme_index(DEFAULT_COLOR_THEME);
    }
    if (i < 0)
    {
        i = 0;
    }
    return theme_defs[i].first;
}

std::string get_prev_theme(const std::string &name)
{
    int i = get_theme_index(name);
    if (i < 0)
    {
        return theme_defs[0].first;
    }

    return theme_defs[
        (i - 1 + theme_defs.size()) % theme_defs.size()
    ].first;
}

std::string get_next_theme(const std::string &name)
{
    int i = get_theme_index(name);
    if (i < 0)
    {
        return theme_defs[0].first;
    }

    return theme_defs[
        (i + 1) % theme_defs.size()
    ].first;
}
