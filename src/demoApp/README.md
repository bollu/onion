# demoApp

A minimal "hello world" app: a themed screen with a header, a centred line of
text, a footer hint, and a battery indicator. It exits on **B** or **MENU**.

It exists to be copied. Everything here is done the same way the real apps do it,
so starting from this directory gets you a working app without reverse-engineering
`tweaks` (which is the fullest example, but is ~1500 lines across a dozen headers).

## Build it

```sh
make demo-app
```

Output lands in `build/sideload/App/DemoApp/`. To try it on hardware, copy that folder to
`/mnt/SDCARD/App/DemoApp/` on the SD card; it shows up in the app list on the next
reload.

It is *also* built and preinstalled by the `apps` target, so it currently ships in
release builds. That is useful while developing against it, but a template app
probably should not reach end users — **before cutting a release, drop the
`demoApp` lines from `apps:` in the root `Makefile`.**

Everything is cross-compiled inside the toolchain container:

```sh
make with-toolchain CMD=demo-app
```

## What's in here

| File | Purpose |
| --- | --- |
| `demoApp.c` | The app: init, event loop, render, teardown. |
| `Makefile` | 8 lines. The shared build logic lives in `src/common/*.mk`. |

The two files MainUI reads live with the package, not here, so there is only ever
one copy of them:

| File | Purpose |
| --- | --- |
| `static/packages/App/Demo App/App/DemoApp/config.json` | Manifest: label, icon, launch script, description. |
| `static/packages/App/Demo App/App/DemoApp/launch.sh` | What MainUI actually executes. |

## How a new app is put together

**The Makefile.** `src/common/config.mk` supplies the cross-compiler, the ARM
flags, and the include paths; `recipes.mk` supplies the link/strip/install rules.
You only declare the target name and what you link against:

```make
INCLUDE_CJSON=1               # needed by settings_load(), which parses JSON
include ../common/config.mk
TARGET = demoApp
LDFLAGS := $(LDFLAGS) -lSDL -lSDL_image -lSDL_ttf -lSDL_rotozoom
include ../common/commands.mk
include ../common/recipes.mk
```

Two non-obvious bits, both of which are link errors if you omit them:

- `INCLUDE_CJSON=1` — without it `settings_load()` fails to link with
  `undefined reference to cJSON_Parse`.
- `-lSDL_rotozoom` — the theme renderer scales surfaces, so leaving it out gives
  `undefined reference to rotozoomSurface`.

Add `-DHAS_AUDIO` and `-lSDL_mixer` if you want sound; that's what enables the
audio branch in `utils/sdl_init.h` and `theme/sound.h`.

**Startup order matters.** `settings_load()` before `lang_load()`, because the
language file to load is named by `settings.language`.

**Include `theme/theme.h`, not the individual `theme/render/*.h` headers.**
Including e.g. `render/header.h` on its own gives
`conflicting types for 'theme_batterySurface'`, because those headers depend on
each other and `theme/render.h` is what pulls them in the right order.

**Drawing.** `SDL_InitDefault()` (from `utils/sdl_init.h`) creates two surfaces:
`screen`, which you draw into, and `video`, the hardware surface. Compose into
`screen`, then blit to `video` and `SDL_Flip(video)` once per frame. Use
`theme_background()`, `theme_renderHeader()`, `theme_renderFooter()` and friends
so your app follows the user's installed theme.

Only redraw when something changed — the `redraw` flag here. `tweaks` splits this
further into `header_changed` / `list_changed` / `footer_changed`, which is worth
copying once your UI is non-trivial.

**Input.** `updateKeystate()` from `utils/keystate.h` fills a `KeyState` array
indexed by the `SW_BTN_*` constants in `system/keymap_sw.h`. `PRESSED` fires once,
`REPEATING` while held.

**Useful things already written**, in `src/common/`:

- `components/list.h` — scrolling list model with key handlers and value-cycling
  items. Pair with `theme/render/list.h`. This is what the settings-style menus use.
- `system/` — `battery.h`, `volume.h`, `settings.h`, `lang.h`, `display.h`, `state.h`.
- `utils/` — `file.h`, `str.h`, `log.h`, `json.h`, `keystate.h`, `process.h`.

Use `lang_get(LANG_X, LANG_FALLBACK_X)` for user-facing strings so the app is
translatable.

## Ship it as a real app

Two steps.

1. Add a package folder under `static/packages/App/<Your App>/App/<YourApp>/`
   containing `config.json` and `launch.sh` (copy Demo App's). The Makefile rsyncs
   `static/packages/` into the Package Manager catalog, so anything there becomes
   installable. Keep these two files only in the package — the quick-iteration
   targets copy them from there, so there is nothing to keep in sync.

2. Build the binary into that folder from the `apps:` target in the root
   `Makefile`, next to the existing entries:

   ```make
   @cd $(SRC_DIR)/demoApp && BUILD_DIR="$(PACKAGES_APP_DEST)/Demo App/App/DemoApp" make
   ```

   If your app has a `res/` folder, copy it too — see how `batteryMonitorUI` and
   `playActivityUI` do it in the same target.

To have it preinstalled rather than merely installable, also add a
`cp -a "$(PACKAGES_APP_DEST)/<Your App>/." $(BUILD_DIR)/` line, as the Activity
Tracker and Tweaks entries do.

The icon path in `config.json` is relative to the app folder on the SD card;
available icons are in `static/build/Icons/Default/app/`. This demo borrows
`manual.png` — a real app should add its own.
