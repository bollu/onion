# Integrating bebook into OnionOS

How to build bebook as part of the Onion tree, so it ships with a release rather than
being installed as a loose app.

Everything asserted here about the build was checked by running it: bebook
cross-compiles inside Onion's own toolchain image and produces an ARM32 binary with the
runtime dependencies listed below.

---

## The short version

bebook does **not** slot in the way `src/demoApp/` does, and should not be made to.
`demoApp` is a single C file that inherits its build from `src/common/config.mk`; bebook
is ~50 C++17 translation units across nested directories, with a vendored HarfBuzz and
its own dependency resolution. Onion already has a category for exactly this — the
`external:` target, which builds `DinguxCommander`, `Terminal` and `SearchFilter` by
invoking each project's own Makefile.

So: **add bebook as a `third-party/` submodule and build it from `external:`**, using
`demoApp`'s packaging conventions (`config.json`, `launch.sh`, a folder under
`static/packages/`) but not its Makefile conventions.

---

## Why not the `demoApp` / `apps:` path

`src/common/config.mk` collects sources with

```make
CFILES   := $(foreach dir, $(SOURCES), $(wildcard $(dir)/*.c))
CPPFILES := $(foreach dir, $(SOURCES), $(wildcard $(dir)/*.cpp))
```

`SOURCES` defaults to `.`, so a nested tree means enumerating every directory by hand.
Beyond that, four things in bebook have no expression in that shared config:

| bebook needs | `config.mk` provides |
| --- | --- |
| `-std=c++17` | `CXXFLAGS := $(CFLAGS)`, no standard selected (GCC 8.3 defaults to gnu++14) |
| FreeType, libzip, libxml2 include paths | `-I../../include -I../common` only |
| A separate compile rule for the HarfBuzz amalgamation, without `-pedantic-errors` and without `-MMD` | one uniform `%.o: %.cpp` rule |
| `-Wl,-rpath` pointing at its own bundled libs | `-Wl,-rpath=/mnt/SDCARD/.tmp_update/lib` |

Forcing these into `config.mk` would mean either special-casing bebook there — which
makes the shared file worse for every other app — or flattening bebook's source tree.
Invoking bebook's existing Makefile costs nothing and keeps both sides clean.

---

## Toolchain: already compatible, and duplicated

Onion's `Containerfile.toolchain` and bebook's `cross-compile/miyoo-mini/Dockerfile` are
the **same recipe**: `debian:bookworm-slim`, shauninman's `miyoomini-toolchain` v0.0.3
selected by host arch, identical `PATH` / `CROSS_COMPILE` / `PREFIX` / `UNION_PLATFORM`.
Onion's additionally installs `rsync`, `p7zip-full` and `unzip` for its release pipeline.
Both tag `localhost/miyoomini-toolchain:latest` on an arm64 host, so they currently
collide.

They were written independently, for the same reason: the upstream
`aemiii91/miyoomini-toolchain` image is amd64-only and does not work under rootless
podman on Apple silicon.

**On integration, delete bebook's Dockerfile and use Onion's.** Verified working, run from
the **repository root** — not from `src/bebook`:

```sh
podman run --rm -v "$PWD":/root/workspace:z -w /root/workspace/src/bebook \
    localhost/miyoomini-toolchain \
    sh -c 'export PLATFORM=miyoomini && make -j8 all'
```

The mount must be the repo root because the Makefile reaches outside bebook for the
vendored harfbuzz (`-I../../third-party/harfbuzz/src`). Mounting `src/bebook` alone puts
that path outside the container and the build stops with

```
No rule to make target '/root/workspace/third-party/harfbuzz/src/hb.h',
needed by 'build/miyoomini/objects/src/text/shaper.o'.
```

which names a missing header rather than a missing mount, so it is worth recognising on
sight. `all` builds all four binaries; name one target (`reader`, `wiki`, `dict`) to build
just that app. Outputs land in `build/miyoomini/`, and

```sh
readelf -h src/bebook/build/miyoomini/bebook | grep -E 'Class|Machine'
```

produces

```
Class:   ELF32
Machine: ARM
```

