# `docs/` — OvenMediaEngine docs source

This folder holds the **MDX source** for the OvenMediaEngine user
guide published at <https://ovenmedialabs.com/docs/ome/>.

## Editing

Each page is a markdown / MDX file under this directory; the folder
tree maps to the URL structure of the published docs.

### Frontmatter

Every page should have YAML frontmatter at the top:

```yaml
---
title: Stream Recording
sidebar_position: 4
description: Configure on-the-fly recording of WebRTC streams.
---
```

- `title` — page title shown in browser tab and as H1
- `sidebar_position` — order within the section (smaller = higher)
- `description` — SEO meta description; appears in search snippets
- `slug` (optional) — override URL path; useful for `intro.md` (`slug: /`)

### Admonitions

```mdx
:::note
General note.
:::

:::tip
Helpful tip.
:::

:::info
Neutral info.
:::

:::warning
Warning.
:::

:::danger
Critical warning.
:::
```

Optionally with a title: `:::info[Custom title]`

### Tabs

```mdx
import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs>
  <TabItem value="ubuntu" label="Ubuntu 22" default>

  Ubuntu-specific instructions here.

  </TabItem>
  <TabItem value="fedora" label="Fedora 38">

  Fedora-specific instructions here.

  </TabItem>
</Tabs>
```

The two `import` lines are required once per file that uses tabs.

### Code blocks

Standard fenced code with optional language, title, and highlights:

````mdx
```bash title="Build OME"
./configure
make -j$(nproc)
sudo make install
```
````

````mdx
```xml title="Server.xml" {3,7-9}
<Server>
  <Name>OME</Name>
  <IP>*</IP>           {/* highlighted */}
  <Bind>...</Bind>
</Server>
```
````

### Images

Put images in `docs/images/` and reference them with a relative path:

```mdx
![Architecture diagram](./images/architecture.png)
```

Subfolders work too: from `features/security/auth.md`, use
`../../images/auth.png`.

**Filename rule**: no spaces, no parens. Use `kebab-case` or
`snake_case`. (Spaces and `()` need URL-encoding, which is a footgun.)

### Characters that need escaping

MDX parses `<`, `{`, `}` as JSX. In plain text:

- `<` → `&lt;` (or wrap in backticks: `` `<992` ``)
- `{` → `&#123;`
- `}` → `&#125;`

Inside fenced code blocks (` ``` `) or inline code (`` ` ``), escape
nothing — those are raw.

### Sidebar order

Page order within a section follows `sidebar_position:` in frontmatter.
For directory labels and order, add a `_category_.json` to the folder:

```json
{
  "label": "Security",
  "position": 5,
  "link": { "type": "doc", "id": "README" }
}
```

## Local preview

Run `./docs/preview.sh` from the repo root.

The script clones the [ovenmedialabs.com](https://github.com/OvenMediaLabs/ovenmedialabs.com)
repo into a per-product cache, copies your `docs/` into it (and
watches it so your edits hot-reload), and starts a dev server. When
it's ready you'll see:

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
- `OML_PREVIEW_HOST` (default `localhost`; set `0.0.0.0` to open the preview from another machine)
- `OML_PREVIEW_CACHE` (cache root path)
