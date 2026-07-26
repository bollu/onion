
## Done

- [x] Scroll in books that start with a picture. Up/down now fall back to scrolling when
      there is no word to move to, instead of doing nothing.
- [x] Up/down move one line at a time; L/R jump half a page. Both shoulder pairs work, not
      just the one the keymap selects.
- [x] The peek shows the conjugated meaning: `loro fecero → they made`, with `fare · pass.
      rem.` to the right. The English is inflected to the person and tense of the Italian.
      Only glosses written as "to ..." are conjugated -- the rest are prose about the word
      and are shown as written.
- [x] Wiki breadcrumb, in the title bar: `…› Impero romano › Giulio Cesare`.
- [x] Chapters on X; settings moved to SELECT. X closes the chapter list too.
- [x] L/R switch between a word's different meanings in the dictionary popup ("ne"); the
      d-pad stays on tabs within one meaning.
- [x] Passato remoto: it was in the database but `TENSE_ORDER` never asked for it. Now its
      own tab, and the peek names it.
- [x] The switcher entry reads `Wikipedia (Roma)`. The label in recentlist.json is ignored
      by the switcher -- it captions from the rompath basename -- so the name is built into
      the stub filename.
- [x] B on the top-level wiki list no longer quits the app. START leaves.
- [x] Dates and years are selectable; the cursor no longer skips them.
- [x] Faster movement everywhere: reader cursor and scroll, dictionary keyboard and
      backspace, and the popup body scroll, which was the slowest thing in the app.
- [x] Words split across a line by a hyphen are looked up whole: `con-danna` resolves
      "condanna", not the preposition "con".
- [x] Paragraphs are indented rather than separated by a blank line, which was costing a
      whole line of an eleven-line screen per paragraph.

- [x] Wiki article search on Y, from the article view or the reading list. A prefix walk
      over the ZIM's sorted path list -- no index built, cheap enough per keystroke.
- [x] Headings are bold, so sections are findable while scrolling past.
- [x] Nouns show their gender as an article (`il cane`, `lo studente`, `l'isola`). The
      gender was not in `forms.features` after all -- it is in kaikki's sense tags, which
      the ingest dropped; a `noun_gender` table now carries it for 63,626 nouns.

## Open

- [ ] **Bring back the "forse: ..." suggestion for unknown words, off the render path.**
      Disabled for now because it froze the cursor. `LexiconService::suggest` runs an FTS5
      trigram query over ~142k lemmas and then reranks the candidates by edit distance, and
      `summarize_word` called it synchronously from `TokenView::render` -- so it ran on every
      cursor move onto a word the lexicon does not know, which on a wiki page is most proper
      nouns.

      Both pieces needed are already in the tree:

      1. **Do not ask most of the time.** A capitalised word mid-sentence is a proper noun,
         not a typo, and that is the dominant unknown case in an encyclopedia. Gate on that
         plus a length window before considering a lookup at all. This alone removes most of
         the calls.
      2. **Debounce, then hand it off.** `util/debounced.h` fires once the pokes stop, so a
         cursor sweeping across a line asks for nothing. `util/task_queue.h` then runs the
         query off the frame; the peek shows the bare word until an answer arrives and
         repaints when it does, exactly as the cover loader already does for the library.
      3. **Cache by surface**, as `ws_preview_surface` already does for the preview itself,
         so moving back and forth over the same word costs one query.

      If a single query still blows a 50ms frame after that, `suggest` has to become
      resumable -- a bounded number of candidates scored per call, keeping its heap between
      calls -- but measure before assuming so.

- [ ] **"Roma" peeks as a type of rice.** The city resolves to the lowercase noun `roma`,
      because lemmatize lowercases before matching and takes the first row. Proper nouns
      probably deserve to win when the surface is capitalised mid-sentence.
- [ ] **Wiki section hierarchy.** Headings are bold now, but every level looks the same,
      so a sub-section reads like a section. The TOC already records an indent level per
      heading; nothing uses it while reading.
