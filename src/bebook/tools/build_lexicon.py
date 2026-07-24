#!/usr/bin/env python3
"""Build the bebook Italian lexicon (resources/italian.sqlite).

The reader ships a prebuilt, read-only SQLite DB that answers three questions about a
word selected on the page:

  1. What is this? (form -> lemma + morphology, e.g. "facevo" -> fare, imperfect 1sg)
  2. What does it mean? (It->En and It->It glosses for the lemma)
  3. How does this verb conjugate? (full tables per tense)

Data model (all tables read-only at runtime, indexed on their lookup key):

  forms(form, lemma, pos, features)   -- lemmatization; `features` is Morph-it! style
  defs_it_en(lemma, sense_no, gloss)  -- English glosses
  defs_it_it(lemma, sense_no, gloss)  -- Italian glosses (optional)
  conj(lemma, tense, person, form)    -- one row per (verb, tense, person)

This is the SEED build: a curated core of high-frequency verbs (fully conjugated) plus
common words, enough to actually read a simple Italian text. It is deliberately built
from pure `sqlite3` stdlib with no downloads, so the whole pipeline + reader UI can be
exercised end to end. Bulk data (Morph-it! for `forms`, FreeDict/Wiktionary for `defs`)
drops into the same tables later via ingest_* functions; the schema does not change.

Usage:
    python3 tools/build_lexicon.py [--out resources/italian.sqlite]
"""

import argparse
import os
import sqlite3

# Person indices used everywhere: 0..5 == io, tu, lui/lei, noi, voi, loro.
PERSONS = ["io", "tu", "lui/lei", "noi", "voi", "loro"]
PERSON_FEAT = ["1+s", "2+s", "3+s", "1+p", "2+p", "3+p"]

# Tense keys (stored in `conj.tense`) in the order the popup shows them as tabs.
TENSES = [
    "presente",
    "imperfetto",
    "passato_prossimo",
    "futuro_semplice",
    "condizionale",
]

# Morph-it! style tense codes, for the `forms.features` column.
TENSE_FEAT = {
    "presente": "pres",
    "imperfetto": "impf",
    "futuro_semplice": "fut",
    "condizionale": "cond",
    # passato_prossimo is compound; its parts are tagged on the auxiliary + participle,
    # so it does not get a simple-tense feature code here.
}


# --- Regular conjugation -----------------------------------------------------------

# Endings by conjugation class for the simple tenses. Index 0..5 == io..loro.
REGULAR = {
    "are": {
        "presente":         ["o", "i", "a", "iamo", "ate", "ano"],
        "imperfetto":       ["avo", "avi", "ava", "avamo", "avate", "avano"],
        "futuro_semplice":  ["erò", "erai", "erà", "eremo", "erete", "eranno"],
        "condizionale":     ["erei", "eresti", "erebbe", "eremmo", "ereste", "erebbero"],
        "participle":       "ato",
    },
    "ere": {
        "presente":         ["o", "i", "e", "iamo", "ete", "ono"],
        "imperfetto":       ["evo", "evi", "eva", "evamo", "evate", "evano"],
        "futuro_semplice":  ["erò", "erai", "erà", "eremo", "erete", "eranno"],
        "condizionale":     ["erei", "eresti", "erebbe", "eremmo", "ereste", "erebbero"],
        "participle":       "uto",
    },
    "ire": {
        "presente":         ["o", "i", "e", "iamo", "ite", "ono"],
        "imperfetto":       ["ivo", "ivi", "iva", "ivamo", "ivate", "ivano"],
        "futuro_semplice":  ["irò", "irai", "irà", "iremo", "irete", "iranno"],
        "condizionale":     ["irei", "iresti", "irebbe", "iremmo", "ireste", "irebbero"],
        "participle":       "ito",
    },
    # -isc- present pattern (capire, finire, preferire, ...): the isc infix in 1s,2s,3s,3p.
    "ire_isc": {
        "presente":         ["isco", "isci", "isce", "iamo", "ite", "iscono"],
        "imperfetto":       ["ivo", "ivi", "iva", "ivamo", "ivate", "ivano"],
        "futuro_semplice":  ["irò", "irai", "irà", "iremo", "irete", "iranno"],
        "condizionale":     ["irei", "iresti", "irebbe", "iremmo", "ireste", "irebbero"],
        "participle":       "ito",
    },
}


