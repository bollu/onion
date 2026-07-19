# Text stack dependencies on the Miyoo Mini toolchain

Findings from inspecting the union-miyoomini-toolchain sysroot
(`miyoomini-toolchain-buildroot-aarch64` v0.0.3, path
`/opt/miyoomini-toolchain/arm-linux-gnueabihf/libc/usr`).

## FreeType — available, no work needed

```
usr/include/freetype2/          ft2build.h, freetype/*.h  (incl. ftcolor.h)
usr/lib/libfreetype.so.6.17.1   -> FreeType 2.10.1
usr/lib/libfreetype.a           static archive also present
usr/lib/pkgconfig/freetype2.pc
```

2.10.1 covers everything bebook's text engine needs: `FT_LOAD_TARGET_LIGHT`
(vertical-only hinting, the correct pairing with fractional pen positioning),
the autofit/CFF stem-darkening controls, and 26.6 fixed-point metrics.

## HarfBuzz — absent from the sysroot, but built from source in-tree

There is no HarfBuzz anywhere in the sysroot. (`plugin/include/graphite.h` is a GCC
compiler-internals header and unrelated.) There is also no meson/ninja/pkg-config in
the sysroot, which initially looked like it would force an awkward cross-build step.

It doesn't. HarfBuzz ships an official **single-file amalgamation**,
`src/harfbuzz.cc`, which `#include`s every other `.cc` in the project. Compiling that
one translation unit is the entire build:

```
c++ -std=c++17 -O2 -DHAVE_FREETYPE=1 -DHB_NO_MT -Ithird-party/harfbuzz/src \
    $(FREETYPE_CFLAGS) -c third-party/harfbuzz/src/harfbuzz.cc
```

~9 seconds, 1.4MB object. No meson, no autotools, no configure, no container step, and
no shared library to bundle or `LD_LIBRARY_PATH` ordering to get right. The backends we
don't want (glib, ICU, graphite2, CoreText, DirectWrite, GDI) self-disable because their
`HAVE_*` macros are undefined.

HarfBuzz is vendored as a submodule pinned to **8.5.0** at `third-party/harfbuzz`, which
requires only C++11 and so is comfortably within the toolchain's GCC 8.3. It is MIT
("Old MIT") licensed, which is GPL-3.0 compatible.

FreeType is linked normally against the sysroot's `libfreetype.so.6` — the firmware
already ships a FreeType for its own SDL_ttf, and we are linking the same soname the
toolchain was built against.

### Verified end to end

Cross-compiled inside the container on the real toolchain:

```
compiler          arm-linux-gnueabihf-c++ (Linaro GCC 8.2-2018.08~dev) 8.3.0
sysroot FreeType  2.10.1
sysroot SDL_ttf   2.0.11   (the SDL 1.2 ceiling; there is no 2.20 for SDL 1.2)
harfbuzz.cc       -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -march=armv7ve+simd
                  -> 15s, 1.6MB object, no errors
```

## Container image

Upstream union-miyoomini-toolchain's own Dockerfile is pinned to `debian:buster-slim`.
Buster is EOL and has been dropped from deb.debian.org, so its `apt-get update` now
fails with 404s on the Release files and the image cannot be built at all.

`cross-compile/miyoo-mini/Dockerfile` replaces it: same toolchain tarball (v0.0.3,
selected by host arch), on `debian:bookworm-slim`. `make miyoo-mini-shell` builds and
enters it, using podman or docker, whichever is on PATH.

## Why HarfBuzz earns its place

FreeType's `FT_Get_Kerning` reads only the legacy `kern` table. Measured by shaping
`"AVATAR Ta Wo, Yo."` at 26px and summing advances both ways:

```
                     FreeType         HarfBuzz    difference
DejaVu (kern+GPOS)   252px (-17 kern)   227px       -25px
NewYork (GPOS only)  217px (  0 kern)   181px       -36px
SFNS    (GPOS only)  222px (  0 kern)   193px       -29px
```

Two things follow. DejaVu ships both tables, yet FreeType still finds only 17px of the
25px of kerning the font actually specifies. And for GPOS-only fonts — which is
essentially every modern OFL text face, including the Literata/Charis SIL candidates for
the body font — FreeType finds **exactly zero**, losing ~17% of the line width in
spacing corrections. Switching to a better body font without a shaper would have
produced measurably worse typography than the status quo.

## Note on why HarfBuzz is worth the build

FreeType's `FT_Get_Kerning` reads only the legacy `kern` table. Verified against
fonts on hand:

```
DejaVuSans.ttf     kern:True   GPOS:True
DejaVuSerif.ttf    kern:True   GPOS:True
Quicksand-*.ttf    kern:False  GPOS:True
NewYork.ttf        kern:False  GPOS:True
SFNS.ttf           kern:False  GPOS:True
```

DejaVu ships both tables, so kerning happens to work today. Essentially every
modern OFL text face ships kerning **only** in GPOS, so switching to a better body
font without a shaper would silently produce *worse* spacing than today.
