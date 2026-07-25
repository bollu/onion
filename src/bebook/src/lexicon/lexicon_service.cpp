#include "./lexicon_service.h"

#include "util/edit_distance.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace lexicon
{

const std::array<const char *, 6> PERSON_LABELS = {
    "io", "tu", "lui/lei", "noi", "voi", "loro"
};

namespace
{

std::string to_lower(const std::string &s)
{
    // Lowercase ASCII and the Latin-1 accented letters (À->à etc.), to match the build
    // script's str.lower() for the range Italian uses. Accents are preserved (only the case
    // changes), so "Città"/"È" still exact-match the stored lowercase accented forms; without
    // the accented-capital handling they would only resolve via the accent-folded fallback.
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size())
        {
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            // Latin-1 uppercase letters À..Þ are 0xC3 0x80..0x9E; lowercase is +0x20. 0x97 in
            // that block is × (multiplication sign), not a letter, so leave it be.
            if (d >= 0x80 && d <= 0x9E && d != 0x97)
            {
                out += static_cast<char>(0xC3);
                out += static_cast<char>(d + 0x20);
            }
            else
            {
                out += static_cast<char>(c);
                out += static_cast<char>(d);
            }
            i += 2;
            continue;
        }
        out += static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
        i += 1;
    }
    return out;
}

// Fold to the accent-insensitive key stored in forms.form_fold: lowercase, and map each
// Latin-1 accented vowel (2-byte UTF-8, lead 0xC3) to its base ASCII letter. Mirrors
// fold() in tools/build_lexicon.py, so a query folds to exactly what the build stored.
// Used only as a fallback when the exact-accent lookup misses, so a book that carries a
// wrong/missing accent ("perche" for "perché") still resolves.
std::string fold_accents(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size())
        {
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            char base = 0;
            switch (d)
            {
                // lowercase à á â ã / uppercase À Á Â Ã
                case 0xA0: case 0xA1: case 0xA2: case 0xA3:
                case 0x80: case 0x81: case 0x82: case 0x83: base = 'a'; break;
                // è é ê ë / È É Ê Ë
                case 0xA8: case 0xA9: case 0xAA: case 0xAB:
                case 0x88: case 0x89: case 0x8A: case 0x8B: base = 'e'; break;
                // ì í î ï / Ì Í Î Ï
                case 0xAC: case 0xAD: case 0xAE: case 0xAF:
                case 0x8C: case 0x8D: case 0x8E: case 0x8F: base = 'i'; break;
                // ò ó ô õ ö / Ò Ó Ô Õ Ö
                case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6:
                case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: base = 'o'; break;
                // ù ú û ü / Ù Ú Û Ü
                case 0xB9: case 0xBA: case 0xBB: case 0xBC:
                case 0x99: case 0x9A: case 0x9B: case 0x9C: base = 'u'; break;
                default: base = 0; break;
            }
            if (base != 0)
            {
                out += base;
            }
            else
            {
                out += static_cast<char>(c);
                out += static_cast<char>(d);
            }
            i += 2;
            continue;
        }
        out += static_cast<char>(std::tolower(c));  // ASCII lowering
        i += 1;
    }
    return out;
}

// Tenses in display order, with human names. Kept in sync with tools/build_lexicon.py.
struct TenseInfo { const char *key; const char *display; };
const TenseInfo TENSE_ORDER[] = {
    {"presente",         "Indicativo Presente"},
    {"imperfetto",       "Imperfetto"},
    {"passato_prossimo", "Passato Prossimo"},
    {"futuro_semplice",  "Futuro Semplice"},
    {"condizionale",     "Condizionale"},
};

std::string column_text(sqlite3_stmt *stmt, int col)
{
    const unsigned char *t = sqlite3_column_text(stmt, col);
    return t ? reinterpret_cast<const char *>(t) : "";
}

} // namespace

struct LexiconService::Impl
{
    sqlite3 *db = nullptr;

    ~Impl()
    {
        if (db)
        {
            sqlite3_close(db);
        }
    }
};

LexiconService::LexiconService(const std::string &db_path)
    : impl(std::make_unique<Impl>())
{
    sqlite3 *db = nullptr;
    int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc == SQLITE_OK)
    {
        impl->db = db;
    }
    else if (db)
    {
        // open_v2 hands back a handle even on failure; release it.
        sqlite3_close(db);
    }
}

LexiconService::~LexiconService() = default;

bool LexiconService::ok() const
{
    return impl->db != nullptr;
}