def aux_tables():
    """Present-tense of the two auxiliaries, needed to build passato prossimo."""
    return {
        "avere": ["ho", "hai", "ha", "abbiamo", "avete", "hanno"],
        "essere": ["sono", "sei", "è", "siamo", "siete", "sono"],
    }


def passato_prossimo(aux, participle):
    """Compound perfect: aux(present) + past participle.

    For essere-auxiliary verbs the participle agrees with the subject, so we surface the
    common alternates (andato/a, andati/e) rather than pretend it is invariable.
    """
    aux_present = aux_tables()[aux]
    if aux == "avere":
        # participle invariable with avere (ignoring clitic agreement).
        return [f"{aux_present[i]} {participle}" for i in range(6)]
    # essere: agree. participle ends in -o; build -o/-a and -i/-e forms.
    stem = participle[:-1]
    sing = f"{stem}o/{stem}a"
    plur = f"{stem}i/{stem}e"
    agree = [sing, sing, sing, plur, plur, plur]
    return [f"{aux_present[i]} {agree[i]}" for i in range(6)]


def conjugate_regular(lemma, cls, aux):
    """Return {tense: [6 forms]} for a regular verb of the given class."""
    endings = REGULAR[cls]
    # Stem is the infinitive minus its two-letter class ending (are/ere/ire).
    base_ending = "ire" if cls.startswith("ire") else cls
    stem = lemma[: -len(base_ending)]
    out = {}
    for tense in ("presente", "imperfetto", "futuro_semplice", "condizionale"):
        out[tense] = [stem + e for e in endings[tense]]
    participle = stem + endings["participle"]
    out["passato_prossimo"] = passato_prossimo(aux, participle)
    return out


# --- Verb catalogue ----------------------------------------------------------------
#
# Each entry: lemma -> dict with en/it glosses and either:
#   "regular": (class, aux)                       -> conjugated by rule
#   "tables": {tense: [6 forms]}, "aux", "part"   -> fully hand-specified (irregulars)
#
# The top verbs the user cares about are irregular, so their simple tenses are written
# out; passato prossimo is derived from aux + participle to stay consistent.

def T(*rows):
    """Small helper: T(io, tu, lui, noi, voi, loro) -> list."""
    assert len(rows) == 6
    return list(rows)


