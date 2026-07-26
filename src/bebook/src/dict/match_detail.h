#ifndef DICT_MATCH_DETAIL_H_
#define DICT_MATCH_DETAIL_H_

#include "lexicon/lexicon_service.h"

#include <string>
#include <vector>

// Pure helpers behind the search screen's inline detail panel. Kept free of SDL and of the
// view so they can be tested: the panel shows one tense, and which tense it should be is
// the only real decision in there.
namespace dict
{

// For the tense a form implies, use lexicon::tense_key_for_features -- the reader's peek
// needs it too, and reader/ cannot depend on dict/.

// For which person a form is, use lexicon::person_index_for_features -- this used to carry
// its own copy, which the peek line then needed too.

// The conjugation table matching `key`, or the first available, or null when the lemma has
// none. Never returns a table for a non-verb.
const lexicon::ConjTable *pick_table(
    const std::vector<lexicon::ConjTable> &tables, const std::string &key);

// Whether the full modal has anything the inline panel does not already show. False for
// the common case -- a noun with one analysis, one sense list and no conjugation -- where
// opening it would only add chrome. Drives whether the Y prompt appears at all.
bool has_more_to_show(
    size_t analyses, size_t conj_tables, size_t senses, size_t senses_shown);

}

#endif
