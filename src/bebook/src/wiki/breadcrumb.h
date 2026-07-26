#ifndef WIKI_BREADCRUMB_H_
#define WIKI_BREADCRUMB_H_

#include <functional>
#include <string>
#include <vector>

namespace wiki
{

// The trail of articles followed to get here, as one line for the title bar:
//
//   …› Impero romano › Giulio Cesare
//
// Built from the end backwards, prepending ancestors while they fit, so the article you are
// actually in is never the thing that gets cut. A leading "…›" says the trail is longer than
// the line. `width_of` measures a candidate, which keeps this free of the font engine and
// testable -- the same shape as reader/conj_layout.h.
//
// `trail` is oldest-first and includes the current article as its last element. Returns ""
// for an empty trail.
std::string breadcrumb(
    const std::vector<std::string> &trail,
    int avail,
    const std::function<int(const std::string &)> &width_of);

}

#endif