IRREGULAR = {
    "essere": {
        "en": ["to be"], "it": ["esistere; avere una qualità"],
        "aux": "essere", "part": "stato",
        "tables": {
            "presente":        T("sono", "sei", "è", "siamo", "siete", "sono"),
            "imperfetto":      T("ero", "eri", "era", "eravamo", "eravate", "erano"),
            "futuro_semplice": T("sarò", "sarai", "sarà", "saremo", "sarete", "saranno"),
            "condizionale":    T("sarei", "saresti", "sarebbe", "saremmo", "sareste", "sarebbero"),
        },
    },
    "avere": {
        "en": ["to have"], "it": ["possedere; tenere"],
        "aux": "avere", "part": "avuto",
        "tables": {
            "presente":        T("ho", "hai", "ha", "abbiamo", "avete", "hanno"),
            "imperfetto":      T("avevo", "avevi", "aveva", "avevamo", "avevate", "avevano"),
            "futuro_semplice": T("avrò", "avrai", "avrà", "avremo", "avrete", "avranno"),
            "condizionale":    T("avrei", "avresti", "avrebbe", "avremmo", "avreste", "avrebbero"),
        },
    },
    "fare": {
        "en": ["to do", "to make"], "it": ["compiere un'azione; creare"],
        "aux": "avere", "part": "fatto",
        "tables": {
            "presente":        T("faccio", "fai", "fa", "facciamo", "fate", "fanno"),
            "imperfetto":      T("facevo", "facevi", "faceva", "facevamo", "facevate", "facevano"),
            "futuro_semplice": T("farò", "farai", "farà", "faremo", "farete", "faranno"),
            "condizionale":    T("farei", "faresti", "farebbe", "faremmo", "fareste", "farebbero"),
        },
    },
    "dire": {
        "en": ["to say", "to tell"], "it": ["esprimere con parole"],
        "aux": "avere", "part": "detto",
        "tables": {
            "presente":        T("dico", "dici", "dice", "diciamo", "dite", "dicono"),
            "imperfetto":      T("dicevo", "dicevi", "diceva", "dicevamo", "dicevate", "dicevano"),
            "futuro_semplice": T("dirò", "dirai", "dirà", "diremo", "direte", "diranno"),
            "condizionale":    T("direi", "diresti", "direbbe", "diremmo", "direste", "direbbero"),
        },
    },
    "andare": {
        "en": ["to go"], "it": ["muoversi verso un luogo"],
        "aux": "essere", "part": "andato",
        "tables": {
            "presente":        T("vado", "vai", "va", "andiamo", "andate", "vanno"),
            "imperfetto":      T("andavo", "andavi", "andava", "andavamo", "andavate", "andavano"),
            "futuro_semplice": T("andrò", "andrai", "andrà", "andremo", "andrete", "andranno"),
            "condizionale":    T("andrei", "andresti", "andrebbe", "andremmo", "andreste", "andrebbero"),
        },
    },
    "stare": {
        "en": ["to stay", "to be (state)"], "it": ["trovarsi; rimanere"],
        "aux": "essere", "part": "stato",
        "tables": {
            "presente":        T("sto", "stai", "sta", "stiamo", "state", "stanno"),
            "imperfetto":      T("stavo", "stavi", "stava", "stavamo", "stavate", "stavano"),
            "futuro_semplice": T("starò", "starai", "starà", "staremo", "starete", "staranno"),
            "condizionale":    T("starei", "staresti", "starebbe", "staremmo", "stareste", "starebbero"),
        },
    },
    "dare": {
        "en": ["to give"], "it": ["consegnare; porgere"],
        "aux": "avere", "part": "dato",
        "tables": {
            "presente":        T("do", "dai", "dà", "diamo", "date", "danno"),
            "imperfetto":      T("davo", "davi", "dava", "davamo", "davate", "davano"),
            "futuro_semplice": T("darò", "darai", "darà", "daremo", "darete", "daranno"),
            "condizionale":    T("darei", "daresti", "darebbe", "daremmo", "dareste", "darebbero"),
        },
    },
    "sapere": {
        "en": ["to know", "to know how to"], "it": ["conoscere; essere capace di"],
        "aux": "avere", "part": "saputo",
        "tables": {
            "presente":        T("so", "sai", "sa", "sappiamo", "sapete", "sanno"),
            "imperfetto":      T("sapevo", "sapevi", "sapeva", "sapevamo", "sapevate", "sapevano"),
            "futuro_semplice": T("saprò", "saprai", "saprà", "sapremo", "saprete", "sapranno"),
            "condizionale":    T("saprei", "sapresti", "saprebbe", "sapremmo", "sapreste", "saprebbero"),
        },
    },
    "potere": {
        "en": ["to be able to", "can"], "it": ["avere la facoltà o il permesso"],
        "aux": "avere", "part": "potuto",
        "tables": {
            "presente":        T("posso", "puoi", "può", "possiamo", "potete", "possono"),
            "imperfetto":      T("potevo", "potevi", "poteva", "potevamo", "potevate", "potevano"),
            "futuro_semplice": T("potrò", "potrai", "potrà", "potremo", "potrete", "potranno"),
            "condizionale":    T("potrei", "potresti", "potrebbe", "potremmo", "potreste", "potrebbero"),
        },
    },
    "volere": {
        "en": ["to want"], "it": ["desiderare; avere volontà"],
        "aux": "avere", "part": "voluto",
        "tables": {
            "presente":        T("voglio", "vuoi", "vuole", "vogliamo", "volete", "vogliono"),
            "imperfetto":      T("volevo", "volevi", "voleva", "volevamo", "volevate", "volevano"),
            "futuro_semplice": T("vorrò", "vorrai", "vorrà", "vorremo", "vorrete", "vorranno"),
            "condizionale":    T("vorrei", "vorresti", "vorrebbe", "vorremmo", "vorreste", "vorrebbero"),
        },
    },
    "dovere": {
        "en": ["to have to", "must"], "it": ["essere obbligato a"],
        "aux": "avere", "part": "dovuto",
        "tables": {
            "presente":        T("devo", "devi", "deve", "dobbiamo", "dovete", "devono"),
            "imperfetto":      T("dovevo", "dovevi", "doveva", "dovevamo", "dovevate", "dovevano"),
            "futuro_semplice": T("dovrò", "dovrai", "dovrà", "dovremo", "dovrete", "dovranno"),
            "condizionale":    T("dovrei", "dovresti", "dovrebbe", "dovremmo", "dovreste", "dovrebbero"),
        },
    },
    "venire": {
        "en": ["to come"], "it": ["muoversi verso chi parla"],
        "aux": "essere", "part": "venuto",
        "tables": {
            "presente":        T("vengo", "vieni", "viene", "veniamo", "venite", "vengono"),
            "imperfetto":      T("venivo", "venivi", "veniva", "venivamo", "venivate", "venivano"),
            "futuro_semplice": T("verrò", "verrai", "verrà", "verremo", "verrete", "verranno"),
            "condizionale":    T("verrei", "verresti", "verrebbe", "verremmo", "verreste", "verrebbero"),
        },
    },
}

