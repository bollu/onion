#include "./lexicon_service.h"

#include "util/budget.h"
#include "util/edit_distance.h"
#include "util/job.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

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
    {"passato_remoto",   "Passato Remoto"},
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

namespace
{

// The suggestion search, as a state machine that can stop between rows.
//
// The query is deliberately unranked. "ORDER BY rank" made FTS5 score every match before it
// could return the first row -- measured at 57-101ms on a desktop for one word, so far worse
// on the device -- and that is one indivisible lump no amount of slicing divides. Streaming
// in rowid order costs 0.2ms to the first row and spreads the rest a row at a time, which is
// exactly what a budget can cut. The ordering was thrown away anyway: every candidate is
// reranked by edit distance below.
class SuggestJob : public Job
{
public:
    SuggestJob(sqlite3 *db, const std::string &surface, int max_results,
               std::function<void(std::vector<Suggestion>)> on_done)
        : db(db), max_results(max_results), on_done(std::move(on_done))
    {
        query = fold_accents(surface);
    }

    ~SuggestJob() override
    {
        // A job is dropped the moment its answer stops being wanted, mid-scan and without
        // warning, so the statement is released here rather than at the end of the scan.
        if (stmt != nullptr)
        {
            sqlite3_finalize(stmt);
        }
    }

    bool step(Budget &budget) override
    {
        if (phase == Phase::Prepare)
        {
            if (!prepare())
            {
                finish();
                return true;
            }
            phase = Phase::Scan;
        }

        if (phase == Phase::Scan)
        {
            while (!budget.spent())
            {
                if (scanned >= MAX_ROWS || sqlite3_step(stmt) != SQLITE_ROW)
                {
                    phase = Phase::Finish;
                    break;
                }
                ++scanned;
                consider_row();
            }
            if (phase != Phase::Finish)
            {
                return false;  // out of budget, more rows to come
            }
        }

        finish();
        return true;
    }

private:
    enum class Phase { Prepare, Scan, Finish };

    // A runaway guard, not a quality knob. Capping lower does not work: the rows arrive in
    // rowid order, which is uncorrelated with how good a candidate is, so "the first N" is
    // an arbitrary subset -- for "bniversita" the right answer sits at row 26,944 of 29,163.
    // Recall requires scanning them all, and the frame budget is what keeps that free.
    static const int MAX_ROWS = 200000;

    bool prepare()
    {
        if (db == nullptr || query.size() < 3)  // the trigram tokenizer needs three
        {
            return false;
        }

        // OR of the query's overlapping trigrams: FTS5 returns words sharing any of them, so
        // a wrong first or last letter still finds the word. Each is quoted so the tokenizer
        // treats it as one token; any carrying a quote byte is skipped.
        std::string match;
        for (size_t i = 0; i + 3 <= query.size(); ++i)
        {
            const std::string g = query.substr(i, 3);
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
            return false;
        }

        static const char *sql = "SELECT fold, lemma FROM vocab WHERE vocab MATCH ?";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            // No `vocab` table (an old DB, or FTS5 not built): degrade to no suggestions.
            stmt = nullptr;
            return false;
        }
        sqlite3_bind_text(stmt, 1, match.c_str(), -1, SQLITE_TRANSIENT);
        return true;
    }

    void consider_row()
    {
        const char *fold_c = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        if (fold_c == nullptr)
        {
            return;
        }
        // Edit distance is at least the difference in length, so this rejects most rows
        // without running it -- and running it is the expensive part of a row.
        const size_t fold_len = static_cast<size_t>(sqlite3_column_bytes(stmt, 0));
        const size_t q_len = query.size();
        const size_t diff = fold_len > q_len ? fold_len - q_len : q_len - fold_len;
        if (diff > static_cast<size_t>(MAX_DIST))
        {
            return;
        }

        std::string fold(fold_c, fold_len);
        const int d = edit_distance(query, fold, MAX_DIST);
        if (d > MAX_DIST)
        {
            return;
        }

        const char *lemma_c = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        std::string lemma = lemma_c != nullptr ? lemma_c : "";
        auto it = best.find(lemma);
        if (it == best.end() || d < it->second.distance)
        {
            best[lemma] = Suggestion{lemma, std::move(fold), d};
        }
    }

    void finish()
    {
        std::vector<Suggestion> out;
        out.reserve(best.size());
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
        if (on_done)
        {
            on_done(std::move(out));
        }
    }

