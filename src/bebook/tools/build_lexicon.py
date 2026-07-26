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
    "passato_remoto",
    "futuro_semplice",
    "condizionale",
]

# Morph-it! style tense codes, for the `forms.features` column.
TENSE_FEAT = {
    "presente": "pres",
    "imperfetto": "impf",
    "futuro_semplice": "fut",
    "condizionale": "cond",
    "passato_remoto": "rem",
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
        "passato_remoto":  ["ai", "asti", "ò", "ammo", "aste", "arono"],
        "participle":       "ato",
    },
    "ere": {
        "presente":         ["o", "i", "e", "iamo", "ete", "ono"],
        "imperfetto":       ["evo", "evi", "eva", "evamo", "evate", "evano"],
        "futuro_semplice":  ["erò", "erai", "erà", "eremo", "erete", "eranno"],
        "condizionale":     ["erei", "eresti", "erebbe", "eremmo", "ereste", "erebbero"],
        "passato_remoto":  ["ei", "esti", "é", "emmo", "este", "erono"],
        "participle":       "uto",
    },
    "ire": {
        "presente":         ["o", "i", "e", "iamo", "ite", "ono"],
        "imperfetto":       ["ivo", "ivi", "iva", "ivamo", "ivate", "ivano"],
        "futuro_semplice":  ["irò", "irai", "irà", "iremo", "irete", "iranno"],
        "condizionale":     ["irei", "iresti", "irebbe", "iremmo", "ireste", "irebbero"],
        "passato_remoto":  ["ii", "isti", "ì", "immo", "iste", "irono"],
        "participle":       "ito",
    },
    # -isc- present pattern (capire, finire, preferire, ...): the isc infix in 1s,2s,3s,3p.
    "ire_isc": {
        "presente":         ["isco", "isci", "isce", "iamo", "ite", "iscono"],
        "imperfetto":       ["ivo", "ivi", "iva", "ivamo", "ivate", "ivano"],
        "futuro_semplice":  ["irò", "irai", "irà", "iremo", "irete", "iranno"],
        "condizionale":     ["irei", "iresti", "irebbe", "iremmo", "ireste", "irebbero"],
        "passato_remoto":  ["ii", "isti", "ì", "immo", "iste", "irono"],
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
    for tense in ("presente", "imperfetto", "passato_remoto",
                  "futuro_semplice", "condizionale"):
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
            "passato_remoto":  T("fui", "fosti", "fu", "fummo", "foste", "furono"),
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
            "passato_remoto":  T("ebbi", "avesti", "ebbe", "avemmo", "aveste", "ebbero"),
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
            "passato_remoto":  T("feci", "facesti", "fece", "facemmo", "faceste", "fecero"),
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
            "passato_remoto":  T("dissi", "dicesti", "disse", "dicemmo", "diceste", "dissero"),
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
            "passato_remoto":  T("andai", "andasti", "andò", "andammo", "andaste", "andarono"),
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
            "passato_remoto":  T("stetti", "stesti", "stette", "stemmo", "steste", "stettero"),
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
            "passato_remoto":  T("diedi", "desti", "diede", "demmo", "deste", "diedero"),
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
            "passato_remoto":  T("seppi", "sapesti", "seppe", "sapemmo", "sapeste", "seppero"),
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
            "passato_remoto":  T("potei", "potesti", "poté", "potemmo", "poteste", "poterono"),
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
            "passato_remoto":  T("volli", "volesti", "volle", "volemmo", "voleste", "vollero"),
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
            "passato_remoto":  T("dovetti", "dovesti", "dovette", "dovemmo", "doveste", "dovettero"),
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
            "passato_remoto":  T("venni", "venisti", "venne", "venimmo", "veniste", "vennero"),
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

CREATE TABLE forms      (form TEXT NOT NULL, lemma TEXT NOT NULL, pos TEXT, features TEXT, form_fold TEXT);
CREATE TABLE defs_it_en (lemma TEXT NOT NULL, sense_no INTEGER, gloss TEXT NOT NULL);
CREATE TABLE defs_it_it (lemma TEXT NOT NULL, sense_no INTEGER, gloss TEXT NOT NULL);
CREATE TABLE conj       (lemma TEXT NOT NULL, tense TEXT NOT NULL, person INTEGER, form TEXT);
CREATE TABLE meta       (key TEXT PRIMARY KEY, value TEXT);
"""

INDEXES = """
CREATE INDEX idx_forms_form      ON forms(form);
CREATE INDEX idx_forms_fold      ON forms(form_fold);
CREATE INDEX idx_defs_en_lemma   ON defs_it_en(lemma);
CREATE INDEX idx_defs_it_lemma   ON defs_it_it(lemma);
CREATE INDEX idx_conj_lemma      ON conj(lemma, tense, person);
"""


def build_vocab(db):
    """FTS5 trigram index over distinct (form_fold, lemma) pairs, for fuzzy 'did you mean'
    lookup: querying the OR of a folded query's trigrams and ordering by `rank` finds the
    closest words even when the first letter is wrong. Needs the runtime SQLite built with
    -DSQLITE_ENABLE_FTS5 (see the Makefile); the host sqlite has FTS5, so this builds here.
    Entries shorter than a trigram can never match, so they are skipped."""
    db.execute("DROP TABLE IF EXISTS vocab")
    db.execute("CREATE VIRTUAL TABLE vocab USING fts5(fold, lemma UNINDEXED, tokenize='trigram')")
    db.execute(
        "INSERT INTO vocab(fold, lemma) "
        "SELECT DISTINCT form_fold, lemma FROM forms "
        "WHERE form_fold IS NOT NULL AND length(form_fold) >= 3"
    )


def norm(s):
    """Lookup key normalization: lowercase. (Accents are significant in Italian.)"""
    return s.lower()


# Accent folding for the fallback lookup: lowercase + strip accents to base ASCII vowels.
# A book may carry a wrong or missing accent ("perche" for "perché"); folding both the
# stored form and the query to this base lets an exact-miss still resolve. Must stay in
# lockstep with fold_accents() in src/lexicon/lexicon_service.cpp.
_FOLD = {
    "à": "a", "á": "a", "â": "a", "ã": "a",
    "è": "e", "é": "e", "ê": "e", "ë": "e",
    "ì": "i", "í": "i", "î": "i", "ï": "i",
    "ò": "o", "ó": "o", "ô": "o", "õ": "o", "ö": "o",
    "ù": "u", "ú": "u", "û": "u", "ü": "u",
}


def fold(s):
    return "".join(_FOLD.get(c, c) for c in s.lower())


def add_verb(db, lemma, en, it, tables):
    """Insert a verb: definitions, conjugation table, and a form row per cell."""
    for i, g in enumerate(en):
        db.execute("INSERT INTO defs_it_en VALUES (?,?,?)", (lemma, i + 1, g))
    for i, g in enumerate(it):
        db.execute("INSERT INTO defs_it_it VALUES (?,?,?)", (lemma, i + 1, g))

    # infinitive maps to itself
    db.execute("INSERT INTO forms VALUES (?,?,?,?,?)",
               (norm(lemma), lemma, "VER", "inf", fold(lemma)))

    for tense in TENSES:
        forms = tables[tense]
        for person in range(6):
            form = forms[person]
            db.execute("INSERT INTO conj VALUES (?,?,?,?)", (lemma, tense, person, form))
            # Register the simple-tense forms for lemmatization (they are single words).
            # Compound tenses (passato_prossimo) are not in TENSE_FEAT and so are left out --
            # their auxiliary resolves via its own entry.
            if tense in TENSE_FEAT:
                feat = f"{TENSE_FEAT[tense]}+{PERSON_FEAT[person]}"
                db.execute(
                    "INSERT INTO forms VALUES (?,?,?,?,?)",
                    (norm(form), lemma, "VER", feat, fold(form)),
                )


def add_word(db, lemma, pos, en, it):
    db.execute("INSERT INTO forms VALUES (?,?,?,?,?)",
               (norm(lemma), lemma, pos, "base", fold(lemma)))
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
    build_vocab(db)
    db.execute("INSERT INTO meta VALUES ('schema_version', '1')")
    db.execute("INSERT INTO meta VALUES ('build', 'seed')")
    db.commit()

    n_forms = db.execute("SELECT COUNT(*) FROM forms").fetchone()[0]
    n_defs = db.execute("SELECT COUNT(*) FROM defs_it_en").fetchone()[0]
    db.close()
    print(f"Wrote {out_path}: {n_verbs} verbs, {len(WORDS)} words, "
          f"{n_forms} form rows, {n_defs} en-glosses.")


# --- kaikki (Wiktextract) ingest ---------------------------------------------------
#
# The bulk build parses kaikki.org's Italian JSONL (one entry per word/POS/etymology).
# The lemma is the clean `word` field; its `forms[]` list carries every inflected surface
# tagged with grammatical features. Two things make this non-trivial:
#
#   1. Stress marks. kaikki's inflected forms are marked with pedagogical stress accents
#      ("amàvo", "amerémo", "hò") that are NOT Italian orthography. Real text has "amavo",
#      "ameremo", "ho". strip_stress() removes interior accents (always wrong in spelling)
#      and keeps a genuine final accent only when it is orthographically real (polysyllables
#      "farò"/"città", and a small set of accented monosyllables "è"/"può"/"dà"). Without
#      this, selecting a word in a real book would never match the DB.
#   2. Compound tenses. kaikki lists only simple tenses in forms[]; passato prossimo is
#      synthesized from the past participle + auxiliary, reusing passato_prossimo() above.

import json

# kaikki POS -> our coarse Morph-it! style tag. Anything not here is skipped (proper
# nouns, affixes, numerals, interjections, symbols) to keep the DB to reading vocabulary.
KAIKKI_POS = {
    "verb": "VER", "noun": "NOUN", "adj": "ADJ", "adv": "ADV",
    "pron": "PRO", "prep": "PRE", "conj": "CON", "article": "ART", "det": "DET",
}

# Form variants we do not want cluttering lemmatization or the tables.
DISQUALIFY = {
    "archaic", "dated", "obsolete", "rare", "literary", "alternative",
    "apocopic", "dialectal", "Modern",
}
# Synthetic / non-form rows kaikki emits inside forms[].
SKIP_FORM_TAGS = {"table-tags", "inflection-template", "canonical", "auxiliary", "romanization", "class"}

_DEACC = {
    "à": "a", "á": "a", "â": "a", "ã": "a",
    "è": "e", "é": "e", "ê": "e",
    "ì": "i", "í": "i", "î": "i",
    "ò": "o", "ó": "o", "ô": "o",
    "ù": "u", "ú": "u", "û": "u",
}
_VOWELS = set("aeiouàáâãèéêìíîòóôùúû")
# Accented monosyllables that DO keep their accent in real orthography, keyed by their
# deaccented form. Everything else monosyllabic (ho, fa, va, sto, so, do) drops the mark.
_KEEP_MONO = {"e", "puo", "gia", "cio", "piu", "giu", "da", "di", "si", "la", "li", "ne", "se", "te"}


def _vowel_groups(s):
    n, prev = 0, False
    for c in s:
        v = c in _VOWELS
        if v and not prev:
            n += 1
        prev = v
    return n


def strip_stress(form):
    """kaikki stress-marked surface -> Italian orthography (see module note)."""
    if not form:
        return form
    chars = list(form)
    for i in range(len(chars) - 1):
        chars[i] = _DEACC.get(chars[i], chars[i])
    last = chars[-1]
    if last in _DEACC:  # a final accented vowel
        deacc = "".join(_DEACC.get(c, c) for c in chars)
        if _vowel_groups(deacc) < 2 and deacc.lower() not in _KEEP_MONO:
            chars[-1] = _DEACC[last]
    return "".join(chars)


def _person_number(ts):
    p = "1" if "first-person" in ts else "2" if "second-person" in ts else "3" if "third-person" in ts else None
    n = "s" if "singular" in ts else "p" if "plural" in ts else None
    return p, n


def form_features(tags):
    """Map a kaikki tag list to our `features` string, or None to skip the form."""
    ts = set(tags)
    if ts & SKIP_FORM_TAGS:
        return None
    if "infinitive" in ts:
        return "inf"
    if "participle" in ts:
        return "part"
    if "gerund" in ts:
        return "ger"

    sub = "subjunctive" in ts
    if "conditional" in ts:
        base = "cond"
    elif "imperative" in ts:
        base = "impr"
    elif "future" in ts:
        base = "fut"
    elif "imperfect" in ts:
        base = "impf"
    elif "present" in ts:
        base = "pres"
    elif "historic" in ts or "past" in ts:
        base = "rem"
    else:
        return None

    code = f"sub+{base}" if (sub and base in ("pres", "impf")) else base
    p, n = _person_number(ts)
    return f"{code}+{p}+{n}" if (p and n) else code


def noun_features(tags):
    """Feature string for a NON-verb inflection (noun plural, adjective gender/number, ...),
    or None to skip. form_features() only understands verb morphology and returns None for
    these, which would drop every noun plural / adjective form from lemmatization. Codes
    (fem/masc/sing/plur) are distinct from the verb person/number tokens and are rendered by
    describe_morphology in src/lexicon/lexicon_service.cpp."""
    ts = set(tags)
    if ts & SKIP_FORM_TAGS:
        return None
    parts = []
    if "feminine" in ts:
        parts.append("f")
    elif "masculine" in ts:
        parts.append("m")
    if "plural" in ts:
        parts.append("pl")
    elif "singular" in ts:
        parts.append("sg")
    return "+".join(parts) if parts else "base"


# The four simple indicative tenses we surface as conjugation tabs. Maps a form's tags to
# (tense_key, person_index 0..5), or None if the form is not one of these cells.
def conj_slot(tags):
    ts = set(tags)
    if "subjunctive" in ts or "imperative" in ts:
        return None
    p, n = _person_number(ts)
    if not (p and n):
        return None
    if "conditional" in ts:
        tense = "condizionale"
    elif "future" in ts and "indicative" in ts:
        tense = "futuro_semplice"
    elif "imperfect" in ts and "indicative" in ts:
        tense = "imperfetto"
    # Passato remoto. Wiktextract tags it "historic"+"past"; "indicative" is present on most
    # rows but not all, so requiring it would drop a chunk of otherwise good forms.
    elif "historic" in ts and "past" in ts:
        tense = "passato_remoto"
    elif "present" in ts and "indicative" in ts:
        tense = "presente"
    else:
        return None
    idx = (int(p) - 1) + (3 if n == "p" else 0)
    return tense, idx


def _real_sense(s):
    """A genuine definition, not an 'inflection of X' form-of pointer or a bare category."""
    tags = s.get("tags") or []
    if "form-of" in tags or "no-gloss" in tags or s.get("form_of"):
        return False
    return bool(s.get("glosses"))


def _clean_gloss(g):
    """Drop editorial cruft; return None to skip the gloss entirely."""
    # Wiktionary appends maintenance notes ("See Category:Italian ...") to some glosses.
    g = g.split("See Category:")[0].strip()
    g = g.rstrip("; ").strip()
    if not g or "Category:" in g:
        return None
    return g


def _flush_word(db, word, entries, seen_defs, counters):
    """Emit rows for all entries sharing one headword (`word`)."""
    lemma = word  # kaikki `word` is the clean orthographic headword.

    # Only entries that are a mapped POS AND carry a real definition are treated as this
    # lemma. Wiktionary gives every inflected form its own page ("amavo": "imperfect of
    # amare"); those are form-of-only and must NOT register as lemmas -- their inflection is
    # already captured from the real lemma's forms[]. Skipping them also keeps the verb
    # count honest and avoids self-referential (amavo -> amavo) rows.
    real = [e for e in entries if KAIKKI_POS.get(e.get("pos")) and any(_real_sense(s) for s in e.get("senses", []))]
    if not real:
        return

    # Definitions.
    def_no = 0
    for e in real:
        for s in e.get("senses", []):
            if not _real_sense(s):
                continue
            for g in (s.get("glosses") or []):
                g = _clean_gloss(g)
                if g is None:
                    continue
                key = (lemma, g)
                if key in seen_defs:
                    continue
                seen_defs.add(key)
                def_no += 1
                db.execute("INSERT INTO defs_it_en VALUES (?,?,?)", (lemma, def_no, g))
                counters["defs"] += 1

    # Forms table (lemmatization): headword + every inflected form, stress-stripped.
    form_rows = set()
    is_verb = any(e.get("pos") == "verb" for e in real)
    head_pos = KAIKKI_POS.get(real[0].get("pos"))
    if head_pos:
        form_rows.add((norm(lemma), lemma, head_pos, "inf" if head_pos == "VER" else "base", fold(lemma)))

    for e in real:
        pos = KAIKKI_POS.get(e.get("pos"))
        if pos is None:
            continue
        is_verb_entry = e.get("pos") == "verb"
        for fm in e.get("forms", []):
            tags = fm.get("tags") or []
            if set(tags) & DISQUALIFY or set(tags) & SKIP_FORM_TAGS:
                continue
            surface = strip_stress((fm.get("form") or "").strip())
            if not surface or " " in surface or surface == "-":
                continue
            # Verbs carry tense/mood morphology; nouns/adjectives carry gender/number.
            feat = form_features(tags) if is_verb_entry else noun_features(tags)
            if feat is None:
                continue
            form_rows.add((norm(surface), lemma, pos, feat, fold(surface)))

    for row in form_rows:
        db.execute("INSERT INTO forms VALUES (?,?,?,?,?)", row)
        counters["forms"] += 1

    # Conjugation tables (verbs): the four simple tenses from forms[], plus a synthesized
    # passato prossimo. Pick the most standard candidate per (tense, person).
    if not is_verb:
        return
    counters["verbs"] += 1

    slots = {}          # (tense, idx) -> (penalty, form)
    participle = None
    aux = None
    for e in real:
        if e.get("pos") != "verb":
            continue
        for fm in e.get("forms", []):
            tags = fm.get("tags") or []
            ts = set(tags)
            raw = (fm.get("form") or "").strip()
            if not raw or " " in raw:
                continue
            if "auxiliary" in ts and aux is None:
                a = strip_stress(raw).lower()
                if a in ("avere", "essere"):
                    aux = a
                continue
            if {"participle", "past"} <= ts and (ts & DISQUALIFY) == set() and participle is None:
                participle = strip_stress(raw)
            slot = conj_slot(tags)
            if slot is None:
                continue
            penalty = len(ts & DISQUALIFY)
            cur = slots.get(slot)
            if cur is None or penalty < cur[0]:
                slots[slot] = (penalty, strip_stress(raw))

    for (tense, idx), (_, form) in slots.items():
        db.execute("INSERT INTO conj VALUES (?,?,?,?)", (lemma, tense, idx, form))
        counters["conj"] += 1

    # Passato prossimo is periphrastic (aux + participle) and not in kaikki's forms[]. Only
    # synthesize it when the auxiliary is known: guessing "avere" would ship "ho andato" for
    # the essere-verbs (andare/partire/...) whose real form is "sono andato/a". Better absent
    # than grammatically wrong.
    if participle and aux:
        for idx, form in enumerate(passato_prossimo(aux, participle)):
            db.execute("INSERT INTO conj VALUES (?,?,?,?)", (lemma, "passato_prossimo", idx, form))
            counters["conj"] += 1
    elif participle:
        counters["pp_skipped_no_aux"] = counters.get("pp_skipped_no_aux", 0) + 1


def ingest_kaikki(out_path, jsonl_path, limit=None):
    if os.path.exists(out_path):
        os.remove(out_path)
    db = sqlite3.connect(out_path)
    db.executescript(SCHEMA)

    counters = {"defs": 0, "forms": 0, "conj": 0, "verbs": 0, "entries": 0, "lemmas": 0}
    seen_defs = set()
    cur_word, group = None, []

    def flush():
        if group:
            _flush_word(db, cur_word, group, seen_defs, counters)
            counters["lemmas"] += 1

    with open(jsonl_path, encoding="utf-8") as f:
        for line in f:
            try:
                e = json.loads(line)
            except json.JSONDecodeError:
                continue
            if e.get("lang_code") != "it":
                continue
            counters["entries"] += 1
            if limit and counters["entries"] > limit:
                break
            w = e.get("word")
            # kaikki is sorted by word, so same-headword entries are contiguous: group them
            # to dedupe glosses/forms and gather all POS/etymologies of one lemma together.
            if w != cur_word:
                flush()
                cur_word, group = w, []
            group.append(e)
        flush()

    db.executescript(INDEXES)
    build_vocab(db)
    db.execute("INSERT INTO meta VALUES ('schema_version', '1')")
    db.execute("INSERT INTO meta VALUES ('build', 'kaikki')")
    db.execute("INSERT INTO meta VALUES ('source_url', 'https://kaikki.org/dictionary/Italian/')")
    db.execute("INSERT INTO meta VALUES ('source', 'Wiktionary via Wiktextract (kaikki.org), CC-BY-SA')")
    db.commit()
    db.close()
    print(f"Wrote {out_path} from kaikki: {counters['lemmas']} lemmas "
          f"({counters['verbs']} verbs), {counters['forms']} form rows, "
          f"{counters['defs']} en-glosses, {counters['conj']} conj rows.")
    skipped = counters.get("pp_skipped_no_aux", 0)
    if skipped:
        print(f"  ({skipped} verbs had no tagged auxiliary; passato prossimo omitted for them)")
    write_xz(out_path)


def write_xz(path):
    """Write path.xz (max compression). This is the committed artifact -- the ~80MB raw
    DB is gitignored and regenerated from the .xz at build time (see the Makefile)."""
    import lzma
    import shutil
    xz_path = path + ".xz"
    with open(path, "rb") as fi, lzma.open(xz_path, "wb", preset=9 | lzma.PRESET_EXTREME) as fo:
        shutil.copyfileobj(fi, fo)
    print(f"Compressed -> {xz_path} ({os.path.getsize(xz_path)} bytes)")


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_out = os.path.join(here, "resources", "italian.sqlite")
    default_kaikki = os.path.join(here, "third-party", "kaikki", "it.jsonl")
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=default_out, help="output SQLite path")
    ap.add_argument("--source", choices=["seed", "kaikki"], default="seed",
                    help="seed = curated core (default, no download); kaikki = bulk Wiktextract")
    ap.add_argument("--input", default=default_kaikki,
                    help="kaikki Italian JSONL (for --source kaikki)")
    ap.add_argument("--limit", type=int, default=None, help="cap entries (smoke tests)")
    args = ap.parse_args()
    if args.source == "kaikki":
        ingest_kaikki(args.out, args.input, args.limit)
    else:
        build(args.out)


if __name__ == "__main__":
    main()
