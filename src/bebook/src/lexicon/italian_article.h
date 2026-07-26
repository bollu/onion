#ifndef LEXICON_ITALIAN_ARTICLE_H_
#define LEXICON_ITALIAN_ARTICLE_H_

#include <string>

namespace lexicon
{

// The definite article a noun takes: "il cane", "la cagna", "lo studente", "l'amico".
//
// Gender is what a learner has to memorise per noun, and an article is how a dictionary
// shows it without a grammatical abbreviation to decode. Which article, though, depends on
// the letters the noun starts with as well as its gender -- that is the part worth having
// in one tested place rather than guessed at each call site.
//
// `gender` is "m" or "f". Returns "" when the gender is unknown, so the caller shows the
// bare noun rather than inventing one.
std::string definite_article(const std::string &noun, const std::string &gender);

// The article joined to the noun, elided where Italian elides: "l'amico", not "l' amico".
std::string with_article(const std::string &noun, const std::string &gender);

}

#endif
