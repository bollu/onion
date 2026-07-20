## bebook

A typographically serious ebook reader for the Miyoo Mini. Supports epub and txt formats.

Forked from [Pixel Reader](https://github.com/ealang/pixel-reader) by ealang, and licensed
under the same terms (GPL-3.0). bebook adds real text shaping and layout (FreeType +
HarfBuzz, Knuth–Plass line breaking, hyphenation, italics/bold), cover art in the game
list, and OnionOS Recents integration.

Books are opened as games from the OnionOS game list rather than through a shelf of their
own: MainUI passes the book path, and bebook writes the cover art and reading progress
that MainUI shows.

![Screenshot](resources/demo.gif)

## Miyoo Mini Installation

1. Extract `bebook_onion_vxxx.zip` into the root of your SD card.
2. Put your `.epub` files in `Roms/EBOOK`.
3. Boot your device. `bebook` appears in the apps list, and a `Books` system appears
   alongside your consoles — launching a book from there makes it show up in OnionOS
   Recents, with its cover as box art.

The canonical location for book files is `Roms/EBOOK`.

## Development Reference

### Desktop Build

Install dependencies (Ubuntu):
```
apt install make g++ libxml2-dev libzip-dev libsdl1.2-dev libsdl-image1.2-dev \
            libfreetype-dev libharfbuzz-dev
```

Build:
```
make -j
```

Find app in `build/bebook`. It honours `SCREEN_WIDTH` / `SCREEN_HEIGHT` env vars, so
`SCREEN_WIDTH=640 SCREEN_HEIGHT=480 ./build/bebook` reproduces the device layout.

### Miyoo Mini Cross-Compile

Cross-compile env is provided by [shauninman/union-miyoomini-toolchain](https://github.com/shauninman/union-miyoomini-toolchain). Docker or Podman is required.

Fetch git submodules:
```
git submodule init && git submodule update
```

Start shell:
```
make miyoo-mini-shell
```

Create app packages:
```
./cross-compile/miyoo-mini/create_packages.sh <version num>
```

### Run Tests

[Install gtest](https://github.com/google/googletest/blob/main/googletest/README.md).

```
make test
```
