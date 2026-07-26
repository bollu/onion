#ifndef LEXICON_ENGLISH_INFLECT_H_
#define LEXICON_ENGLISH_INFLECT_H_

#include <string>

namespace lexicon
{

// Render an English gloss in the person and tense of the Italian form the reader is looking
// at: "to make" + loro + passato remoto -> "they made".
//
// The point is that a bare "to make" beside "fecero" leaves the reader to do the conjugating
// in their head, which is the very thing they are reading in order to learn. Seeing "they
// made" next to "loro fecero" teaches the pairing directly.
//
// `gloss` is a dictionary gloss, with or without the leading "to". Multi-word glosses keep
// their tail ("to bring about" -> "they brought about"): only the first word inflects.
//
// `person` is 0..5 (io, tu, lui/lei, noi, voi, loro); `tense_key` is a ConjTable::tense
// value. Returns "" when the pairing cannot be rendered -- no person, an unknown tense, or
// an empty gloss -- so the caller can fall back to the plain gloss.
std::string conjugate_gloss(const std::string &gloss, int person, const std::string &tense_key);

// The pieces, exposed for testing. English is irregular in exactly the places that matter
// here (be/have/do/make/go...), so each is a small table plus a regular rule.
std::string english_past(const std::string &verb);
std::string english_participle(const std::string &verb);
std::string english_third_person(const std::string &verb);
std::string english_gerund(const std::string &verb);

}

#endif