Onion's sysroot already carries everything bebook needs — FreeType 2.10.1, libzip,
libxml2, SDL 1.2 — because it is the same buildroot sysroot bebook was developed
against.

Host and cross builds write to `build/$(PLATFORM)/` rather than a shared `build/`. They
must not share: the `.d` dependency files GCC emits record absolute include paths, so a
cross build followed by a host build used to fail with `No rule to make target
'/opt/miyoomini-toolchain/.../ft2build.h'` -- a message that points nowhere near the
cause.

### Runtime dependencies of the built binary

```
libstdc++.so.6   libSDL-1.2.so.0   libfreetype.so.6
libzip.so.5      libxml2.so.2      libm / libgcc_s / libc
```

Two things worth noting. **HarfBuzz does not appear**: it is compiled from its
single-file amalgamation straight into the binary, so there is no shared library to ship
or version. And **SDL_ttf and SDL_image do not appear**: bebook replaced both, so it
does not depend on the firmware's copies at all.

`libfreetype.so.6` is new relative to stock pixel-reader. The firmware ships one for its
own SDL_ttf, but bebook bundles its own rather than binding to whatever version happens
to be present.

---

## Integration steps

### 1. Add the submodule

```sh
git submodule add https://github.com/<you>/bebook third-party/bebook
```

bebook has a submodule of its own (`third-party/harfbuzz`, pinned to 8.5.0), so Onion's
setup must recurse:

```sh
git submodule update --init --recursive
```

This is the one genuine wrinkle in the integration. Onion's existing submodules are
flat, so if `$(CACHE)/.setup` or CI initialises submodules non-recursively, bebook's
build will fail with `harfbuzz submodule missing` — the Makefile checks for it and says
so explicitly rather than failing on a missing header.

### 2. Package folders

Following `demoApp`'s "Ship it as a real app" instructions, create:

```
static/packages/App/BeBook/App/BeBook/config.json
static/packages/App/BeBook/App/BeBook/launch.sh
```

and, for the Recents integration (WP6), a second package registering books as a system:

```
static/packages/Emu/Books (bebook)/Emu/EBOOK/config.json
static/packages/Emu/Books (bebook)/Emu/EBOOK/launch.sh
static/packages/Emu/Books (bebook)/Roms/EBOOK/Imgs/.gitkeep
```

The `Emu/EBOOK/config.json` is what makes individual books appear in OnionOS Recents:
MainUI writes `Roms/recentlist.json` itself whenever it launches anything from a
registered system, so recents, favourites, box art and GameSwitcher all come for free.
`Ports Collection` is the precedent for a non-emulator system.

```json
{
  "label": "Books",
  "icon": "../../Icons/Default/app/ereader.png",
  "iconsel": "../../Icons/Default/app/ereader.png",
  "launch": "launch.sh",
  "rompath": "../../Roms/EBOOK",
  "imgpath": "../../Roms/EBOOK/Imgs",
  "gamelist": "../../Roms/EBOOK/miyoogamelist.xml",
  "extlist": "epub|txt",
  "useswap": 0
}
```

`Emu/EBOOK/launch.sh` must `cd` into the app directory before exec'ing, because
`bebook.cfg` and `resources/fonts` resolve relative to the working directory:

```sh
#!/bin/sh
cd /mnt/SDCARD/App/BeBook
LD_LIBRARY_PATH=./lib exec ./bebook "$@"
```

`"$@"` rather than `"$1"`: launching from the apps list passes no argument, and an empty
one would make bebook try to open the path `""`.

### 3. Build it from `external:`

In the root `Makefile`, alongside the DinguxCommander and Terminal entries:

```make
	@$(ECHO) $(COLOR_BLUE)"\n-- Build bebook"$(COLOR_NORMAL)
	@cd $(THIRD_PARTY_DIR)/bebook && make -j4
	@mkdir -p "$(PACKAGES_APP_DEST)/BeBook/App/BeBook/lib"
	@mkdir -p "$(PACKAGES_APP_DEST)/BeBook/App/BeBook/resources/fonts"
	@cp $(THIRD_PARTY_DIR)/bebook/build/bebook "$(PACKAGES_APP_DEST)/BeBook/App/BeBook/"
	@cp $(THIRD_PARTY_DIR)/bebook/resources/fonts/*.ttf \
	    $(THIRD_PARTY_DIR)/bebook/resources/fonts/*.txt \
	    "$(PACKAGES_APP_DEST)/BeBook/App/BeBook/resources/fonts/"
```

