https://github.com/mozilla/pdf.js
Legacy build, from the GitHub release ZIP (the npm `pdfjs-dist` package does NOT
contain `web/viewer.html`, `web/viewer.mjs`, `web/locale/` or `web/cmaps/`):
https://github.com/mozilla/pdf.js/releases/download/v6.2.108/pdfjs-6.2.108-legacy-dist.zip
v6.2.108

VNote-owned files in this folder are NOT part of pdf.js and must be preserved
across an upgrade:
- pdfviewer.mjs
- pdfviewercore.js
- pdfviewer.css
- web/pdf-viewer-template.html (stock `web/viewer.html` with three edits: the
  `<title>`, the removal of the stock `Content-Security-Policy` meta, and the
  VX_STYLES_PLACEHOLDER / VX_SCRIPTS_PLACEHOLDER comments in place of the stock
  `<link>`/`<script>` tags — the `<link rel="resource" ... locale/locale.json>`
  is KEPT)

`web/locale/` is deliberately trimmed to a subset of the upstream locales. v6
uses a Fluent manifest: `web/locale/locale.json` maps a LOWERCASED tag to
`<Tag>/viewer.ftl`. Keep it in sync with the folders actually vendored, and keep
`en-US` (the fallback locale) whatever else you trim.

Not vendored from the ZIP: `*.map` source maps, `web/debugger.{mjs,css}`,
`web/compressed.tracemonkey-pldi-09.pdf`, and the top-level `LICENSE`.

## Upgrade checklist

After bumping the vendored tree, re-verify these pdf.js INTERNALS. They are not
API and they carry no deprecation warning; a rename fails silently, and the only
symptom is a control that stopped working.

- `#toolbarContainer` — the id `pdfviewer.css` collapses. **It is NOT
  `display: none`**: `#viewsManager` (the sidebar) is a descendant of it, so the
  CSS hides the strip's chrome piece by piece instead. If either id is renamed,
  the built-in toolbar reappears or the sidebar becomes invisible.
  `PdfViewerCore.checkBuiltInToolbar()` warns about both; check the log rather
  than trusting the eye.
- `#viewsManager` — still nested under
  `#toolbarContainer > #toolbarViewer > #toolbarViewerLeft`, still
  `position: absolute` against `#toolbarContainer`. If pdf.js moves it out, the
  CSS gymnastics in `pdfviewer.css` can be simplified back to a plain
  `display: none` on the container.
- `MIN_SCALE` / `MAX_SCALE` (currently `0.1` / `25.0`) — duplicated as
  `VX_MIN_SCALE` / `VX_MAX_SCALE` in `pdfviewercore.js` and as
  `c_minViewerScale` / `c_maxViewerScale` in `pdfvieweradapter.cpp`. Too tight a
  ceiling silently rejects a legitimate state and leaves the toolbar stale.
- `--toolbar-height` — still consumed by `#viewerContainer`'s inset AND by
  `#toolbarSidebar`'s height AND by `#sidebarContent`'s inset. `pdfviewer.css`
  overrides only `#viewerContainer`'s block-start inset for exactly that reason;
  if the shape changed, re-derive the override rather than zeroing the variable.
- `PDFViewerApplication.viewsManager` — the sidebar owner. There is no
  `pdfSidebar` in v6; if it moves again, `toggleSidebar()` becomes a dead button.
- The eventBus vocabulary the viewer bridge depends on: the STATE events
  `pagechanging` / `pagesloaded` / `scalechanging` / `rotationchanging` /
  `scrollmodechanged` / `spreadmodechanged` / `cursortoolchanged` /
  `sidebarviewchanged`, and the COMMAND events `pagenumberchanged` /
  `scalechanged` / `zoomin` / `zoomout` / `switchscrollmode` /
  `switchspreadmode` / `switchcursortool` / `documentproperties`.
  (`presentationmode` and `print` are deliberately NOT used — see the AGENTS.md
  section on why neither verb can be completed from inside the page. Do not
  "restore" them on an upgrade.)
- The `find` payload shape (`{type, query, caseSensitive, entireWord,
  highlightAll, findPrevious, matchDiacritics}`), `findbarclose`, and that
  `updatefindmatchescount` still reports `matchesCount.current` **1-based**.

`tests/widgets/test_pdfviewercore_js.cpp` gates the VNote side of all of these
against the real shipped files, but it drives a FAKE eventBus — it cannot tell
you that pdf.js renamed something.

Why v6 needs Qt 6.9+: pdf.js's legacy build targets Chrome 125+ from v4 on, and
the bundle is ESM-only. Qt WebEngine 6.9 ships Chromium 130 and 6.10 ships 134;
Qt 6.8.0-6.8.6 ship Chromium 122 (too old) and Qt 5.15 ships Chromium 83. The
built-in viewer is therefore compile-time gated to Qt >= 6.9; below it VNote
hands PDFs to the system default reader.

Because the bundle is ESM-only, the viewer page is served over VNote's own
`vxpdf://` URL scheme rather than `setHtml()`. See AGENTS.md in this folder
before touching anything here.
