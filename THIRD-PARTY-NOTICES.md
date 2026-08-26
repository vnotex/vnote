# Third-Party Notices

VNote itself is licensed under the [GNU LGPLv3](COPYING.LESSER). This file records
the third-party material redistributed inside this repository and inside built
VNote binaries, together with the notices those licenses require us to keep.

## Scope

This file covers the bundled **icon sets**, which had no notice anywhere in the tree.

It does **not** restate licenses that already ship next to the code they cover:

| Material | Where its license lives |
|---|---|
| pdf.js, and the CMaps / ICC profiles / standard fonts / WASM decoders it bundles | `src/data/extra/web/pdf.js/web/**/LICENSE*` |
| `libs/vxcore`, `libs/vtextedit`, `libs/QHotkey`, `libs/qwindowkit`, and the cmark fork they vendor | each submodule's own repository |

Provenance below was established by comparing SVG path data against upstream, not
by assuming from file names. Where that failed, the file says so — see
[Unresolved](#unresolved) rather than treating this document as complete.

---

## Lucide

- **Upstream:** https://github.com/lucide-icons/lucide
- **License:** ISC, with an MIT-licensed subset inherited from Feather
- **Full text:** [`licenses/Lucide-LICENSE.txt`](licenses/Lucide-LICENSE.txt) (verbatim copy of upstream `LICENSE`)

Covers 57 files in `src/data/core/icons/`:

- 55 carry `class="lucide lucide-<name>"`, which also records the upstream icon name;
- `read_only.svg` and `theme_switcher.svg` carry no class but are path-identical to
  upstream `lock` and `shirt`.

The files are modified from upstream in one respect: Lucide ships
`stroke="currentColor"`, and VNote rewrites that to an explicit
`stroke="#000000"` so `IconUtils::fetchIcon` recolors them per theme.

### The Feather / MIT subset

Lucide's `LICENSE` places icons inherited from [Feather](https://github.com/feathericons/feather)
under **MIT, Copyright (c) 2013-present Cole Bemis** rather than ISC. These VNote
files are in that subset:

| File | Upstream icon |
|---|---|
| `src/data/core/icons/add.svg` | `plus` |
| `src/data/core/icons/apply_editor.svg` | `check` |
| `src/data/core/icons/busy.svg` | `loader` |
| `src/data/core/icons/close.svg` | `x` |
| `src/data/core/icons/info.svg` | `info` |
| `src/data/core/icons/lock.svg` | `lock` |
| `src/data/core/icons/move.svg` | `move` |
| `src/data/core/icons/read_only.svg` | `lock` |
| `src/data/core/icons/search.svg` | `search` |
| `src/data/core/icons/textbox_editor.svg` | `type` |

Both license texts are reproduced in `licenses/Lucide-LICENSE.txt`.

> Recheck this table when adding a Lucide icon. The MIT list is upstream's, it is
> not guessable from the file name, and it changes as Lucide evolves.

---

## IconPark

- **Upstream:** https://github.com/bytedance/IconPark
- **Copyright:** Copyright (c) 2020 ByteDance Inc.
- **License:** Apache License 2.0
- **Full text:** [`licenses/Apache-2.0.txt`](licenses/Apache-2.0.txt)

Covers the older icons, drawn on a `48x48` viewBox:

- 31 files in `src/data/core/icons/` (those without a `lucide` class)
- 263 files in `src/data/extra/themes/*/icons/` — per-theme copies, recolored

Four were spot-checked against upstream and match byte-for-byte in their `d`
attributes:

| VNote file | IconPark source |
|---|---|
| `type_italic_editor.svg` | `source/Edit/text-italic.svg` |
| `type_bold_editor.svg` | `source/Edit/text-bold.svg` |
| `type_code_editor.svg` | `source/Edit/code.svg` |
| `type_quote_editor.svg` | `source/Edit/quote.svg` |

The rest share the same generator signature but were not individually verified.

These are **modified** from upstream: recolored, and in the theme copies the
stroke color is baked per theme. Apache-2.0 §4(b) requires modified files to
carry prominent notice of the change; this section is that notice for the set.

---

## Unresolved

The following are redistributed but their license could **not** be determined,
so no claim is made about them here. They need a maintainer decision — either
confirm the origin and add it above, or replace the files.

### iconfont.cn exports (37 files)

Identifiable by the `t="<timestamp>"` and `p-id="<n>"` attributes their exporter
injects, on a `1024x1024` viewBox:

- `src/data/core/icons/`: `maximize.svg`, `maximize_restore.svg`, `minimize.svg`
- `src/data/extra/themes/*/icons/`: 34 files

iconfont.cn is an **aggregator**: licensing is per-uploader and is not recorded in
the exported SVG, so it cannot be recovered from the file. These three are the
window caption buttons, which is worth knowing before replacing them.

### Unmatched

- `src/data/core/icons/united_entry.svg` — Lucide's shape conventions (24x24,
  `stroke-width="2"`, and still `stroke="currentColor"`) but no upstream Lucide
  match was found. Possibly a modified Lucide icon or an original.
- `src/data/extra/themes/vx-idea/branch_closed.svg`, `branch_open.svg` — hand-authored
  (SVG-edit output: `<title>Layer 1</title>`, `id="svg_1"`), most likely original
  to VNote.

---

## Known gap

These notices ship in the **source tree** only. They are not compiled into
`core.qrc` and VNote has no About dialog that displays them, so a user who
receives only a built binary does not receive the notices with it. Both the ISC
and MIT texts ask that the notice appear "in all copies". Closing that properly
means either shipping this file alongside the binary from `src/Packaging.cmake`
or surfacing it in the UI — a packaging/product decision, not made here.