    static const int MAX_DIST = 3;

    sqlite3 *db = nullptr;
    std::string query;
    int max_results = 6;
    std::function<void(std::vector<Suggestion>)> on_done;

    Phase phase = Phase::Prepare;
    sqlite3_stmt *stmt = nullptr;
    int scanned = 0;
    std::unordered_map<std::string, Suggestion> best;
};

}

std::unique_ptr<Job> LexiconService::make_suggest_job(
    const std::string &surface, int max_results,
    std::function<void(std::vector<Suggestion>)> on_done) const
{
    return std::unique_ptr<Job>(
        new SuggestJob(impl->db, surface, max_results, std::move(on_done)));
}

std::vector<Suggestion> LexiconService::suggest(const std::string &surface, int max_results) const
{
    // The blocking form, for callers that want an answer now -- the popup's "did you mean"
    // opens on demand and has a whole frame to itself. Implemented by running the very same
    // job to completion, so there is one algorithm rather than two that can disagree.
    std::vector<Suggestion> out;
    auto job = make_suggest_job(surface, max_results,
                                [&out](std::vector<Suggestion> r) { out = std::move(r); });
    Budget budget = Budget::unlimited();
    job->step(budget);
    return out;
}

std::vector<SearchHit> LexiconService::search(const std::string &query, int max_results) const
{
    std::vector<SearchHit> out;
    if (!impl->db)
    {
        return out;
    }
    const std::string q = fold_accents(query);
    if (q.empty())
    {
        return out;
    }

    std::unordered_set<std::string> seen;
    auto add = [&](std::string word, std::string lemma) {
        // Skip multiword entries (idioms like "far battere il cuore"): the app searches
        // single words, and they only clutter the incremental list.
        if (word.find(' ') != std::string::npos)
        {
            return;
        }
        std::string key = word + '\x01' + lemma;
        if (seen.insert(key).second)
        {
            out.push_back(SearchHit{ std::move(word), std::move(lemma) });
        }
    };

    // Tier 1: words folding exactly to the query, including conjugated forms (query "faro"
    // finds both "faro" the noun and "farò" of fare). Shortest surface first.
    {
        static const char *sql =
            "SELECT form, lemma FROM forms WHERE form_fold = ? ORDER BY length(form), form LIMIT 12";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(impl->db, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, q.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                add(column_text(stmt, 0), column_text(stmt, 1));
            }
            sqlite3_finalize(stmt);
        }
    }

    // Tier 2: headword lemmas whose folded form starts with the query, via the form_fold index
    // range-scan (natural alphabetical order), so incremental typing browses lemmas.
    if (static_cast<int>(out.size()) < max_results)
    {
        std::string hi = q;
        hi.back() = static_cast<char>(hi.back() + 1);  // exclusive prefix upper bound (ASCII fold)
        static const char *sql =
            "SELECT form, lemma FROM forms "
            "WHERE form_fold >= ? AND form_fold < ? AND features IN ('inf','base') LIMIT ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(impl->db, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, q.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, hi.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 3, max_results);
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                add(column_text(stmt, 0), column_text(stmt, 1));
            }
            sqlite3_finalize(stmt);
        }
    }

    // Tier 3: still thin -> fuzzy "did you mean" over lemmas.
    if (static_cast<int>(out.size()) < 5 && q.size() >= 3)
    {
        for (const auto &s : suggest(query, max_results))
        {
            add(s.lemma, s.lemma);
        }
    }

    if (static_cast<int>(out.size()) > max_results)
    {
        out.resize(static_cast<size_t>(max_results));
    }
    return out;
}

std::string LexiconService::noun_gender(const std::string &lemma) const
{
    if (!impl->db)
    {
        return "";
    }
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(impl->db, "SELECT gender FROM noun_gender WHERE lemma = ?",
                           -1, &stmt, nullptr) != SQLITE_OK)
    {
        // An older DB has no such table. Absent gender is a supported state, so this is
        // silent rather than an error the caller has to handle.
        return "";
    }
    sqlite3_bind_text(stmt, 1, lemma.c_str(), -1, SQLITE_TRANSIENT);
    std::string out;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out = column_text(stmt, 0);
    }
    sqlite3_finalize(stmt);
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