namespace
{
// Run one lemmatize query (`column` is "form" or "form_fold") with `key`, appending rows.
void query_forms(sqlite3 *db, const char *column, const std::string &key,
                 std::vector<LemmaEntry> &out)
{
    const std::string sql =
        std::string("SELECT lemma, pos, features FROM forms WHERE ") + column + " = ?";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        return;
    }
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        LemmaEntry e;
        e.lemma = column_text(stmt, 0);
        e.pos = column_text(stmt, 1);
        e.features = column_text(stmt, 2);
        e.morphology_human = describe_morphology(e.pos, e.features);
        out.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
}
} // namespace

std::vector<LemmaEntry> LexiconService::lemmatize(const std::string &surface) const
{
    std::vector<LemmaEntry> out;
    if (!impl->db)
    {
        return out;
    }

    // Exact match on the accented form first.
    query_forms(impl->db, "form", to_lower(surface), out);

    // Only if nothing matched, fall back to the accent-folded key, so a book with a wrong
    // or missing accent still resolves. Exact matches always win when they exist.
    if (out.empty())
    {
        query_forms(impl->db, "form_fold", fold_accents(surface), out);
    }
    return out;
}

namespace
{
// Senses for `lemma` from a definitions table ("defs_it_en" / "defs_it_it"). `table` is a
// fixed internal identifier, not user input.
std::vector<Sense> lookup_defs(sqlite3 *db, const char *table, const std::string &lemma)
{
    std::vector<Sense> out;
    if (!db)
    {
        return out;
    }
    const std::string sql =
        std::string("SELECT sense_no, gloss FROM ") + table + " WHERE lemma = ? ORDER BY sense_no";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        return out;
    }
    sqlite3_bind_text(stmt, 1, lemma.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.push_back({sqlite3_column_int(stmt, 0), column_text(stmt, 1)});
    }
    sqlite3_finalize(stmt);
    return out;
}
} // namespace

std::vector<Sense> LexiconService::lookup_it_en(const std::string &lemma) const
{
    return lookup_defs(impl->db, "defs_it_en", lemma);
}

std::vector<Sense> LexiconService::lookup_it_it(const std::string &lemma) const
{
    return lookup_defs(impl->db, "defs_it_it", lemma);
}

std::vector<ConjTable> LexiconService::conjugations(const std::string &lemma) const
{
    std::vector<ConjTable> out;
    if (!impl->db)
    {
        return out;
    }

    static const char *sql =
        "SELECT person, form FROM conj WHERE lemma = ? AND tense = ? ORDER BY person";

    for (const auto &ti : TENSE_ORDER)
    {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            continue;
        }
        sqlite3_bind_text(stmt, 1, lemma.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, ti.key, -1, SQLITE_STATIC);

        ConjTable table;
        table.tense = ti.key;
        table.display_name = ti.display;
        bool any = false;
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            int person = sqlite3_column_int(stmt, 0);
            if (person >= 0 && person < 6)
            {
                table.forms[person] = column_text(stmt, 1);
                any = true;
            }
        }
        sqlite3_finalize(stmt);

        if (any)
        {
            out.push_back(std::move(table));
        }
    }
    return out;
}

std::vector<Suggestion> LexiconService::suggest(const std::string &surface, int max_results) const
{
    std::vector<Suggestion> out;
    if (!impl->db)
    {
        return out;
    }

    const std::string q = fold_accents(surface);
    if (q.size() < 3)  // the trigram tokenizer needs at least three characters
    {
        return out;
    }

    // OR of the query's overlapping trigrams: FTS5 returns words sharing any of them, ranked
    // by how many they share, so a wrong first (or last) letter still finds the word. Each
    // trigram is quoted so the tokenizer treats it as one token; skip any with a quote byte.
    std::string match;
    for (size_t i = 0; i + 3 <= q.size(); ++i)
    {
        const std::string g = q.substr(i, 3);
        if (g.find('"') != std::string::npos)
        {
            continue;
        }
        if (!match.empty())
        {
            match += " OR ";
        }
        match += '"';
        match += g;
        match += '"';
    }
    if (match.empty())
    {
        return out;
    }

    static const char *sql =
        "SELECT fold, lemma FROM vocab WHERE vocab MATCH ? ORDER BY rank LIMIT 60";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return out;  // no `vocab` table (old DB / FTS5 not built): degrade to no suggestions
    }
    sqlite3_bind_text(stmt, 1, match.c_str(), -1, SQLITE_TRANSIENT);

    // Rerank the trigram candidates by actual edit distance and keep the nearest per lemma.
    const int max_dist = 3;
    std::unordered_map<std::string, Suggestion> best;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        std::string fold = column_text(stmt, 0);
        std::string lemma = column_text(stmt, 1);
        const int d = edit_distance(q, fold, max_dist);
        if (d > max_dist)
        {
            continue;
        }
        auto it = best.find(lemma);
        if (it == best.end() || d < it->second.distance)
        {
            best[lemma] = Suggestion{lemma, std::move(fold), d};
        }
    }
    sqlite3_finalize(stmt);

    for (auto &kv : best)
    {
        out.push_back(kv.second);
    }
    std::sort(out.begin(), out.end(), [](const Suggestion &a, const Suggestion &b) {
        if (a.distance != b.distance) return a.distance < b.distance;
        if (a.matched.size() != b.matched.size()) return a.matched.size() < b.matched.size();
        return a.lemma < b.lemma;
    });
    if (static_cast<int>(out.size()) > max_results)
    {
        out.resize(static_cast<size_t>(max_results));
    }
    return out;
}

