# Third-Party Notices

VNote itself is licensed under the [GNU LGPLv3](COPYING.LESSER). This file records
the third-party material redistributed inside this repository and inside built
VNote binaries, together with the notices those licenses require us to keep.

## Scope

This file covers the bundled **icon sets**, **libsodium cryptography dependency**,
**Windows minisign verifier**, and **WebDAV transport/XML dependencies**.

It does **not** restate licenses that already ship next to the code they cover:

| Material | Where its license lives |
|---|---|
| pdf.js, and the CMaps / ICC profiles / standard fonts / WASM decoders it bundles | `src/data/extra/web/pdf.js/web/**/LICENSE*` |
| [KaTeX 0.16.22](https://github.com/KaTeX/KaTeX/releases/tag/v0.16.22), including its WOFF2 / WOFF / TTF fonts (MIT) | `src/data/extra/web/js/katex/LICENSE` |
| [html-to-image 1.11.13](https://github.com/bubkoo/html-to-image/tree/v1.11.13), used for KaTeX previews and raster exports (MIT) | `src/data/extra/web/js/html-to-image/LICENSE` |
| [reveal.js 6.0.2](https://github.com/hakimel/reveal.js/tree/6.0.2) core, black theme and white palette (MIT), including embedded Source Sans Pro fonts (SIL OFL 1.1) | `src/data/extra/web/js/reveal/LICENSE` and `LICENSE.font` |
| `libs/vxcore`, `libs/vtextedit`, `libs/QHotkey`, `libs/qwindowkit`, and the cmark fork they vendor | each submodule's own repository |

The KaTeX and html-to-image runtime files are unmodified upstream distribution files.
Their licenses are included in `vnote_extra.rcc` and installed alongside the extracted
web assets. KaTeX loads these local assets; MathJax retains its configurable script URL.

The reveal.js core has one VNote lifecycle patch: `destroy()` clears its scroll-prevention
interval and removes the load listener even before readiness. The styles and embedded fonts
are unmodified upstream distribution assets. `web/css/presentation.css` applies the upstream
6.0.2 white palette as scoped variable overrides, sharing the black theme’s fonts and layout.
Both license files ship in `vnote_extra.rcc`.

Icon provenance below was established by comparing SVG path data against upstream, not
by assuming from file names. Where that failed, the file says so — see
[Unresolved](#unresolved) rather than treating this document as complete.

---

## libsodium

Per-note encryption links the pinned libsodium library through the libsodium-cmake
build adapter. Both use the ISC license:

| Material | Pinned source | Copyright |
|---|---|---|
| libsodium | [93a7d0d41fe2e32409b5d00386946f491750b7de](https://github.com/jedisct1/libsodium/tree/93a7d0d41fe2e32409b5d00386946f491750b7de) | Copyright (c) 2013-2026, Frank Denis |
| libsodium-cmake | [9b2848dfc1b917a9410f0de9d81059b26cbfaa8d](https://github.com/robinlinden/libsodium-cmake/tree/9b2848dfc1b917a9410f0de9d81059b26cbfaa8d) | Copyright (c) 2019, Robin Linden |

The build copies the exact upstream `LICENSE` files as `LICENSE.libsodium` and
`LICENSE.libsodium-cmake`, without rewriting their text. Windows packages carry them
beside the executable; macOS bundles carry them in `Contents/Resources`; Linux
installs them under `${datadir}/licenses/vnote`. Standalone vxcore installs include
both under `${datadir}/licenses/vxcore`. These copies are required, not optional.

---

## minisign (Windows updater)

- **Upstream:** [minisign 0.11](https://github.com/jedisct1/minisign/releases/tag/0.11)
- **License:** ISC; [upstream LICENSE](https://github.com/jedisct1/minisign/blob/0.11/LICENSE)
- **Deployed files:** `updater/minisign.exe` and verbatim `updater/LICENSE.minisign`

The external Windows PowerShell updater uses this bundled executable to verify
release-manifest signatures. `prepare_win_updater` pins the upstream archive, executable
and license by SHA-256; the license is a required, non-optional Windows package file.
This notice does not replace or remove the separate libsodium notices above.

---

## WebDAV transport and XML

| Material | Pinned source | License and copyright |
|---|---|---|
| libcurl (Windows/Linux) | [curl 8.22.0](https://github.com/curl/curl/releases/tag/curl-8_22_0) | curl license; Copyright (c) 1996–2026 Daniel Stenberg and contributors |
| pugixml | [pugixml 1.16](https://github.com/zeux/pugixml/releases/tag/v1.16) | MIT; Copyright (c) 2006–2026 Arseny Kapoulkine |

`libs/vxcore/third_party/CMakeLists.txt` pins release archives by SHA-256. The build installs
verbatim upstream `COPYING` / `LICENSE.md` as `LICENSE.curl` / `LICENSE.pugixml` beside Windows
executables and under `${datadir}/licenses/vnote` on Linux; standalone vxcore uses
`${datadir}/licenses/vxcore`. macOS links the SDK/system libcurl rather than redistributing a
new curl runtime; its bundle carries `LICENSE.pugixml` in `Contents/Resources`.

---

## Lucide

- **Upstream:** https://github.com/lucide-icons/lucide
- **License:** ISC, with an MIT-licensed subset inherited from Feather
- **Full text:** [`licenses/Lucide-LICENSE.txt`](licenses/Lucide-LICENSE.txt) (verbatim copy of upstream `LICENSE`)

Covers 60 files in `src/data/core/icons/`:

- 58 carry `class="lucide lucide-<name>"`, which also records the upstream icon name;
- `read_only.svg` and `theme_switcher.svg` carry no class but are path-identical to
  upstream `pen-off` and `shirt`.

The files are modified from upstream in one respect: VNote rewrites Lucide
`currentColor` stroke and fill values to explicit `#000000` so
`IconUtils::fetchIcon` recolors them per theme.

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

The icon-set notices above ship in the **source tree** only. They are not
compiled into `core.qrc` and VNote has no About dialog that displays them, so a
user receiving only a built binary does not receive those icon notices. This
does not describe the separate dependency license files installed above,
including `updater/LICENSE.minisign`. Both the ISC and MIT texts ask that the
notice appear "in all copies". Closing the icon-notice gap properly means either
shipping this file alongside the binary from `src/Packaging.cmake` or surfacing
it in the UI — a packaging/product decision, not made here.
