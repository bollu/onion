# bebook TODO

Status as of the WP5 milestone. WP0–WP5 of
`~/.claude/plans/i-want-to-improve-enumerated-cupcake.md` are done and committed on
branch `bebook`; 103 tests pass; the app builds and runs natively on macOS and
cross-compiles for the device.

Nothing here has been run on real hardware yet.

---

## Next up

### Planned — English → Italian direction for BeDict (not yet implemented)

Add a reverse (EN→IT) lookup to the standalone BeDict app. **Chosen approach: reverse the
English glosses we already ingest** — no new download. Full plan (design, files, verification)
in `~/.claude/plans/please-plan-a-feature-parsed-teapot.md`. Summary:

- **Build** (`tools/build_lexicon.py`): new table `defs_en_it(en_term, en_fold, it_lemma, gloss)`
  + `idx_defs_en_it_fold`; populate it alongside the existing `defs_it_en` insert (line ~686) via
  a helper `english_terms_from_gloss(gloss)` that strips `(qualifiers)`, splits on `,`/`;`, drops
  long phrases, adds a `"to "`-stripped variant. Optional `vocab_en` FTS5 trigram for EN fuzzy.
- **Service** (`src/lexicon/lexicon_service.{h,cpp}`): add `SearchHit::note` (English gloss shown
  as the secondary line) and `search_en(query, max)` mirroring `search()`'s exact/prefix/fuzzy
  tiers against `defs_en_it`; each hit's `word` is the Italian lemma, so it opens the existing
  Italian entry (glosses + conjugation).
- **UI** (`src/dict/views/search_view.{h,cpp}`): `Direction {ItEn, EnIt}` member, **`X` toggles**,
  an `IT→EN`/`EN→IT` badge on the search bar, `refresh_results()` dispatches on direction. Keyboard
  unchanged (ASCII suits English). Reader popup stays IT→EN by design.
- **Data**: rebuild `resources/italian.sqlite` (761MB kaikki input present locally) and recompress;
  the reverse table rides along, no packaging change. Tests: `search_en("dog")` → `cane`.

### WP6 — OnionOS `EBOOK` system

Built and packaged; **not yet verified on hardware**.

- [x] `Emu/EBOOK/config.json` + `launch.sh` and an empty `Roms/EBOOK/Imgs/` are
      emitted by `create_packages.sh` alongside the App package.
- [x] `launch.sh` `cd`s to `/mnt/SDCARD/App/BeBook` before exec'ing, since `bebook.cfg`
      and `resources/fonts` resolve relative to the working directory.
- [x] Box art written to `Roms/EBOOK/Imgs/<basename>.png` during indexing, only when
      absent so a scraped or hand-made cover survives. Verified across 8 books, all
      within MainUI's 250x360 limit.
- [ ] **Verify on device**: a `Books` system appears under Games; launching a book
      opens it at the right page; **the book then appears in OnionOS Recents with its
      cover**; relaunching from Recents resumes the reading position.
- [ ] Confirm the bundled `libfreetype.so.6` is the one that loads, rather than the
      firmware's, given `LD_LIBRARY_PATH=./lib` is set before it.

### Deeper Onion integration

bebook now lives in the Onion tree at `src/bebook`, built by `make bebook` and
preinstalled by `make apps`. See `INTEGRATION.md` for what remains.

- [x] Vendored into `src/bebook` rather than kept as a submodule, so it can be worked on
      alongside Onion and eventually share code with it.
- [x] Own Makefile retained, as `third-party/DinguxCommander` does; `src/common/config.mk`
      cannot express what the build needs.
- [x] bebook's Dockerfile dropped in favour of Onion's `Containerfile.toolchain` — they
      were the same recipe and collided on image name.
- [ ] Decide whether bebook should adopt Onion's theme renderer. It currently does
      not, and adopting it is the largest single item in the integration.

---

## Known gaps and rough edges

### Typography

- [ ] **Justified spacing reaches 180% of natural space at worst.** Measured, not
      guessed: worst stretch ratio 1.59 on a 584px column. Acceptable and better than
      several commercial readers, but the tuning was stopped rather than finished. The
      levers are `pretolerance`/`tolerance` in `BreakOptions` and the glue
      stretch/shrink proportions in `build_items`.
- [ ] **Paragraph indent is wired but set to 0** (`PARAGRAPH_INDENT_PERCENT` in
      `reader/config.h`). Paragraphs currently separate by blank line, which is web
      convention rather than book convention. Switching means suppressing the empty
      `TextDocToken` the parser emits as a separator.
