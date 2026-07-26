#!/bin/sh
# Fetch the offline Wikipedia archive bewiki reads.
#
# The default is the Italian "top" selection in the "nopic" flavour: the ~194k most-visited
# articles, full text, no images, 836MB. Every article is present, so links always resolve.
#
# Swap ZIM_NAME for wikipedia_it_top_mini to get the same article set at 107MB with only
# the lead section of each -- same code path, much smaller. wikipedia_it_all_nopic is the
# whole of Italian Wikipedia at full depth, but it is 8.3GB and so cannot live on the
# card: FAT32, which Onion requires, caps a single file at 4GiB.
#
# Kiwix filenames carry a month suffix that changes with each rebuild, so pinning a URL
# would 404 within weeks. The current one is resolved from the directory listing instead.
# Override with ZIM_URL to pin a specific build.
set -e

ZIM_NAME="${ZIM_NAME:-wikipedia_it_top_nopic}"
ZIM_BASE="${ZIM_BASE:-https://download.kiwix.org/zim/wikipedia}"
DEST="$(cd "$(dirname "$0")/.." && pwd)/resources/wiki"

mkdir -p "$DEST"

if [ -z "$ZIM_URL" ]; then
    echo "Resolving the current $ZIM_NAME build from $ZIM_BASE ..."
    latest="$(curl -sSL "$ZIM_BASE/" \
        | grep -o "${ZIM_NAME}_[0-9][0-9-]*\.zim" \
        | sort -u | tail -1)"

    if [ -z "$latest" ]; then
        echo "error: no $ZIM_NAME archive found at $ZIM_BASE/" >&2
        echo "       The name may have changed; browse that page and set ZIM_URL." >&2
        exit 1
    fi

    ZIM_URL="$ZIM_BASE/$latest"
fi

# Kept under the unsuffixed name so bewiki.cfg does not need editing every time a new
# monthly build is fetched.
out="$DEST/$ZIM_NAME.zim"

echo "Fetching $ZIM_URL"
echo "     ->  $out"
echo "(836MB for the nopic flavour; interrupted downloads resume on re-run)"

# -C - resumes a partial file. Written to a .part first so an interrupted run can never
# leave a truncated archive sitting at the final path, where bewiki would try to open it.
curl -L -C - -o "$out.part" "$ZIM_URL"
mv "$out.part" "$out"

echo
ls -la "$out"
echo
echo "Deploy it with: make deploy-card   (or deploy-wifi)"
