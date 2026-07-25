# bebook, inside the Onion tree

bebook is an epub reader. It lives here rather than as a submodule so it can be worked on
alongside the rest of Onion, and eventually share code with it.

Forked from [pixel-reader](https://github.com/ealang/pixel-reader) by ealang, GPL-3.0 like
Onion itself. `README.md` is the project's own; this file covers the in-tree build.

## Build

```sh
make bebook                     # from the Onion root
make with-toolchain CMD=bebook  # or inside the toolchain container
```

Output is staged to `build/App/PackageManager/data/App/BeBook/App/BeBook/`, and `make apps`
both builds it and preinstalls it, so a release contains it without the user installing
anything.

Requires the HarfBuzz submodule:

```sh
git submodule update --init --recursive third-party/harfbuzz
```

## Why it does not use `src/common/*.mk`

It keeps its own Makefile, like `third-party/DinguxCommander`. The shared config collects
sources with a flat `wildcard` over `SOURCES`, sets no C++ standard, and has nowhere to put
per-file flags — and bebook is C++17 across a nested tree, needs FreeType/libzip/libxml2,
and compiles HarfBuzz from a single-file amalgamation that must be built *without*
`-pedantic-errors` and without `-MMD`. Special-casing all of that in `config.mk` would make
it worse for every other app.

## Dependencies

| Library | Where from |
| --- | --- |
| SDL 1.2, FreeType | toolchain sysroot |
| libzip, libxml2 (+ libz, liblzma) | `src/bebook/deps/` — absent from both the sysroot and Onion's `lib/` |
| HarfBuzz | `third-party/harfbuzz`, compiled into the binary from its amalgamation |

It links **no SDL_ttf and no SDL_image**. Both were replaced: SDL_ttf rounds every glyph
advance to a whole pixel and reads kerning only from the legacy `kern` table, which modern
OFL fonts do not ship; images decode through a vendored `stb_image`. This is the main way
bebook differs from every other app in this repo, and the reason its reading page cannot
simply be re-rendered through `theme/`.

## Packaging

Manifest, launcher and icon live in `static/packages/`, not here:

- `static/packages/App/BeBook/App/BeBook/` — the app entry
- `static/packages/Emu/Books (bebook)/` — registers `.epub` as an OnionOS *system*, which
  is what puts individual books in Recents. MainUI maintains `Roms/recentlist.json` for
  anything launched from a registered system, so recents, favourites, box art, play-time
  tracking and GameSwitcher all follow without bebook writing to any of them.

## Deploying to a device

From the repo root, after `make with-toolchain CMD=bewiki`:

```sh
make deploy-card    # card is in this Mac's reader
make deploy-wifi    # device is serving its own hotspot
```

Both stage into `build/sideload/` first (a tree shaped like the card root) and then rsync
it across. Two targets rather than one with a flag: they have different failure modes,
different preflight checks, and costs three orders of magnitude apart.

`deploy-card` finds the volume by looking for `.tmp_update/` and `Roms/` together, and
refuses to write anywhere that lacks both — `SD=/Volumes/NAME` overrides the search but is
still checked, since the failure being guarded against is unpacking 284MB onto a backup
drive. `deploy-wifi` needs a **Miyoo Mini Plus** (the base Mini has no Wi-Fi) with
Tweaks → Network → SSH on, and this Mac joined to `MiyooMini+APOnionOS`; the device is
always `192.168.100.100` there. Run `make deploy-wifi-key` once to stop it asking for a
password. `DRY_RUN=1` previews either.

Nothing is installed on the device to make this work: Onion already ships dropbear and
rsync in `.tmp_update/bin`.

Both compare with `--checksum` rather than timestamps. The card is FAT32, which stores
mtimes at 2-second resolution, so a written file's timestamp comes back rounded and every
file would otherwise look modified — re-sending the 157MB dictionary on every run.
Comparing content means a file moves only if its bytes differ.

### Content

The Wikipedia archive is not in git (112MB). Fetch it once:

```sh
src/bebook/tools/fetch_zim.sh
```

It resolves the current build from kiwix (the filenames carry a month suffix, so a pinned
URL would stop working within weeks), resumes if interrupted, and lands in
`resources/wiki/`, which is gitignored. Set `ZIM_NAME=wikipedia_it_top_nopic` for full
articles instead of lead sections — same code path, 876MB instead of 112MB.

Deploy stages the archive when it is present and warns when it is not, rather than
quietly shipping a reader with nothing to read.

## Tests

`make test` inside `src/bebook` builds and runs the suite (needs gtest and the host SDL
1.2/FreeType/libzip/libxml2 dev packages). It does not run on device — the binary is ARM.

## Where to look next

- `INTEGRATION.md` — what deeper integration with Onion would buy, in value order.
- `TODO.md` — known gaps, including what has never been verified on hardware.
- `TEXT_STACK.md` — why the text stack is FreeType + HarfBuzz rather than SDL_ttf, with
  the measurements behind it.
