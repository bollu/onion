#include "./recents.h"

#include "util/rom_screen.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{

const char *STUB_DIR = "/mnt/SDCARD/App/BeWiki/current";
const char *LAUNCH_PATH = "/mnt/SDCARD/App/BeWiki/launch.sh";

const char *RECENTLIST_PATH = "/mnt/SDCARD/Roms/recentlist.json";
const char *RECENTLIST_HIDDEN_PATH = "/mnt/SDCARD/Roms/recentlist-hidden.json";

const char *STUB_SUFFIX = ".wiki";

// Hidden wins when present: runtime.sh parks the list there while Recents are switched
// off, and a writer that ignores it produces an entry nobody can see.
std::string recents_path()
{
    std::error_code ec;
    if (std::filesystem::exists(RECENTLIST_HIDDEN_PATH, ec))
    {
        return RECENTLIST_HIDDEN_PATH;
    }
    return RECENTLIST_PATH;
}

// JSON string escaping, enough for the article titles and paths that reach here.
std::string json_escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        if (c == '"' || c == '\\')
        {
            out.push_back('\\');
            out.push_back(c);
        }
        else if (static_cast<unsigned char>(c) >= 0x20)
        {
            out.push_back(c);
        }
    }
    return out;
}

// Article paths carry slashes and colons on occasion; neither survives as a filename.
std::string sanitize(const std::string &article_path)
{
    std::string out;
    out.reserve(article_path.size());
    for (char c : article_path)
    {
        out.push_back((c == '/' || c == '\\' || c == ':') ? '_' : c);
    }
    return out;
}

// Also removes the Game Switcher tile that went with each stub. Those are named by a hash
// of the stub path in a different directory, so nothing else would ever collect them --
// browsing 200 articles used to leave 200 orphaned 640x480 PNGs on the card.
void remove_other_stubs(const std::string &keep)
{
    // Collected before deleting: removing entries while readdir is in flight is
    // unspecified and can skip files. The iterator is also advanced explicitly, because
    // range-for uses the throwing operator++ and a mid-scan failure would take the app
    // down during a routine tile save.
    std::vector<std::filesystem::path> stale;
    std::error_code ec;

    auto it = std::filesystem::directory_iterator(STUB_DIR, ec);
    const auto end = std::filesystem::directory_iterator();
    while (!ec && it != end)
    {
        const std::string path = it->path().string();
        if (path != keep && wiki_recents::is_stub_path(path))
        {
            stale.push_back(it->path());
        }
        it.increment(ec);
    }

    for (const auto &path : stale)
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        remove_rom_screen(path.string());
    }
}

}

namespace wiki_recents
{

std::string stub_path_for(const std::string &article_path)
{
    return std::string(STUB_DIR) + "/" + sanitize(article_path) + STUB_SUFFIX;
}

bool is_stub_path(const std::string &path)
{
    const std::string suffix = STUB_SUFFIX;
    return path.size() > suffix.size() &&
           path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool read_stub(const std::string &stub_path, std::string &out_zim_path,
               std::string &out_article_path, DocAddr &out_address)
{
    std::ifstream file(stub_path);
    if (!file)
    {
        return false;
    }

    std::string zim_line;
    std::string article_line;
    std::string address_line;
    if (!std::getline(file, zim_line) || !std::getline(file, article_line))
    {
        return false;
    }
    std::getline(file, address_line);

    if (article_line.empty())
    {
        return false;
    }

    out_zim_path = zim_line;
    out_article_path = article_line;
    out_address = address_line.empty()
        ? 0
        : static_cast<DocAddr>(std::strtoull(address_line.c_str(), nullptr, 10));
    return true;
}

void save(const std::string &zim_path, const std::string &article_path,
          const std::string &title, DocAddr address, const SDL_Surface *surface)
{
    if (article_path.empty())
    {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(STUB_DIR, ec);

    // The stub is both the rompath the switcher requires to exist and the resume state
    // it hands back as argv[1].
    const std::string stub = stub_path_for(article_path);
    {
        std::ofstream out(stub, std::ios::trunc);
        if (!out)
        {
            return;
        }
        out << zim_path << "\n" << article_path << "\n" << address << "\n";
    }
    remove_other_stubs(stub);

    if (surface != nullptr)
    {
        write_rom_screen_plain(surface, stub);
    }

    std::ostringstream entry;
    entry << "{\"label\":\"" << json_escape(title.empty() ? article_path : title)
          << "\",\"launch\":\"" << LAUNCH_PATH
          << "\",\"type\":5,\"rompath\":\"" << json_escape(stub) << "\"}";

    // Rewrite with ours on top and any earlier bewiki line dropped, so Recents holds one
    // tile that follows what is being read rather than one per article.
    const std::string path = recents_path();
    std::vector<std::string> kept;
    {
        std::ifstream in(path);
        std::string line;
        while (in && kept.size() < 64 && std::getline(in, line))
        {
            if (line.size() > 1 && line.find(LAUNCH_PATH) == std::string::npos)
            {
                kept.push_back(line);
            }
        }
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        return;
    }
    out << entry.str() << "\n";
    for (const auto &line : kept)
    {
        out << line << "\n";
    }
}

}
