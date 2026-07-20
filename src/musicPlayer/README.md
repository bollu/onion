# musicPlayer (OnionMusic)

A native music player, intended to replace the vendored GMU build in
`static/packages/App/Music Player (GMU)/`.

**Status: froze to a black screen on hardware; two candidate fixes in, not yet
re-tested.** Build with `NO_AUDIO=1` to take audio out of the picture entirely:

```sh
make with-toolchain CMD="music-player NO_AUDIO=1 DEBUG=1"
```

That produces the same `App/OnionMusic/` folder with SDL_mixer unlinked, so a black
screen then implicates startup or rendering rather than audio. `DEBUG=1` matters:
`printf_debug()` compiles to nothing without it, so a normal build logs nothing at
all. Startup milestones then land in `/mnt/SDCARD/.tmp_update/logs/musicPlayer.log`.

Verified offscreen under qemu with the dummy video driver: the app reaches its main
loop and paints at **117 ms**, with and without a track argument. The audio seam in
`audio.h` is what makes running the whole app there possible.

The cause was `launch.sh` running `stop_audioserver.sh` before starting the app.
That line was copied from GMU, and it is right for GMU: GMU `LD_PRELOAD`s its own
SDL and drives `mi_ao` directly, so it needs audioserver out of the way. The same
is true of the other callers — Drastic, PICO-8, ScummVM, OpenBOR.

This app links the **system** SDL_mixer, whose audio goes *through* audioserver.
Killing it first leaves `Mix_OpenAudio` with nothing to talk to, and it blocks —
a freeze, on a black screen, before anything is drawn.

Confirmed afterwards by tracing the audio path properly: the system SDL 1.2 has one
audio driver, `dsp`, which writes to `/dev/dsp`. `runtime.sh:225` launches MainUI with
`LD_PRELOAD=miyoo/lib/libpadsp.so`, and every app launched from MainUI inherits it —
that shim intercepts `/dev/dsp` and forwards to audioserver. Killing audioserver left
the shim forwarding to a process that no longer existed, so `Mix_OpenAudio` blocked.

The comparison that isolated it:

| app | `HAS_AUDIO` | kills audioserver | result |
| --- | --- | --- | --- |
| demoApp | no | no | works |
| PCLink | no | no | works |
| bebook | no (video only) | no | works |
| **tweaks** | **yes** | **no** | **works** |
| musicPlayer (before) | yes | yes | black screen |

tweaks is the decisive row: audio via the system SDL is fine, provided audioserver
is left alone. So the fix is simply not to kill it.

Two things changed alongside, so a future failure is not as opaque:

- `launch.sh` now redirects to `log.txt`. Before, `./musicPlayer` ran with no
  redirect and anything it printed went nowhere.
- `SDL_InitDefault()`'s return value is checked. It returns false when
  `Mix_OpenAudio` fails; that was silently ignored, so a dead audio device looked
  identical to a working one right up until the screen stayed black. The app now
  logs it and carries on, since a usable UI beats a blank screen.

### Audio setup

Adopted from MiyooPod's `src/audio.c`, which is SDL_mixer too:

- **Tracks are decoded from RAM.** `audio_load()` reads the file, wraps it in
  `SDL_RWFromMem` and hands it to `Mix_LoadMUS_RW`, so playback does no SD I/O — the
  thing MiyooPod blames for starving the decoder mid-song. Over 32 MB it streams
  instead; the device has 128 MB and MP3s are 3-10 MB. Verified on this exact library:
  `Mix_LoadMUS_RW` reports `MUS_MP3_MAD`, plays and seeks.
- **A music-sized buffer.** `SDL_InitDefault()` opens the device at 4096 frames, which
  suits UI blips, so `audio_init()` reopens it at 8192. MiyooPod uses 524288, which is
  not copied: `SDL_AudioSpec.samples` is a `Uint16`, so that cannot mean frames.
- **`Mix_HookMusicFinished` sets a flag**, replacing the end-of-track poll.
- **Position comes from a post-mix byte counter.** SDL2's `Mix_GetMusicPosition` has no
  1.2 equivalent, but `Mix_SetPostMix` sees every byte on its way to the device, so
  counting them gives the true audio clock. `Mix_QuerySpec` supplies the byte rate.
  Pausing then needs no correction — measured +0.00s across a one-second pause.

`Mix_MusicDuration` has no 1.2 equivalent either, which is why `mp3.h` exists.

### Compared with MiyooPod

