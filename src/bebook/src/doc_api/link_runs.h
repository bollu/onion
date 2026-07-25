#ifndef LINK_RUNS_H_
#define LINK_RUNS_H_

#include "./doc_token.h"

#include <cstdint>
#include <vector>

// Restricts `runs` to [offset, offset + length), rebased to the slice. Deliberately not
// text::slice_runs, which normalizes: merging two adjacent links to different targets
// would silently retarget one of them.
std::vector<LinkRun> slice_link_runs(const std::vector<LinkRun> &runs, uint32_t offset, uint32_t length);

// Moves every run right by `delta`, for when text is prepended (a list bullet).
void shift_link_runs(std::vector<LinkRun> &runs, uint32_t delta);

// The first run overlapping [start, end), or nullptr. Overlap rather than containment:
// tokenize_words drops leading punctuation, so a selected word can begin a byte inside
// or outside the link that covers it.
const LinkRun *link_run_overlapping(const std::vector<LinkRun> &runs, uint32_t start, uint32_t end);

#endif