`PLATFORM` comes from `UNION_PLATFORM`, which the toolchain image already exports, so
bebook picks up the ARM flags without being told.

Copy the runtime libraries listed above into `App/BeBook/lib/`, or set bebook's
`launch.sh` to rely on Onion's `/mnt/SDCARD/.tmp_update/lib` if you would rather share
them — but check FreeType's version there first.

To have bebook preinstalled rather than merely installable, add the usual line to the
`apps:` target:

```make
	@cp -a "$(PACKAGES_APP_DEST)/BeBook/." $(BUILD_DIR)/
```

### 4. Fonts

bebook ships Charis SIL (four faces, ~3.4MB) under OFL. Onion already bundles fonts, so
if size matters they could be shared — but bebook resolves style faces by filename
convention (`Charis-Regular.ttf` → `Charis-Italic.ttf`), so any shared location must keep
that naming for italics and bold to be found.

---

## What "deeper integration" would actually involve

The build integration above is mechanical. The user-visible integration is not, and it
is worth being explicit about the gap.

**bebook does not use Onion's UI framework at all.** It does not call
`theme_renderHeader()`, does not read the user's installed theme, and does not use
`components/list.h`. An Onion release containing bebook would ship one app that looks
unlike every other app.

Closing that means either:

- **Adopting Onion's theme renderer for bebook's chrome** — the library view, settings
  and popups — while keeping bebook's own text engine for the reading page. The reading
  page is the whole point of the project and must not be rendered through SDL_ttf. This
  is the sensible middle path, and is a real piece of work: bebook's views draw through
  `text::draw_text` into a surface it owns, so they would need to compose with
  `theme_background()` and the header/footer surfaces instead.
- **Or leaving bebook visually independent**, and treating it as a full-screen
  application in the way RetroArch is. Defensible: a reader is a mode you enter, not a
  menu you pass through.

That decision should be made deliberately rather than by default.

---

## How bebook's UI is actually built

No framework. The whole thing is SDL 1.2 surfaces plus about 150 lines of view
scaffolding.

### View stack

`src/reader/view.h` defines a five-method interface:

```cpp
class View {
    virtual bool render(SDL_Surface *dest, bool force_render) = 0;
    virtual bool is_done() = 0;
    virtual bool is_modal() { return false; }
    virtual void on_keypress(SDLKey key) = 0;
    virtual void on_keyheld(SDLKey, uint32_t) {}
};
```

`ViewStack` (`src/reader/view_stack.cpp`) holds a stack of them, routes input to the
top, and pops views that report `is_done()`. The current views are `LibraryView`,
`FileSelector`, `TokenView` (the reading page), `SelectionMenu`, `SettingsView`,
`PopupView` and `ReaderBootstrapView`.

Each view owns a `struct XViewState` behind a `unique_ptr`, which keeps SDL and layout
details out of the headers.

### Rendering model

Retained, not immediate: `render()` returns whether it drew anything, and the main loop
only flips when something did. Views set an internal `needs_render` flag from input or
from a `SystemStyling` subscription. `main.cpp` composes into an offscreen `screen`
surface and blits to `video` once per frame, capped at 20 FPS.

There is no widget tree, no layout engine, and no retained scene — each view draws
itself with `SDL_FillRect` and text calls, positioning things arithmetically.

### Text

This is where the project's weight is, and it is entirely bebook's own
(`src/text/`, ~2000 lines):

- **`face_cache`** — owns the `FT_Library` and every `FT_Face`; resolves family + style
  to a concrete face, with per-codepoint fallback for CJK.
- **`shaper`** — HarfBuzz behind a single function. Positions come back in 26.6 fixed
  point, from HarfBuzz's own OpenType font functions so advances stay fractional.
