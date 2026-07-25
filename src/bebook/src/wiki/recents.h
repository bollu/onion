#ifndef WIKI_RECENTS_H_
#define WIKI_RECENTS_H_

#include "doc_api/doc_addr.h"

#include <SDL/SDL.h>

#include <string>

// Puts bewiki in the Game Switcher without pretending to be a registered system, the way
// OnionMusic does: the switcher keeps any recentlist entry of type 5 whose launch and
// rompath both exist, and never checks that it is really a rom.
//
// The rompath is a stub file named after the article, because the switcher derives the
// tile's caption from the rompath's basename and ignores the label field entirely.
// file_cleanName turns underscores into spaces, and ZIM paths already use underscores, so
// "Impero_bizantino.wiki" reads as "Impero bizantino" for free.
namespace wiki_recents
{

// Where an article's stub lives. Public so main() can recognise one passed as argv[1].
std::string stub_path_for(const std::string &article_path);

// True if `path` names a stub, i.e. the Game Switcher is resuming us.
bool is_stub_path(const std::string &path);

// Reads a stub written by save(). False if it is missing or malformed.
bool read_stub(const std::string &stub_path, std::string &out_zim_path,
               std::string &out_article_path, DocAddr &out_address);

// Writes the stub, the tile and the recentlist entry, dropping any earlier bewiki entry
// so exactly one tile follows what is being read. `surface` should be the freshly
// rendered top of the article. Replaces the previous stub rather than accumulating.
void save(const std::string &zim_path, const std::string &article_path,
          const std::string &title, DocAddr address, const SDL_Surface *surface);

}

#endif