[MiyooPod](https://github.com/danfragoso/miyoopod) is a working MP3 player for this
device, and takes the opposite trade deliberately. It bundles its own **SDL2** stack
(`libSDL2.so`, `libSDL2_mixer.so`, `libmpg123.so.0`) built with Miyoo's `mmiyoo`
video and audio drivers, then kills audioserver, MainUI *and* keymon to own the
device outright, restoring them on exit. Volume it drives itself through
`MI_AO_SETVOLUME` ioctls on `/dev/mi_ao`, reproducing Onion's logarithmic curve.

That works, but it is inseparable from vendoring the stack this rewrite removed: the
system SDL 1.2 has no `mmiyoo` driver, only `dsp`. Going direct to the hardware means
shipping an SDL that can, which is where GMU's several megabytes of binaries came
from.

Worth taking from it without that: pinning the CPU governor during playback (done),
session persistence — restoring queue, position and shuffle/repeat on launch — and
browsing by artist/album rather than a flat list. Its album-art fetching needs the
network and is out of scope here.

If audioserver turns out to be unusable for continuous playback, MiyooPod's approach
is the proven fallback, at that cost.

Still unverified on hardware: that playback actually works through audioserver, the
volume keys, timing drift, and behaviour with the display off.

## Rendering the UI without a device

Both screens can be rendered offscreen, which is how the layout was checked. The
trick is that the fallback theme lives at `/mnt/SDCARD/miyoo/app/`, which is
`static/build/miyoo/app/` in this repo — so a container can fake an SD card:

    mkdir -p /mnt/SDCARD && cp -r static/build/* /mnt/SDCARD/

Then build a harness that calls `render_library()` / `render_nowPlaying()` and
`SDL_SaveBMP`s the `screen` surface, and run it under `qemu-arm`. Three things
are needed or it will not work:

- `-DPLATFORM_MIYOOMINI`, or `DEFAULT_WIDTH/HEIGHT` in `system/display.h` fall
  back to 752x560 and the render is the wrong size. `/dev/fb0` doesn't exist in a
  container, so `display_getRenderResolution()` cannot correct it.
- `SDL_VIDEODRIVER=dummy` and `SDL_AUDIODRIVER=dummy`.
- The device's `libpng12.so.0` on `LD_LIBRARY_PATH` (from
  `static/build/miyoo/lib/`). `libSDL_image` loads PNG via `dlopen` rather than
  linking it, so without it every `IMG_Load` returns "Unsupported image format",
  `theme_backgroundLoad()` passes NULL to `rotate180()`, and that dereferences
  `original->format` and segfaults.

## Build

This **is** the shipped music player: it replaced the vendored GMU package, and
is built by the `apps` target and preinstalled, so a normal `make all` includes it.

For a quick iteration loop that skips the rest of the release build:

```sh
make with-toolchain CMD=music-player
```

Output in `build/sideload/App/OnionMusic/`. Copy that folder to `/mnt/SDCARD/App/OnionMusic/`
to try it without reflashing.

The sample tracks that used to live in the GMU package moved to
`static/packages/App/OnionMusic/Media/Music/`, and land in `/mnt/SDCARD/Media/Music`
— which is where this scans.

## Why it is called OnionMusic

The app is named `OnionMusic`, not `Music Player`, so it does not collide with a
GMU install already on the card. MainUI lists apps by the `label` in config.json
(`getInstalledApps()` in `src/common/utils/apps.h`), and GMU's label is
"Music Player" — two entries with the same label are only distinguished by a
`dup_id` suffix, which is not something a user should have to decode. The source
directory and binary stay `musicPlayer`.

## Why this is worth doing

GMU is checked into the repo as a prebuilt binary plus its own SDL 1.2, eight
decoder `.so` files, themes and frontends — several MB of binaries no one in the
project can rebuild from source. This binary is ~110 KB and links only libraries
already on the device.

## The audio question, settled

The device's `libSDL_mixer-1.2.so.0.12.0` (in `static/build/miyoo/lib/`) is built
with `music_mad.c` and lists `libmad.so.0` in `DT_NEEDED`, so MP3 decoding needs
nothing bundled. Confirmed by cross-compiling a probe and running the ARM binary
under qemu against a real file:

```
music type = 7 (MUS_MP3=6, MUS_MP3_MAD=7)   -> MUS_MP3_MAD
playing = 1
seek rc = 0                                  -> Mix_SetMusicPosition works
```

Note this is **not** `dr_mp3`: that is SDL_mixer 2.6+, and this is SDL_mixer 1.2.

## Scope: MP3 only

The device's SDL_mixer has **only** libmad — no vorbis, no FLAC, no modplug. The
toolchain's copy *does* link vorbis/ogg, so OGG code would compile and link fine
and then fail at runtime on hardware. `library.h` therefore accepts `.mp3` only.

This is a real feature regression against GMU, which handles vorbis, opus, flac,
modplug, openmpt, speex and wavpack. Supporting those again means shipping decoder
`.so` files — exactly the vendored-binary situation this replaces. **Decide whether
MP3-only is acceptable before going further.**

## Duration, and why there is a progress bar at all

SDL_mixer 1.2 reports neither duration nor play position, so both are derived:

- **Duration** — `mp3.h` skips the ID3v2 tag, then reads the exact frame count
  from a Xing/Info/VBRI header, falling back to walking every frame header and
  counting. A single-frame CBR extrapolation is deliberately *not* used as the
  general case: the bundled Onion theme tracks are VBR with no Xing header, and
  extrapolating from the first frame gives ~154s for a 205s file.
- **Elapsed** — `player.h` measures with `SDL_GetTicks()` against a base offset
  that pausing and seeking adjust.

Verified against the two bundled tracks, with `ffprobe` as ground truth:

| File | `mp3.h` | ffprobe | Error |
| --- | --- | --- | --- |
| Onion music theme - 01 | 205.032s | 205.043s | 0.005% |
| Onion music theme - 02 | 187.246s | 187.256s | 0.005% |

(Both are VBR, 48/44.1 kHz, with ~59 KB ID3v2 cover-art tags. The residual is
encoder delay/padding, which ffmpeg accounts for and a frame count does not.)

## Layout

| File | Purpose |
| --- | --- |
| `musicPlayer.c` | Event loop, view state machine, key handling. |
| `render.h` | Both screens: library list and Now Playing, incl. progress bar. |
| `player.h` | Playback state: play/pause/skip/seek, shuffle, repeat, elapsed. |
| `library.h` | Scans `/mnt/SDCARD/Media/Music` into a `List`. |
| `mp3.h` | Track duration from frame headers. |

## Controls

| Button | Library | Now Playing |
| --- | --- | --- |
| Up / Down | Move selection | — |
| A | Play selected, go to Now Playing | Pause / resume |
| B | Exit | Back to library |
| X | Pause / resume | Pause / resume |
| Y | Toggle shuffle | Toggle shuffle |
| SELECT | Cycle repeat (off / all / one) | Cycle repeat |
| START | Jump to Now Playing | — |
| L1 / R1 | Previous / next track | Previous / next track |
| Left / Right | — | Seek ∓5s |
| MENU | Exit | Exit |

L1 restarts the current track unless pressed within its first 3 seconds, which is
the conventional behaviour.

## Known gaps

- **No hardware test.** Everything below is reasoning from the code, not observation.
- **Elapsed time is measured, not queried.** It cannot account for SDL_mixer's
  internal buffering and will drift on a long track. Seeking resyncs it.
- **Now Playing redraws every frame** while a track runs, because the progress bar
  has to advance. The library view only redraws on change. If battery life
  matters, the bar could be redrawn once a second instead.
- **Flat directory scan — no albums.** `library_scan()` skips subdirectories
  outright, so files must sit directly in `Media/Music/`. Drop in a folder of
  albums and the library comes up empty, with nothing on screen explaining why.
  Browsing folders as albums is the most wanted improvement; see `TODO.md`.
- **Track names are filenames.** No ID3 tags are read, so a listing is only as tidy
  as the file names. `mp3.h` already locates the ID3v2 tag, so reading
  `TIT2`/`TPE1`/`TALB` is incremental work rather than a new parser.
- **No playlists.** GMU reads `.m3u` and `.pls`.
- **Durations are computed for the whole library at startup**, after the first
  frame is drawn. With a large library on a slow SD card this will stall the UI
  briefly; it should move to a background thread or become lazy per-track.
- **No album art**, despite the ID3v2 parser already locating the tag.
- **No background playback.** GMU keeps playing with the display off (PR #1749).
  This exits its loop only on B/MENU, but nothing yet handles the display-off path
  in `system/display.h`.
- **Volume.** `launch.sh` kills `audioserver` to free `mi_ao`, as GMU does. Whether
  the hardware volume keys still work in that state is untested — check
  `system/volume.h` against the audioserver-dead case.
- **`Mix_OpenAudio` is called by `SDL_InitDefault()`** at 48000 Hz stereo. If a
  track's sample rate differs, SDL_mixer resamples; quality has not been assessed.

## Reverting to GMU

The GMU package was deleted in the commit that switched this on, so `git revert`
of that commit restores it (the binaries are still in history). Then drop the
`musicPlayer` line from the `apps:` target and the matching preinstall line.