- **`glyph_atlas`** — rasterizes each glyph once per (face, size, glyph, subpixel phase)
  and keeps the coverage bitmap. Four horizontal phases.
- **`text_render`** — composites through a ramp precomputed in **linear light**, so
  antialiasing is gamma-correct.
- **`line_break`** — Knuth–Plass total-fit paragraph breaking.
- **`hyphenate`** — Liang's algorithm over the TeX en-US patterns.
- **`styled_text`** — measures and draws text whose style varies mid-line.
- **`font`** — a small SDL_ttf-shaped facade (`draw_text`, `text_size`) so view code
  reads conventionally.

**bebook uses no SDL_ttf and no SDL_image.** Both were removed: SDL_ttf rounds every
glyph advance up to a whole pixel and reads kerning only from the legacy `kern` table,
which modern OFL fonts (including Charis SIL) do not ship at all. Images decode through
a vendored `stb_image`.

This is the substantive difference from every Onion app, which use `SDL_ttf` via
`theme/`. It is also why the reading page cannot simply be reskinned onto Onion's
renderer.

### Input

Raw SDL key events, mapped through `src/sys/keymap.h` (`SW_BTN_*`), with a
`Throttled` helper for key repeat. Onion's `utils/keystate.h` does the same job
differently; if bebook adopts Onion's chrome it would be reasonable to adopt its input
handling too.

---

# Deeper integration: what moving into the tree would buy

Ordered by value per unit of work. The build integration above is a precondition for
none of it — these are independent — but doing them from inside the tree is much easier,
because `src/common/` is then a relative include away rather than a vendored copy.

## 0. What already works, for free

Worth knowing before writing any code, because two of these look like work and are not.

**Reading time is already tracked.** Onion's `runtime.sh` decides whether to record play
activity with `check_is_game()`, which matches any command containing `/../../Roms/`.
MainUI launches an EBOOK entry as
`"/mnt/SDCARD/Emu/EBOOK/launch.sh" "/mnt/SDCARD/Emu/EBOOK/../../Roms/EBOOK/book.epub"`,
so books already land in the play-activity SQLite database. The Activity Tracker app
will show time per book with no changes at all. Whether "play time" is the right label
for reading is a UI question, not an engineering one.

**Favourites, GameSwitcher and the game-list options menu** likewise come from being a
registered system, not from anything bebook does.

## 1. Theme support

The largest single gap, and the one a user notices immediately: bebook's chrome does not
follow the installed theme, so an Onion release containing it ships one app that looks
unlike the others.

Onion's renderer is `src/common/theme/`: `theme_background()`, `theme_renderHeader()`,
`theme_renderFooter()`, `theme_renderDialog()`, plus `components/list.h` paired with
`theme/render/list.h` for scrolling menus.

**Do this for the chrome only — the library view, settings and popups — and leave the
reading page alone.** The reading page is the entire point of the project, and Onion's
renderer draws text through SDL_ttf, which is precisely what bebook replaced. Rendering
a page through it would undo the work.

The seam is already in the right place: bebook's views draw into an `SDL_Surface` they
are handed, so they can compose over `theme_background()` instead of over a
`SDL_FillRect`. `SystemStyling::get_loaded_color_theme()` becomes a shim over Onion's
`theme.list.color` and friends rather than bebook's own `color_theme_def.cpp`.

Real work, mostly in `library_view.cpp` and `settings_view.cpp`. Also drags in
`INCLUDE_CJSON=1`, since `settings_load()` parses JSON.

## 2. Translations

bebook's user-facing strings are hardcoded English literals: `"Recently read"`,
`"Library"`, `"No books found"`, `"indexing…"`. Onion has `lang_get(LANG_X,
LANG_FALLBACK_X)` with translation files under `/mnt/SDCARD/miyoo/app/lang`.

Cheap and mechanical, but it needs new `LANG_*` keys added upstream, so it is worth doing
in the same pass as the theme work rather than twice.

## 3. Respect the system settings

`src/common/system/settings.h` already carries values bebook currently ignores:

