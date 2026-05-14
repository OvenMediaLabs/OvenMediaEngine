# Authoring Guide — `docs/`

This folder contains the **MDX source** for the ovenmedialabs.com manual.
Edits land on the live site after a `git subtree pull` from this repo.

> Note: the legacy `docs/` (or `docs-enterprise/`) folder is still
> served by GitBook at the old domain. Once ovenmedialabs.com fully
> replaces it, the legacy folder will be retired. Until then, write
> new content **here**.

## TL;DR — Markdown changes from GitBook

| Need              | GitBook                                         | MDX (here)                                |
| ----------------- | ----------------------------------------------- | ----------------------------------------- |
| Callout / hint    | `{% hint style="info" %} ... {% endhint %}`     | `:::info` ... `:::`                       |
| Tabs              | `{% tabs %}{% tab title="X" %} ... {% endtabs %}` | `<Tabs><TabItem value="x" label="X"> ... </TabItem></Tabs>` |
| Image             | `<figure><img src=".gitbook/assets/x.png" /></figure>` | `![alt text](./images/x.png)`            |
| Page link         | `[label](page.md)` or `[[page]]`                | `[label](./relative/path.md)`             |
| External embed    | `<embed url="..."/>`                            | Plain link, or `<iframe src="..." />`     |
| Frontmatter       | GitBook generates it                            | Write your own (see below)                |

## Page frontmatter

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

## Admonition types

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

## Tabs

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

## Code blocks

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

## Images

Put images in `docs/images/` and reference them with a relative
path:

```mdx
![Architecture diagram](./images/architecture.png)
```

Subfolders work too: from `features/security/auth.md`, use
`../../images/auth.png`.

**Filename rule**: no spaces, no parens. Use `kebab-case` or
`snake_case`. (Spaces and `()` need URL-encoding, which is a footgun.)

## Characters that need escaping

MDX parses `<`, `{`, `}` as JSX. In plain text:

- `<` → `&lt;` (or wrap in backticks: `` `<992` ``)
- `{` → `&#123;`
- `}` → `&#125;`

Inside fenced code blocks (` ``` `) or inline code (`` ` ``), escape
nothing — those are raw.

## Local preview

If you want to see your changes before pushing:

```bash
# In a fresh clone of ovenmedialabs.com:
git subtree pull --prefix=docs/<source> git@github.com:OvenMediaLabs/<repo>.git <branch> --squash
npm install
npm start
# Visit http://localhost:3000/docs/<source>/...
```

(Or coordinate with whoever maintains ovenmedialabs.com to run a preview
deploy from your branch.)

## Sidebar order

Sidebar ordering follows `sidebar_position:` per file. For directory
labels and order, drop a `_category_.json`:

```json
{
  "label": "Security",
  "position": 5,
  "link": { "type": "doc", "id": "security/README" }
}
```

## Questions?

Ping `#docs` on Slack, or file an issue on this repo with the
`docs` label.