# Regular verbs, expanded by rule: lemma -> (class, aux, [en glosses], [it glosses]).
REGULAR_VERBS = {
    "parlare":   ("are", "avere", ["to speak", "to talk"], ["comunicare con parole"]),
    "guardare":  ("are", "avere", ["to look at", "to watch"], ["rivolgere lo sguardo"]),
    "amare":     ("are", "avere", ["to love"], ["voler bene"]),
    "mangiare":  ("are", "avere", ["to eat"], ["assumere cibo"]),
    "pensare":   ("are", "avere", ["to think"], ["formulare pensieri"]),
    "trovare":   ("are", "avere", ["to find"], ["scoprire; incontrare"]),
    "lavorare":  ("are", "avere", ["to work"], ["svolgere un lavoro"]),
    "chiamare":  ("are", "avere", ["to call"], ["dare un nome; telefonare"]),
    "credere":   ("ere", "avere", ["to believe"], ["ritenere vero"]),
    "vedere":    ("ere", "avere", ["to see"], ["percepire con gli occhi"]),
    "vivere":    ("ere", "avere", ["to live"], ["essere in vita"]),
    "prendere":  ("ere", "avere", ["to take"], ["afferrare; prendere"]),
    "scrivere":  ("ere", "avere", ["to write"], ["tracciare parole"]),
    "dormire":   ("ire", "avere", ["to sleep"], ["riposare dormendo"]),
    "sentire":   ("ire", "avere", ["to hear", "to feel"], ["percepire"]),
    "aprire":    ("ire", "avere", ["to open"], ["rendere aperto"]),
    "partire":   ("ire", "essere", ["to leave", "to depart"], ["andare via"]),
    "capire":    ("ire_isc", "avere", ["to understand"], ["comprendere"]),
    "finire":    ("ire_isc", "avere", ["to finish", "to end"], ["portare a termine"]),
    "preferire": ("ire_isc", "avere", ["to prefer"], ["scegliere di più"]),
}

