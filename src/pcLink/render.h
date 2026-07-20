#ifndef PCLINK_RENDER_H__
#define PCLINK_RENDER_H__

#include <stdio.h>

#include "system/battery.h"
#include "system/lang.h"
#include "theme/background.h"
#include "theme/theme.h"

#include "./sharing.h"

// Layout is scaled by g_scale so the same numbers work at any device resolution.
// Font sizes mislead: TITLE 36, HINT 40, LIST 24, so body text uses LIST.
#define LINE_HEIGHT 34.0
#define BODY_LEFT 60.0

// Clears first: theme backgrounds are RGBA, so blitting alone leaves ghosts.
static void _renderBackground(SDL_Surface *screen)
{
    SDL_FillRect(screen, NULL, 0);
    SDL_BlitSurface(theme_background(), NULL, screen, NULL);
}

static void _text(SDL_Surface *screen, ThemeFonts font, const char *str,
                  SDL_Color color, int x, int y)
{
    if (str == NULL || strlen(str) == 0)
        return;

    SDL_Surface *text = TTF_RenderUTF8_Blended(resource_getFont(font), str, color);
    if (text == NULL)
        return;

    SDL_Rect pos = {x, y};
    SDL_BlitSurface(text, NULL, screen, &pos);
    SDL_FreeSurface(text);
}

static void _textCentered(SDL_Surface *screen, ThemeFonts font, const char *str,
                          SDL_Color color, int y)
{
    if (str == NULL || strlen(str) == 0)
        return;

    SDL_Surface *text = TTF_RenderUTF8_Blended(resource_getFont(font), str, color);
    if (text == NULL)
        return;

    SDL_Rect pos = {(screen->w - text->w) / 2, y};
    SDL_BlitSurface(text, NULL, screen, &pos);
    SDL_FreeSurface(text);
}

static void _chrome(SDL_Surface *screen, const char *title, const char *btn_a)
{
    theme_renderHeader(screen, title, false);
    theme_renderFooter(screen);
    theme_renderStandardHint(screen, btn_a,
                             lang_get(LANG_BACK, LANG_FALLBACK_BACK));
    theme_renderHeaderBattery(screen, battery_getPercentage());
}

// Idle: explain what this does before touching the user's WiFi.
void render_idle(SDL_Surface *screen)
{
    _renderBackground(screen);

    SDL_Color fg = theme()->list.color;
    SDL_Color dim = theme()->hint.color;
    int y = (int)(120.0 * g_scale);

    _textCentered(screen, TITLE, "Share SD card with a computer", fg, y);
    y += (int)(60.0 * g_scale);

    _textCentered(screen, LIST, "Turns this device into its own Wi-Fi network,", dim, y);
    y += (int)(LINE_HEIGHT * g_scale);
    _textCentered(screen, LIST, "then serves the SD card over it.", dim, y);
    y += (int)(LINE_HEIGHT * 1.6 * g_scale);

    _textCentered(screen, LIST, "No router or internet needed.", dim, y);
    y += (int)(LINE_HEIGHT * g_scale);
    // Kept short deliberately: at LIST size this line already spans most of a
    // 640px screen, and themes are free to ship a wider font.
    _textCentered(screen, LIST, "Your computer will lose internet.", dim, y);

    _chrome(screen, "PCLink", "Start sharing");
}

// Starting: per-step progress, because hotspot bring-up takes seconds.
void render_starting(SDL_Surface *screen, SharingStep step)
{
    _renderBackground(screen);

    SDL_Color fg = theme()->list.color;
    SDL_Color dim = theme()->hint.color;

    _textCentered(screen, TITLE, "Setting up...", fg, (int)(160.0 * g_scale));
    _textCentered(screen, LIST, sharing_stepLabel(step), dim, (int)(230.0 * g_scale));

    char progress[32];
    snprintf(progress, sizeof(progress), "Step %d of %d", (int)step + 1, STEP_COUNT);
    _textCentered(screen, LIST, progress, dim, (int)(270.0 * g_scale));

    _chrome(screen, "PCLink", "");
}

