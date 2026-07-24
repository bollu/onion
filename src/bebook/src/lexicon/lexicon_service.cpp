#include "./lexicon_service.h"

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
    // ASCII-only lowering. Italian lookup keys are stored lowercased by the build
    // script; accented bytes (UTF-8 multibyte) are left untouched, which matches the
    // build script's str.lower() for the Latin-1 range we use here.
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
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

std::vector<LemmaEntry> LexiconService::lemmatize(const std::string &surface) const
{
    std::vector<LemmaEntry> out;
    if (!impl->db)
    {
        return out;
    }

    static const char *sql =
        "SELECT lemma, pos, features FROM forms WHERE form = ?";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return out;
    }

    std::string key = to_lower(surface);
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
    return out;
}

std::vector<Sense> LexiconService::lookup_it_en(const std::string &lemma) const
{
    std::vector<Sense> out;
    static const char *sql =
        "SELECT sense_no, gloss FROM defs_it_en WHERE lemma = ? ORDER BY sense_no";
    if (!impl->db)
    {
        return out;
    }
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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

std::vector<Sense> LexiconService::lookup_it_it(const std::string &lemma) const
{
    std::vector<Sense> out;
    static const char *sql =
        "SELECT sense_no, gloss FROM defs_it_it WHERE lemma = ? ORDER BY sense_no";
    if (!impl->db)
    {
        return out;
    }
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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

        std::string person, number;
        for (const auto &tok : tokens)
        {
            auto t_it = TENSE_NAME.find(tok);
            if (t_it != TENSE_NAME.end())
            {
                parts.push_back(t_it->second);
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