| Setting | What bebook should do |
| --- | --- |
| `fontsize` | Seed the reading font size, instead of only its own stored value |
| `language` | Select the hyphenation pattern set — bebook is en-US only today |
| `sleep_timer` | Currently only `IDLE_SAVE_TIME_SEC` flushes state; the device sleeps without bebook knowing |
| `brightness`, `volume` | See OSD below |
| `startup_auto_resume` | bebook already resumes the last book unconditionally; this should gate it |

`language` is the interesting one: it turns bebook's `HyphenateFn` seam from a nicety
into a feature, since a French or German book is currently hyphenated by English rules.

## 4. On-screen display for volume and brightness

Onion draws a bar overlay when the user holds MENU and presses volume
(`system/osd.h`: `osd_showVolumeBar()`, `osd_showBrightnessBar()`). bebook renders none
of it, so adjusting brightness mid-chapter — plausibly the most common adjustment while
reading — gives no feedback at all.

Small, self-contained, and disproportionately noticeable.

## 5. Page previews in GameSwitcher

Onion's GameSwitcher shows a screenshot of each recent game, read from
`Saves/CurrentProfile/romScreens/<FNV1a of rom path>.png` (`gs_model.h`, and the hash in
`state.h`). Nothing writes one for bebook, so a book appears as a blank tile.

bebook can already do this: `write_surface_png()` exists and `BEBOOK_SCREENSHOT` proves
the capture path works. Writing the current page on exit means GameSwitcher previews a
book by *the page you were reading*, which is a better affordance than a cover — it is
the one thing a cover cannot tell you.

Perhaps twenty lines. The highest ratio of delight to effort on this list.

## 6. Shared runtime libraries

The package currently bundles `libfreetype`, `libzip`, `libxml2`, `libz`, `liblzma`
(~2.2MB) because it must stand alone. Inside the tree it could link against
`/mnt/SDCARD/.tmp_update/lib` like other Onion apps, provided the FreeType there is
recent enough — check before assuming, since bebook needs 2.10 or newer for
`FT_LOAD_TARGET_LIGHT` to behave as expected.

Also lets the Charis SIL faces (~3.4MB) live in a shared font directory, as long as the
`-Italic` / `-Bold` filename convention is preserved, since that is how bebook discovers
style faces.

## 7. The direction nobody expects: Onion adopting bebook's text engine

Everything above is bebook becoming more like Onion. The larger opportunity is the
reverse.

Every Onion app renders text with SDL_ttf, which:

- rounds every glyph advance up to a whole pixel, so spacing is uneven;
- reads kerning only from the legacy `kern` table, which modern OFL fonts do not ship —
  measured on Charis SIL, SDL_ttf finds **zero** kerning where HarfBuzz finds the font's
  actual values;
- blends antialiasing in sRGB rather than linear light, which is why light-on-dark text
  looks heavier than it should — and Onion's default themes are dark.

`src/text/` is self-contained, has no dependency on anything in bebook, and exposes an
SDL_ttf-shaped facade (`text::draw_text`, `text::text_size`) written precisely so call
sites migrate mechanically. Lifting it to `src/common/text/` would improve every list,
header and dialog in the system.

That is a much bigger conversation than shipping a reader, and it should be had with the
Onion maintainers rather than decided here. But it is the reason moving into the tree is
worth more than keeping bebook as a submodule: shared code can flow both ways, and right
now the interesting flow is outward.

## Suggested order

1. Page previews for GameSwitcher (§5) — tiny, immediately visible.
2. OSD (§4) — small, fixes a real annoyance.
3. Settings (§3) — mostly plumbing, and unlocks per-language hyphenation.
4. Theme and translations together (§1, §2) — the bulk of the work; do them in one pass
   because both touch every view.
5. Shared libraries (§6) — only once the above has settled, since it changes packaging.
6. Text engine (§7) — a separate conversation entirely.

**What to skip.** Do not port the reading page onto Onion's renderer, and do not replace
bebook's view stack with `components/list.h`. The first would undo the project; the
second buys consistency in a place users do not look, at the cost of rewriting working
code.
