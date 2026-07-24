#!/bin/sh
# Vendor the SQLite single-file amalgamation into third-party/sqlite/ for the device
# build. Desktop builds use the system sqlite3 via pkg-config and do not need this.
#
# The amalgamation (sqlite3.c + sqlite3.h) is public domain. We pin a version so device
# builds are reproducible; bump SQLITE_URL to update.
set -e

SQLITE_URL="https://sqlite.org/2024/sqlite-amalgamation-3450100.zip"
DEST="$(cd "$(dirname "$0")/.." && pwd)/third-party/sqlite"

mkdir -p "$DEST"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "Fetching $SQLITE_URL"
curl -L -o "$tmp/sqlite.zip" "$SQLITE_URL"
unzip -o -j "$tmp/sqlite.zip" '*/sqlite3.c' '*/sqlite3.h' -d "$DEST"

echo "Vendored sqlite amalgamation into $DEST:"
ls -la "$DEST"
