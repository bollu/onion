#include "battery.h"

#include <cstdio>

int read_battery_percent()
{
    std::FILE *fp = std::fopen("/tmp/percBat", "r");
    if (fp == nullptr)
    {
        return -1;
    }

    int percent = -1;
    const bool read_ok = (std::fscanf(fp, "%d", &percent) == 1);
    std::fclose(fp);

    if (!read_ok || percent < 0)
    {
        return -1;
    }

    // batmon reports 500 while charging; clamp so the indicator stays visible.
    return percent > 100 ? 100 : percent;
}