# Non-verb vocabulary: lemma -> (pos, [en glosses], [it glosses]).
# pos uses Morph-it! coarse tags: NOUN, ADJ, ADV, PRO, ART, PRE, CON, DET.
WORDS = {
    "casa":      ("NOUN", ["house", "home"], ["edificio in cui si abita"]),
    "tempo":     ("NOUN", ["time", "weather"], ["durata; condizioni atmosferiche"]),
    "uomo":      ("NOUN", ["man"], ["essere umano di sesso maschile"]),
    "donna":     ("NOUN", ["woman"], ["essere umano di sesso femminile"]),
    "giorno":    ("NOUN", ["day"], ["periodo di 24 ore"]),
    "notte":     ("NOUN", ["night"], ["parte buia del giorno"]),
    "anno":      ("NOUN", ["year"], ["periodo di dodici mesi"]),
    "vita":      ("NOUN", ["life"], ["condizione di essere vivo"]),
    "mano":      ("NOUN", ["hand"], ["parte del corpo"]),
    "occhio":    ("NOUN", ["eye"], ["organo della vista"]),
    "acqua":     ("NOUN", ["water"], ["liquido incolore"]),
    "cosa":      ("NOUN", ["thing", "what"], ["oggetto; entità"]),
    "parola":    ("NOUN", ["word"], ["unità di linguaggio"]),
    "amico":     ("NOUN", ["friend"], ["persona a cui si vuole bene"]),
    "bambino":   ("NOUN", ["child", "little boy"], ["essere umano nei primi anni"]),
    "città":     ("NOUN", ["city", "town"], ["grande centro abitato"]),
    "mondo":     ("NOUN", ["world"], ["la Terra; l'universo"]),
    "libro":     ("NOUN", ["book"], ["insieme di pagine scritte"]),
    "grande":    ("ADJ", ["big", "great"], ["di notevoli dimensioni"]),
    "piccolo":   ("ADJ", ["small", "little"], ["di dimensioni ridotte"]),
    "buono":     ("ADJ", ["good"], ["di buona qualità"]),
    "bello":     ("ADJ", ["beautiful", "nice"], ["gradevole alla vista"]),
    "nuovo":     ("ADJ", ["new"], ["fatto o comparso da poco"]),
    "vecchio":   ("ADJ", ["old"], ["di molti anni; non nuovo"]),
    "bene":      ("ADV", ["well"], ["in modo positivo"]),
    "male":      ("ADV", ["badly", "bad"], ["in modo negativo"]),
    "molto":     ("ADV", ["very", "a lot", "much"], ["in grande quantità"]),
    "sempre":    ("ADV", ["always"], ["in ogni momento"]),
    "mai":       ("ADV", ["never", "ever"], ["in nessun momento"]),
    "adesso":    ("ADV", ["now"], ["in questo momento"]),
    "qui":       ("ADV", ["here"], ["in questo luogo"]),
    "dove":      ("ADV", ["where"], ["in quale luogo"]),
    "quando":    ("ADV", ["when"], ["in quale momento"]),
    "perché":    ("ADV", ["why", "because"], ["per quale motivo"]),
    "come":      ("ADV", ["how", "like", "as"], ["in che modo"]),
    "io":        ("PRO", ["I"], ["prima persona singolare"]),
    "tu":        ("PRO", ["you (sing.)"], ["seconda persona singolare"]),
    "lui":       ("PRO", ["he", "him"], ["terza persona maschile"]),
    "lei":       ("PRO", ["she", "her"], ["terza persona femminile"]),
    "noi":       ("PRO", ["we", "us"], ["prima persona plurale"]),
    "voi":       ("PRO", ["you (pl.)"], ["seconda persona plurale"]),
    "loro":      ("PRO", ["they", "them"], ["terza persona plurale"]),
    "che":       ("PRO", ["that", "which", "who"], ["pronome relativo"]),
    "questo":    ("PRO", ["this"], ["dimostrativo di vicinanza"]),
    "quello":    ("PRO", ["that"], ["dimostrativo di lontananza"]),
    "con":       ("PRE", ["with"], ["insieme a"]),
    "per":       ("PRE", ["for", "through"], ["a favore di; attraverso"]),
    "senza":     ("PRE", ["without"], ["in assenza di"]),
    "dopo":      ("PRE", ["after"], ["in seguito a"]),
    "prima":     ("ADV", ["before", "first"], ["in precedenza"]),
    "ma":        ("CON", ["but"], ["congiunzione avversativa"]),
    "però":      ("CON", ["however", "but"], ["tuttavia"]),
    "anche":     ("ADV", ["also", "too"], ["in aggiunta"]),
    "ancora":    ("ADV", ["still", "yet", "again"], ["di nuovo; tuttora"]),
}


# --- DB build ----------------------------------------------------------------------

SCHEMA = """
DROP TABLE IF EXISTS forms;
DROP TABLE IF EXISTS defs_it_en;
DROP TABLE IF EXISTS defs_it_it;
DROP TABLE IF EXISTS conj;
DROP TABLE IF EXISTS meta;

CREATE TABLE forms      (form TEXT NOT NULL, lemma TEXT NOT NULL, pos TEXT, features TEXT);
CREATE TABLE defs_it_en (lemma TEXT NOT NULL, sense_no INTEGER, gloss TEXT NOT NULL);
CREATE TABLE defs_it_it (lemma TEXT NOT NULL, sense_no INTEGER, gloss TEXT NOT NULL);
CREATE TABLE conj       (lemma TEXT NOT NULL, tense TEXT NOT NULL, person INTEGER, form TEXT);
CREATE TABLE meta       (key TEXT PRIMARY KEY, value TEXT);
"""