namespace
{
// '+'-joined feature codes, e.g. "sub+impf+1+s" -> {sub, impf, 1, s}.
std::vector<std::string> split_feature_tokens(const std::string &features)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : features)
    {
        if (c == '+')
        {
            if (!cur.empty()) { out.push_back(cur); }
            cur.clear();
        }
        else { cur += c; }
    }
    if (!cur.empty()) { out.push_back(cur); }
    return out;
}

bool has_token(const std::vector<std::string> &t, const char *want)
{
    for (const auto &s : t) { if (s == want) { return true; } }
    return false;
}
}

int person_index_for_features(const std::string &features)
{
    const std::vector<std::string> t = split_feature_tokens(features);
    int person = -1;
    if (has_token(t, "1")) { person = 0; }
    else if (has_token(t, "2")) { person = 1; }
    else if (has_token(t, "3")) { person = 2; }
    if (person < 0) { return -1; }
    return has_token(t, "p") ? person + 3 : person;
}

std::string tense_name_for_features(const std::string &features)
{
    const std::vector<std::string> t = split_feature_tokens(features);

    // Mood first, since it qualifies the tense: a subjunctive imperfect is "congiuntivo
    // imperfetto", not "imperfetto".
    std::string out;
    if (has_token(t, "sub"))       { out = "congiuntivo"; }
    else if (has_token(t, "impr")) { return "imperativo"; }

    auto add = [&out](const char *s) {
        if (!out.empty()) { out += " "; }
        out += s;
    };

    if (has_token(t, "pres"))      { add("presente"); }
    else if (has_token(t, "impf")) { add("imperfetto"); }
    else if (has_token(t, "rem"))  { add("passato remoto"); }
    else if (has_token(t, "fut"))  { add("futuro"); }
    else if (has_token(t, "cond")) { add("condizionale"); }
    else if (has_token(t, "part")) { add("participio"); }
    else if (has_token(t, "ger"))  { add("gerundio"); }
    else if (has_token(t, "inf"))  { add("infinito"); }

    return out;
}

std::string tense_key_for_features(const std::string &features)
{
    const std::vector<std::string> t = split_feature_tokens(features);

    // Only the indicative has tables, so a subjunctive or imperative form has no tense of
    // its own to show and falls back to the present.
    if (has_token(t, "sub") || has_token(t, "impr"))
    {
        return "presente";
    }

    if (has_token(t, "impf")) { return "imperfetto"; }
    if (has_token(t, "rem"))  { return "passato_remoto"; }
    if (has_token(t, "fut"))  { return "futuro_semplice"; }
    if (has_token(t, "cond")) { return "condizionale"; }

    // A participle is the half of a compound tense the reader is most likely holding.
    if (has_token(t, "part")) { return "passato_prossimo"; }

    return "presente";
}

std::string abbreviate_morphology(const std::string &pos, const std::string &features)
{
    const std::vector<std::string> t = split_feature_tokens(features);

    // Mood first, since it qualifies the tense: a subjunctive imperfect is "cong. imperf.",
    // not "imperf.".
    std::string out;
    if (has_token(t, "sub"))       { out = "cong."; }
    else if (has_token(t, "impr")) { return "imper."; }

    auto add = [&out](const char *s) {
        if (!out.empty()) { out += " "; }
        out += s;
    };

    if (has_token(t, "pres"))      { add("pres."); }
    else if (has_token(t, "impf")) { add("imperf."); }
    else if (has_token(t, "rem"))  { add("pass. rem."); }
    else if (has_token(t, "fut"))  { add("fut."); }
    else if (has_token(t, "cond")) { add("cond."); }
    else if (has_token(t, "part")) { add("part. pass."); }
    else if (has_token(t, "ger"))  { add("ger."); }
    else if (has_token(t, "inf"))  { add("inf."); }

    if (!out.empty())
    {
        return out;
    }

    // No tense or mood: fall back to the part of speech, which is all a noun or adjective
    // has to say. "congiunz." rather than "cong." so it cannot be read as congiuntivo,
    // which a learner meets far more often and which owns the short form above.
    if (pos == "VER")  return "v.";
    if (pos == "NOUN") return "sost.";
    if (pos == "ADJ")  return "agg.";
    if (pos == "ADV")  return "avv.";
    if (pos == "PRO")  return "pron.";
    if (pos == "PRE")  return "prep.";
    if (pos == "CON")  return "congiunz.";
    if (pos == "ART")  return "art.";
    if (pos == "DET")  return "det.";
    return "";
}

} // namespace lexicon
