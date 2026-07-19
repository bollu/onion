# TODO

Open work from the native music player / toolchain changes. Items are grouped by
what would block a release.

## Blocking a release

- [ ] **Run `musicPlayer` on hardware.** Nothing below this line has been observed
      on a device. The UI has only been rendered offscreen under qemu, and audio
      has only been verified as far as "SDL_mixer decodes the file".
- [ ] **Remove `demoApp` from the `apps:` target** in the root `Makefile` (both the
      build line and the preinstall line). It is a template for developers, not an
      app end users should find in their app list.
- [ ] **Decide whether MP3-only is acceptable.** GMU decoded vorbis, opus, flac,
      modplug, openmpt, speex and wavpack. The device's `libSDL_mixer` is built
      against libmad only, so anything else needs decoder `.so` files shipped
      again — the vendored-binary situation the rewrite removed. If the answer is
      no, this change should be reverted rather than patched.

## musicPlayer: correctness

- [ ] **Verify `Mix_SetMusicPosition` accuracy on VBR files.** The bundled tracks
      are VBR with no Xing seek table. If SDL_mixer's `mad_seek` decodes frames to
      the target it is exact; if it extrapolates from bitrate it will land in the
      wrong place. Untested — there is no position query to check it against.
      Also check whether a backwards seek re-decodes from the start, which would
      stall audibly on a long track.
- [ ] **Elapsed time is wall-clock, not the audio clock.** `player_elapsed()` uses
      `SDL_GetTicks()`. Known error sources, worst first:
      - Pause/resume quantization. `Mix_PauseMusic()` acts on a buffer boundary but
        the clock is frozen at the call, so every pause cycle adds a small error
        that never washes out. This is the one that accumulates.
      - A constant ~85 ms lead: the clock starts at `Mix_PlayMusic()`, but the
        4096-sample buffer at 48 kHz has to fill before sound reaches the speaker.
      - Track-end polling at 30 fps, so up to 33 ms late.
      - Oscillator mismatch between wall and audio clocks (~50-100 ppm), negligible
        here because the clock resets each track.
- [ ] **Check volume keys with `audioserver` killed.** `launch.sh` runs
      `stop_audioserver.sh` to free `mi_ao`. Whether `system/volume.h` still works
      in that state is unverified; volume may have to be driven by writing to
      `/proc/mi_modules/mi_ao/mi_ao0` directly.
- [ ] **Confirm resampling quality.** `SDL_InitDefault()` opens the device at
      48 kHz. The 44.1 kHz track is resampled by SDL_mixer 1.2, whose resampler is
      crude. Listen before assuming it is fine.

## musicPlayer: display-off behaviour

`keymon.c` `suspend_exec()` SIGSTOPs every process and mutes on screen-off unless
`/tmp/stay_awake` exists. `launch.sh` creates it, so playback should survive — this
is the same fix GMU needed in #1749 (commit `1975e9c0`). What is left:

- [ ] **Stop redrawing while the display is off.** `display_off()` and
      `system_powersave_on()` still run in the stay_awake path, but the app keeps
      rendering at 30 fps and flipping to a disabled framebuffer. That wastes power,
      and if the downclock is aggressive it may cause audio underruns. Needs a check
      against `system/display.h`.
- [ ] **Verify the elapsed clock survives a sleep/wake cycle.** If the process is
      ever stopped without `stay_awake`, `SDL_GetTicks()` keeps advancing while
      audio does not, and the displayed time jumps by the whole sleep duration.

## musicPlayer: features and performance

- [ ] **Now Playing redraws every frame** while a track runs, because the progress
      bar advances. Once per second would be enough and much cheaper.
- [ ] **Durations are computed for the whole library at startup**, after the first
      frame is drawn. `mp3.h` walks every frame header of every file, so a large
      library on a slow SD card will stall the UI. Make it lazy per-track or move it
      to a background thread.
- [ ] **Flat directory scan.** No recursion into subfolders. GMU also read `.m3u`
      and `.pls` playlists.
- [ ] **No album art**, though `mp3.h` already locates the ID3v2 tag.
- [ ] **No resume-on-launch.** Playback position is not persisted across runs.

## Shared code

- [ ] **`infoPanel.c:97` blits the theme background without clearing first.** Theme
      backgrounds are RGBA and can be partly transparent, so this alpha-blends over
      the previous frame instead of covering it. Latent there because the view never
      changes; it was a real ghosting bug in musicPlayer, fixed in `62406d8f`.

## Toolchain

- [ ] **`Containerfile.toolchain` is not a full replacement for the upstream image.**
      It was reconstructed from `podman history` of a local image and covers the
      cross-compile plus the release pipeline (rsync, 7z, unzip). Upstream also
      installs cmake, sqlite3, libgtest, cppcheck and host SDL dev packages, so
      `make test` and `make static-analysis` will likely fail against this image.
- [ ] **amd64 emulation does not work under podman on macOS.** binfmt registrations
      made in the podman VM do not reach rootless containers, so every amd64
      container fails with "Exec format error". This is why non-amd64 hosts build
      the toolchain natively. Possibly fixed by `podman machine set --rootful`;
      untested. Until then a CI-identical build cannot be reproduced locally.
- [ ] **Deleting a package leaves stale files in `build/`.** `rsync -a` in the
      `$(CACHE)/.setup` target has no `--delete`, so a removed package lingers in an
      existing build tree. Run `make clean` after removing one, or add `--delete`.
