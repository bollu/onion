#ifndef EDIT_DISTANCE_H_
#define EDIT_DISTANCE_H_

#include <string>

// Levenshtein edit distance between two byte strings, capped at `max`. If the true distance
// exceeds `max`, returns `max + 1` without finishing the computation (each row early-exits
// once its whole row is already worse than `max`). Byte-level: callers wanting character
// semantics should pass already-folded ASCII (which the dictionary's form_fold keys are).
int edit_distance(const std::string &a, const std::string &b, int max);

#endif
