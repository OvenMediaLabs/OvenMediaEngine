#!/usr/bin/env bash
#
# Start a local preview of the ovenmedialabs.com site and surface ONLY the docs for the upstream product
# you're working in. Designed to be copied into each upstream
# `docs/preview.sh` so that editors can run
# `./docs/preview.sh` after changing markdown and see the result
# rendered with the same theme/layout as production.
#
# Behaviour:
#   1. Detects which product you're in (ome / ome-enterprise /
#      ovenplayer) by inspecting `git remote get-url origin`.
#   2. Bootstraps a per-product cache under
#         ~/.cache/ovenmedialabs-preview/<source>/site/
#      with a clone of OvenMediaLabs/ovenmedialabs.com so different
#      products keep separate working copies.
#   3. Refreshes the cache (fetch + reset --hard) on each run.
#   4. Installs npm deps if the lockfile changed.
#   5. Symlinks the current repo's `docs/` into the cache's
#      `docs/<source>/` so docs changes are picked up live by HMR.
#   6. Starts `npm start` with OML_PREVIEW_SOURCE set — the consuming
#      site hides marketing nav/footer and redirects `/` to
#      `/docs/<source>/`.
#
# Default port is 3000; override with OML_PREVIEW_PORT (or pick a
# different branch of the ovenmedialabs.com repo via OML_PREVIEW_BRANCH).
#
# Requires: bash, git, Node 20+, npm. macOS / Linux.

set -euo pipefail

SITE_REPO="https://github.com/OvenMediaLabs/ovenmedialabs.com.git"
SITE_BRANCH="${OML_PREVIEW_BRANCH:-feat/docusaurus-migration}"
CACHE_ROOT="${OML_PREVIEW_CACHE:-$HOME/.cache/ovenmedialabs-preview}"

# Detect which product we're in.
origin_url="$(git remote get-url origin 2>/dev/null || true)"
case "$origin_url" in
    *OvenMediaEngineEnterprise*) SOURCE="ome-enterprise" ;;
    *OvenMediaEngine*)           SOURCE="ome" ;;
    *OvenPlayer*)                SOURCE="ovenplayer" ;;
    *)
        echo "error: can't detect product from origin URL '$origin_url'" >&2
        echo "       run this from inside an OvenMediaLabs upstream repo." >&2
        exit 1
        ;;
esac
PORT="${OML_PREVIEW_PORT:-3000}"

# Locate the docs directory we're previewing.
repo_root="$(git rev-parse --show-toplevel)"
docs_site="$repo_root/docs"
if [ ! -d "$docs_site" ]; then
    echo "error: $docs_site not found — are you in the right repo?" >&2
    exit 1
fi

# Verify prerequisites.
for cmd in node npm git; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "error: '$cmd' not found in PATH. Install Node 20+ and git." >&2
        exit 1
    fi
done

node_major="$(node --version | sed -E 's/^v([0-9]+).*/\1/')"
if [ "$node_major" -lt 20 ]; then
    echo "error: Node $node_major detected; preview needs Node 20+." >&2
    exit 1
fi

site_dir="$CACHE_ROOT/$SOURCE/site"

# Clone ovenmedialabs.com into the cache on first run, then keep it
# fresh on every subsequent run.
if [ ! -d "$site_dir/.git" ]; then
    echo "▶ first run: cloning $SITE_REPO into $site_dir"
    mkdir -p "$(dirname "$site_dir")"
    git clone --branch "$SITE_BRANCH" --depth 1 "$SITE_REPO" "$site_dir"
else
    echo "▶ refreshing site cache ($site_dir)"
    git -C "$site_dir" fetch origin "$SITE_BRANCH" --quiet
    git -C "$site_dir" reset --hard "origin/$SITE_BRANCH" --quiet
fi

# Install npm deps the first time, or whenever the lockfile changed
# since node_modules was last touched.
if [ ! -d "$site_dir/node_modules" ] \
   || [ "$site_dir/package-lock.json" -nt "$site_dir/node_modules" ]; then
    echo "▶ installing npm dependencies (one-time, ~1 minute)"
    (cd "$site_dir" && npm install --no-audit --no-fund)
fi

# Replace the cached docs/<source>/ with a symlink to our docs/
# so the editor sees their edits live via HMR. If the cache already
# has a non-symlink directory there (from a fresh clone), drop it
# first.
target="$site_dir/docs/$SOURCE"
if [ -L "$target" ]; then
    rm "$target"
elif [ -e "$target" ]; then
    rm -rf "$target"
fi
ln -s "$docs_site" "$target"

echo
echo "▶ starting Docusaurus on http://localhost:$PORT  (Ctrl-C to stop)"
echo "  source:     $SOURCE  ←  $docs_site"
echo "  cache:      $site_dir"
echo "  preview UI: marketing pages and other products' docs are hidden"
echo

cd "$site_dir"
OML_PREVIEW_SOURCE="$SOURCE" npm start -- --port "$PORT"