std::string describe_morphology(const std::string &pos, const std::string &features)
{
    // Italian grammatical names, to match the conjugation tab labels and what the learner
    // sees in Italian references.
    static const std::unordered_map<std::string, std::string> POS_NAME = {
        {"VER", "verbo"}, {"NOUN", "sostantivo"}, {"ADJ", "aggettivo"}, {"ADV", "avverbio"},
        {"PRO", "pronome"}, {"PRE", "preposizione"}, {"CON", "congiunzione"},
        {"ART", "articolo"}, {"DET", "determinante"},
    };
    // Tense/mood/non-finite feature codes. The bulk (kaikki) build registers every
    // inflected form for lemmatization, so besides the four surfaced indicative tenses the
    // header may see subjunctive/imperative/past-historic and the non-finite forms. Codes
    // combine with '+' (e.g. "sub+impf+1+s" -> "subjunctive imperfect io"); unknown tokens
    // are ignored, so this degrades gracefully.
    static const std::unordered_map<std::string, std::string> TENSE_NAME = {
        {"pres", "presente"}, {"impf", "imperfetto"}, {"fut", "futuro"},
        {"cond", "condizionale"}, {"inf", "infinito"},
        {"sub", "congiuntivo"}, {"impr", "imperativo"}, {"rem", "passato remoto"},
        {"ger", "gerundio"}, {"part", "participio"},
    };

    std::vector<std::string> parts;

    auto pos_it = POS_NAME.find(pos);
    if (pos_it != POS_NAME.end())
    {
        parts.push_back(pos_it->second);
    }
    else if (!pos.empty())
    {
        parts.push_back(pos);
    }

    // features are '+'-joined: a tense code then person/number, e.g. "impf+1+s".
    // "base" and empty carry no extra morphology.
    if (features != "base" && !features.empty())
    {
        std::vector<std::string> tokens;
        std::string cur;
        for (char c : features)
        {
            if (c == '+')
            {
                if (!cur.empty()) tokens.push_back(cur);
                cur.clear();
            }
            else
            {
                cur += c;
            }
        }
        if (!cur.empty()) tokens.push_back(cur);

        // Gender/number for non-verb inflections (noun plurals, adjective forms). Tokens are
        // deliberately distinct from the verb person/number tokens (1/2/3, s/p) above.
        static const std::unordered_map<std::string, std::string> GENDER_NUMBER = {
            {"f", "femminile"}, {"m", "maschile"},
            {"sg", "singolare"}, {"pl", "plurale"},
        };

        std::string person, number;
        for (const auto &tok : tokens)
        {
            auto t_it = TENSE_NAME.find(tok);
            auto g_it = GENDER_NUMBER.find(tok);
            if (t_it != TENSE_NAME.end())
            {
                parts.push_back(t_it->second);
            }
            else if (g_it != GENDER_NUMBER.end())
            {
                parts.push_back(g_it->second);
            }
            else if (tok == "1" || tok == "2" || tok == "3")
            {
                person = tok;
            }
            else if (tok == "s" || tok == "p")
            {
                number = tok;
            }
        }

        if (!person.empty() && !number.empty())
        {
            int idx = (person[0] - '1') + (number == "p" ? 3 : 0);
            if (idx >= 0 && idx < 6)
            {
                parts.push_back(PERSON_LABELS[idx]);
            }
        }
    }

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i) result += " \xC2\xB7 ";  // " · " (U+00B7) in UTF-8
        result += parts[i];
    }
    return result;
}

} // namespace lexicon
