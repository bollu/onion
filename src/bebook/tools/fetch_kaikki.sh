#!/bin/sh
# Fetch the Wiktextract (kaikki.org) machine-readable extract of Italian Wiktionary,
# the bulk source for the dictionary DB. This is a HOST-side build input only -- it is
# never shipped or run on device, and third-party/kaikki/ is gitignored.
#
# ~761 MB download. tools/build_lexicon.py --source kaikki --input <this file> parses it
# into resources/italian.sqlite. The extract is derived from Wiktionary and is CC-BY-SA.
set -e

KAIKKI_URL="https://kaikki.org/dictionary/Italian/kaikki.org-dictionary-Italian.jsonl"
DEST="$(cd "$(dirname "$0")/.." && pwd)/third-party/kaikki"
OUT="$DEST/it.jsonl"

mkdir -p "$DEST"

echo "Fetching $KAIKKI_URL"
echo "  -> $OUT  (~761 MB; this takes a while)"
# -C - resumes a partial file if the download was interrupted.
curl -L -C - -o "$OUT" "$KAIKKI_URL"

echo "Done:"
ls -la "$OUT"