// Sharing: the screen the user actually reads, so it is the joining instructions.
void render_sharing(SDL_Surface *screen, const char *ssid, const char *ap_pass,
                    const char *address)
{
    _renderBackground(screen);

    SDL_Color fg = theme()->list.color;
    SDL_Color dim = theme()->hint.color;

    int x = (int)(BODY_LEFT * g_scale);
    int y = (int)(80.0 * g_scale);
    char line[STR_MAX];

    _text(screen, LIST, "1.  Join this Wi-Fi network:", fg, x, y);
    y += (int)(LINE_HEIGHT * g_scale);

    snprintf(line, sizeof(line), "network:  %s", ssid);
    _text(screen, LIST, line, dim, x + (int)(30.0 * g_scale), y);
    y += (int)(LINE_HEIGHT * g_scale);

    snprintf(line, sizeof(line), "password: %s", ap_pass);
    _text(screen, LIST, line, dim, x + (int)(30.0 * g_scale), y);
    y += (int)(LINE_HEIGHT * 1.5 * g_scale);

    _text(screen, LIST, "2.  Open this address:", fg, x, y);
    y += (int)(LINE_HEIGHT * g_scale);

    snprintf(line, sizeof(line), "smb://%s", address);
    _text(screen, LIST, line, dim, x + (int)(30.0 * g_scale), y);
    y += (int)(LINE_HEIGHT * g_scale);

    snprintf(line, sizeof(line), "user: %s    password: %s", SMB_USER, SMB_PASS);
    _text(screen, LIST, line, dim, x + (int)(30.0 * g_scale), y);
    y += (int)(LINE_HEIGHT * 1.5 * g_scale);

    // Live status, so a dropped hotspot is visible rather than silent. Kept to one
    // short phrase: three fields joined on one line overflow the screen width.
    if (!sharing_hotspotRunning())
        snprintf(line, sizeof(line), "Hotspot stopped");
    else if (!sharing_smbdRunning())
        snprintf(line, sizeof(line), "File server stopped");
    else {
        int clients = sharing_clientCount();
        snprintf(line, sizeof(line), "Ready  -  %d computer%s connected", clients,
                 clients == 1 ? "" : "s");
    }
    _text(screen, LIST, line, fg, x, y);

    _chrome(screen, "PCLink", "Stop sharing");
}

// Stopping: shown while WiFi is being restored, which is not instant.
void render_stopping(SDL_Surface *screen)
{
    _renderBackground(screen);

    _textCentered(screen, TITLE, "Stopping...", theme()->list.color,
                  (int)(180.0 * g_scale));
    _textCentered(screen, LIST, "Restoring your Wi-Fi connection.",
                  theme()->hint.color, (int)(250.0 * g_scale));

    _chrome(screen, "PCLink", "");
}

// Error: name the step that failed and where to look.
void render_error(SDL_Surface *screen, SharingStep step)
{
    _renderBackground(screen);

    SDL_Color fg = theme()->list.color;
    SDL_Color dim = theme()->hint.color;
    char line[STR_MAX];

    _textCentered(screen, TITLE, "Couldn't start sharing", fg, (int)(140.0 * g_scale));

    snprintf(line, sizeof(line), "Failed at: %s", sharing_stepLabel(step));
    _textCentered(screen, LIST, line, dim, (int)(210.0 * g_scale));

    _textCentered(screen, LIST, "Wi-Fi has been restored.", dim,
                  (int)(250.0 * g_scale));
    _textCentered(screen, LIST, "Check .tmp_update/logs/ for details.", dim,
                  (int)(284.0 * g_scale));

    _chrome(screen, "PCLink", "Try again");
}

#endif // PCLINK_RENDER_H__
