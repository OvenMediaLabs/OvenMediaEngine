# `docs/` — OvenMediaEngine docs source

This folder holds the **MDX source** for the OvenMediaEngine user
guide published at <https://ovenmedialabs.com/docs/ome/>.

## Editing

Each page is a markdown / MDX file under this directory; the folder
tree maps to the URL structure of the published docs. See
[STYLE.md](./STYLE.md) for frontmatter, admonition syntax, link
conventions, and image rules.

## Local preview

Run `./docs/preview.sh` from the repo root.

The script clones the [ovenmedialabs.com](https://github.com/OvenMediaLabs/ovenmedialabs.com)
repo into a per-product cache, symlinks your `docs/` into it,
and starts a dev server. When it's ready you'll see:

    [SUCCESS] Docusaurus website is running at: http://localhost:3000/

Open that URL in a browser — the page reloads automatically as you
save edits in `docs/`.

Stop the preview with **Ctrl-C** in the terminal.

> **What about broken links?** A broken markdown link (e.g. a typo'd
> `.md` path or a missing image) shows up in the preview terminal
> immediately and stops the page from compiling — you'll know right
> away. A broken anchor (`#missing-section`) is only flagged by the
> full production build, so click your anchor links once before
> merging.

Requirements: bash, git, Node 20+, npm. macOS/Linux. First run ~5
minutes (clone + npm install); subsequent runs ~10 seconds.

Env var overrides:

- `OML_PREVIEW_PORT` (default `3000`)
- `OML_PREVIEW_BRANCH` (which branch of the ovenmedialabs.com repo to clone)
- `OML_PREVIEW_CACHE` (cache root path)
