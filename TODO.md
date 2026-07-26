
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

## Open

- [ ] **Wiki article search, on Y.** Y is free in the article view and the reading list.
      The dictionary app's on-screen keyboard and its accent-folding search are the parts
      to reuse; the ZIM's title index is what to search against.
- [ ] **Wiki rendering still feels unstructured.** The paragraph indent helped, but
      headings are only centred -- they do not read as headings. Worth giving them weight,
      and considering whether the section hierarchy should show at all.
- [ ] **Noun gender in the peek and the dictionary.** A noun reads `cane · sost.`, which
      says nothing about gender -- the one thing a learner has to memorise per noun. Render
      it with its definite article (`il cane`, `la cagna`). The gender is already in
      `forms.features`; the work is choosing il/lo/la/l', which depends on the initial
      letters as well as the gender.
- [ ] **"Roma" peeks as a type of rice.** The city resolves to the lowercase noun `roma`,
      because lemmatize lowercases before matching and takes the first row. Proper nouns
      probably deserve to win when the surface is capitalised mid-sentence.
