#!/usr/bin/env python3
"""Unit tests for the fragile pieces of build_lexicon.py.

Run: python3 tools/test_build_lexicon.py   (or via `make test`, which also runs it)

These cover the accent/stress heuristics and the kaikki tag->feature mapping, which are
easy to break and are coupled to the C++ side (fold() must mirror fold_accents(); the
feature codes must be understood by describe_morphology in lexicon_service.cpp).
"""

import unittest

import build_lexicon as bl


class StripStress(unittest.TestCase):
    def test_interior_accents_removed(self):
        # kaikki marks stress with accents that are not Italian orthography.
        self.assertEqual(bl.strip_stress("amàvo"), "amavo")     # amàvo
        self.assertEqual(bl.strip_stress("amerémo"), "ameremo")  # amerémo
        self.assertEqual(bl.strip_stress("amàto"), "amato")     # amàto

    def test_real_final_accents_kept(self):
        self.assertEqual(bl.strip_stress("amerò"), "amerò")   # amerò
        self.assertEqual(bl.strip_stress("città"), "città")   # città (headword-shaped)

    def test_monosyllables(self):
        self.assertEqual(bl.strip_stress("hò"), "ho")       # hò -> ho (no orthographic accent)
        self.assertEqual(bl.strip_stress("è"), "è")    # è stays (copula)
        self.assertEqual(bl.strip_stress("può"), "può")  # può stays
        self.assertEqual(bl.strip_stress("dà"), "dà")    # dà (gives) stays


class Fold(unittest.TestCase):
    def test_folds_accents_and_lowercases(self):
        self.assertEqual(bl.fold("Città"), "citta")
        self.assertEqual(bl.fold("perché"), "perche")
        self.assertEqual(bl.fold("È"), "e")           # È -> e
        self.assertEqual(bl.fold("università"), "universita")


class FormFeatures(unittest.TestCase):
    def test_verb_codes(self):
        self.assertEqual(bl.form_features(["first-person", "singular", "imperfect", "indicative"]),
                         "impf+1+s")
        self.assertEqual(bl.form_features(["infinitive"]), "inf")
        self.assertEqual(bl.form_features(["past", "participle"]), "part")
        self.assertEqual(bl.form_features(["first-person", "singular", "present", "subjunctive"]),
                         "sub+pres+1+s")

    def test_non_verb_tags_are_none(self):
        self.assertIsNone(bl.form_features(["plural"]))       # a noun/adj tag set

    def test_synthetic_rows_skipped(self):
        self.assertIsNone(bl.form_features(["table-tags"]))


class NounFeatures(unittest.TestCase):
    def test_gender_number(self):
        self.assertEqual(bl.noun_features(["plural"]), "pl")
        self.assertEqual(bl.noun_features(["feminine", "plural"]), "f+pl")
        self.assertEqual(bl.noun_features(["masculine", "singular"]), "m+sg")

    def test_bare_form_is_base(self):
        self.assertEqual(bl.noun_features([]), "base")

    def test_synthetic_rows_skipped(self):
        self.assertIsNone(bl.noun_features(["canonical"]))


class ConjSlot(unittest.TestCase):
    def test_indicative_present(self):
        self.assertEqual(bl.conj_slot(["first-person", "plural", "present", "indicative"]),
                         ("presente", 3))
        self.assertEqual(bl.conj_slot(["third-person", "singular", "imperfect", "indicative"]),
                         ("imperfetto", 2))

    def test_non_surfaced_moods_are_none(self):
        self.assertIsNone(bl.conj_slot(["first-person", "singular", "present", "subjunctive"]))
        self.assertIsNone(bl.conj_slot(["second-person", "singular", "imperative"]))


class PassatoProssimo(unittest.TestCase):
    def test_avere_invariable(self):
        forms = bl.passato_prossimo("avere", "amato")
        self.assertEqual(forms[0], "ho amato")
        self.assertEqual(forms[5], "hanno amato")

    def test_essere_agrees(self):
        forms = bl.passato_prossimo("essere", "andato")
        self.assertEqual(forms[0], "sono andato/andata")
        self.assertEqual(forms[3], "siamo andati/andate")


class CleanGloss(unittest.TestCase):
    def test_strips_category_notes_and_trailing_semicolons(self):
        self.assertEqual(bl._clean_gloss("to have; See Category:Italian verbs"), "to have")
        self.assertEqual(bl._clean_gloss("to do;"), "to do")
        self.assertIsNone(bl._clean_gloss("See Category:only"))


if __name__ == "__main__":
    unittest.main()