- [ ] **No hanging punctuation / optical margin alignment.** A line starting with a
      quote mark visibly indents relative to its neighbours.
- [ ] **No paged mode with widow/orphan control.** Reading is line-scrolled, so page
      boundaries fall wherever they fall. The plan called for this; it was not reached.
- [ ] **Hyphenation is en-US only.** `hyphenate_word` is language-blind and the
      patterns are compiled in. A non-English book gets hyphenated by English rules.
      The `HyphenateFn` seam in `line_break.h` exists to fix this.
- [ ] **`right_hyphen_min` disagreement with the TeXbook is pinned in tests.**
      `computer` hyphenates as `com-puter`, not Knuth's illustrative `com-put-er`,
      because `righthyphenmin=3` forbids leaving `er`. Documented in
      `src/text/tests/hyphenate_test.cpp`; revisit only with evidence.
- [ ] **`democratic` follows an upstream pattern bug** (`de-mo-c-ra-tic`), pinned so a
      pattern refresh surfaces as a test failure rather than a silent change.

### Text engine

- [ ] **Variable fonts are unsupported.** Google Fonts has migrated its entire
      library to variable fonts, so Literata, Source Serif 4, EB Garamond, Lora,
      Crimson Pro and Bitter no longer ship static instances and cannot currently be
      used. Supporting FreeType named instances (`FT_Get_MM_Var`, face index
      `(instance << 16) | index`) would reopen all of them. This is why the default is
      Charis SIL.
- [ ] **Stem darkening is implemented but unused** (`GlyphAtlas::set_stem_darkening`).
      Worth evaluating on the real panel, where linear-light compositing can leave text
      looking thin on dark backgrounds.
- [ ] **Hinting is not a user setting.** It is fixed at `FT_LOAD_TARGET_LIGHT`, which
      is the right default at ~285 DPI, but the atlas already supports None/Light/Normal.
- [ ] **No per-language shaping features.** HarfBuzz is called with no feature list, so
      e.g. oldstyle figures (`onum`) or small caps (`smcp`) are unavailable.

### Library and covers

- [ ] **Cover art is only read from the epub.** No fallback to a sibling `cover.jpg`,
      and no way to override one.
- [ ] **The grid does not scroll past two rows visibly.** `grid_scroll_row` keeps the
      selection on screen but there is no scrollbar or position indicator, so a large
      library gives no sense of where you are.
- [ ] **`.txt` files get no metadata.** `TxtReader::load_resource` returns nothing, so
      they always show a placeholder plate with the filename.
- [ ] **Sort order is fixed** (alphabetical by title). No author/recent/series sort,
      no search. The plan listed these under "full library manager", which was not the
      chosen option.

### Robustness

- [ ] **`epub_metadata.cpp` leaks every `xmlGetProp` result.** Pre-existing upstream;
      noticed during WP4 but out of scope. Now more material, since the library
      indexer parses every book's OPF.
- [ ] **`epub_reader.cpp` may call `strlen` on an empty vector's `.data()`** when a
      book has an empty NCX or nav document (`epub_reader.cpp:137` and `:160`).
      Pre-existing.
- [ ] **No percent-decoding of hrefs.** A cover or image whose manifest href contains
      `%20` will not be found.
- [ ] **Sustained-FPS check on device has not been done.** The plan asks for 20 FPS
      while page-scrolling justified, hyphenated, mixed-italic text, and for a cold
      scan of ~30 books not to block input. Both are plausible but unverified.

### Build and packaging

- [ ] **Only the Onion package is exercised.** `create_packages.sh` still emits a
      MiniUI `.pak`, but nothing about it has been tested since the fork.
- [ ] **Charis SIL adds ~3.4MB to the package** (four faces). Subsetting to Latin
      would cut most of that if size becomes a concern.
- [ ] **`union-miyoomini-toolchain` submodule is now unused.** bebook builds with its
      own `Containerfile`; the submodule can be dropped, more so if we adopt Onion's.

---

## Deliberately not done

- **A settings UI for justification, hyphenation and leading.** The settings are
  persisted and take effect, but `settings_view.cpp` has no entries for them, so they
  can currently only be changed by editing the store.
- **Migration of PixelReader reading positions.** A clean break was chosen; the book
  id also changed when the `zip_utils` size bug was fixed, so old ids would not match
  anyway.
