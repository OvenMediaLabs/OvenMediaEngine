#!/usr/bin/env bash
#
# Start a local preview of the ovenmedialabs.com site and surface ONLY the docs for the upstream product
# you're working in. Designed to be copied into each upstream
# `docs/preview.sh` (Enterprise: `docs-enterprise/preview.sh`) so that
# editors can run it after changing markdown and see the result
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
#   5. COPIES this repo's docs into the cache's docs/<source>/, then
#      runs a file watcher that mirrors each of your subsequent edits
#      into the cache so Docusaurus HMR still fires. (A symlink is NOT
#      usable: Docusaurus content-docs mis-reads its registry when its
#      `path:` points through a symlink and the build fails with
#      "Cannot read properties of undefined (reading 'id')".)
#   6. Starts `npm start` with OML_PREVIEW_SOURCE set — the consuming
#      site hides marketing nav/footer and redirects `/` to
#      `/docs/<source>/`.
#
# Note: shared (`dup:`) pages are filled from the open-source manual by
# the site's generator at startup. Don't edit a `dup:` stub locally —
# edit the OSS source. Enterprise-only pages hot-reload as you save.
#
# Defaults: port 3000, bind host localhost. Override via OML_PREVIEW_PORT
# / OML_PREVIEW_HOST (set 0.0.0.0 for direct, non-VSCode remote access),
# or pick a different ovenmedialabs.com branch via OML_PREVIEW_BRANCH.
#
# Requires: bash, git, Node 20+, npm. macOS / Linux.

set -euo pipefail

SITE_REPO="https://github.com/OvenMediaLabs/ovenmedialabs.com.git"
SITE_BRANCH="${OML_PREVIEW_BRANCH:-main}"
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
HOST="${OML_PREVIEW_HOST:-localhost}"

# Locate the docs directory we're previewing. Enterprise keeps its docs
# under docs-enterprise/ (so the OvenMediaEngine merge into Enterprise
# doesn't overwrite them); every other product uses docs/.
repo_root="$(git rev-parse --show-toplevel)"
case "$SOURCE" in
    ome-enterprise) docs_subdir="docs-enterprise" ;;
    *)              docs_subdir="docs" ;;
esac
docs_site="$repo_root/$docs_subdir"
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

# Copy our docs into the cache's docs/<source>/. A symlink can't be
# used (Docusaurus content-docs breaks when its path: is a symlink), so
# we copy and then mirror edits with a watcher (below) to keep HMR.
target="$site_dir/docs/$SOURCE"
rm -rf "$target"
mkdir -p "$target"
cp -R "$docs_site/." "$target/"

# Watch our docs and mirror each changed file into the cache copy so
# Docusaurus HMR fires on save. Per-file (not a bulk re-sync): a bulk
# rsync would clobber the shared pages the site's generator expands at
# startup. chokidar ships with Docusaurus (already in the cache's
# node_modules); ignoreInitial keeps the startup-expanded pages intact.
watch_js="$(mktemp "${TMPDIR:-/tmp}/oml-preview-watch.XXXXXX")"
cat > "$watch_js" <<'NODE'
const chokidar = require(process.env.OML_CHOKIDAR);
const fs = require('fs');
const path = require('path');
const src = process.env.OML_SRC;
const dst = process.env.OML_DST;
chokidar.watch(src, { ignoreInitial: true }).on('all', (ev, p) => {
  const out = path.join(dst, path.relative(src, p));
  try {
    if (ev === 'unlink' || ev === 'unlinkDir') {
      fs.rmSync(out, { recursive: true, force: true });
    } else if (ev === 'addDir') {
      fs.mkdirSync(out, { recursive: true });
    } else {
      fs.mkdirSync(path.dirname(out), { recursive: true });
      fs.copyFileSync(p, out);
    }
  } catch (e) {
    console.error('[preview-watch]', e.message);
  }
});
console.log('[preview-watch] mirroring edits:', src, '->', dst);
NODE

OML_CHOKIDAR="$site_dir/node_modules/chokidar" \
OML_SRC="$docs_site" OML_DST="$target" \
    node "$watch_js" &
watch_pid=$!
trap 'kill "$watch_pid" 2>/dev/null || true; rm -f "$watch_js"' EXIT INT TERM

echo
echo "▶ Docusaurus preview starting — open http://localhost:$PORT  (Ctrl-C to stop)"
echo "  source:     $SOURCE  ←  $docs_site"
echo "  cache:      $site_dir"
echo "  bind host:  $HOST  (override: OML_PREVIEW_HOST)"
echo "  remote dev: VSCode Remote-SSH auto-forwards the port — just open"
echo "              http://localhost:$PORT on your local machine. For direct"
echo "              (non-VSCode) remote access, re-run with"
echo "              OML_PREVIEW_HOST=0.0.0.0 and browse http://<remote-host>:$PORT."
echo "  preview UI: marketing pages and other products' docs are hidden"
echo

cd "$site_dir"
OML_PREVIEW_SOURCE="$SOURCE" npm start -- --port "$PORT" --host "$HOST"