INDEXES = """
CREATE INDEX idx_forms_form      ON forms(form);
CREATE INDEX idx_defs_en_lemma   ON defs_it_en(lemma);
CREATE INDEX idx_defs_it_lemma   ON defs_it_it(lemma);
CREATE INDEX idx_conj_lemma      ON conj(lemma, tense, person);
"""


def norm(s):
    """Lookup key normalization: lowercase. (Accents are significant in Italian.)"""
    return s.lower()


def add_verb(db, lemma, en, it, tables):
    """Insert a verb: definitions, conjugation table, and a form row per cell."""
    for i, g in enumerate(en):
        db.execute("INSERT INTO defs_it_en VALUES (?,?,?)", (lemma, i + 1, g))
    for i, g in enumerate(it):
        db.execute("INSERT INTO defs_it_it VALUES (?,?,?)", (lemma, i + 1, g))

    # infinitive maps to itself
    db.execute("INSERT INTO forms VALUES (?,?,?,?)", (norm(lemma), lemma, "VER", "inf"))

    for tense in TENSES:
        forms = tables[tense]
        for person in range(6):
            form = forms[person]
            db.execute("INSERT INTO conj VALUES (?,?,?,?)", (lemma, tense, person, form))
            # Register each simple-tense form for lemmatization. Compound
            # (passato_prossimo) forms are multi-word; index only their first token so a
            # selected auxiliary still resolves, and skip agreement slashes.
            if tense in TENSE_FEAT:
                feat = f"{TENSE_FEAT[tense]}+{PERSON_FEAT[person]}"
                for token in form.split():
                    db.execute(
                        "INSERT INTO forms VALUES (?,?,?,?)",
                        (norm(token), lemma, "VER", feat),
                    )


def add_word(db, lemma, pos, en, it):
    db.execute("INSERT INTO forms VALUES (?,?,?,?)", (norm(lemma), lemma, pos, "base"))
    for i, g in enumerate(en):
        db.execute("INSERT INTO defs_it_en VALUES (?,?,?)", (lemma, i + 1, g))
    for i, g in enumerate(it):
        db.execute("INSERT INTO defs_it_it VALUES (?,?,?)", (lemma, i + 1, g))


def build(out_path):
    if os.path.exists(out_path):
        os.remove(out_path)
    db = sqlite3.connect(out_path)
    db.executescript(SCHEMA)

    n_verbs = 0
    for lemma, e in IRREGULAR.items():
        # Fill passato prossimo from aux + participle so it is consistent with the rest.
        tables = dict(e["tables"])
        tables["passato_prossimo"] = passato_prossimo(e["aux"], e["part"])
        add_verb(db, lemma, e["en"], e["it"], tables)
        n_verbs += 1

    for lemma, (cls, aux, en, it) in REGULAR_VERBS.items():
        tables = conjugate_regular(lemma, cls, aux)
        add_verb(db, lemma, en, it, tables)
        n_verbs += 1

    for lemma, (pos, en, it) in WORDS.items():
        add_word(db, lemma, pos, en, it)

    db.executescript(INDEXES)
    db.execute("INSERT INTO meta VALUES ('schema_version', '1')")
    db.execute("INSERT INTO meta VALUES ('build', 'seed')")
    db.commit()

    n_forms = db.execute("SELECT COUNT(*) FROM forms").fetchone()[0]
    n_defs = db.execute("SELECT COUNT(*) FROM defs_it_en").fetchone()[0]
    db.close()
    print(f"Wrote {out_path}: {n_verbs} verbs, {len(WORDS)} words, "
          f"{n_forms} form rows, {n_defs} en-glosses.")


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_out = os.path.join(here, "resources", "italian.sqlite")
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=default_out, help="output SQLite path")
    args = ap.parse_args()
    build(args.out)


if __name__ == "__main__":
    main()
