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

Why v6 needs Qt 6.9+: pdf.js's legacy build targets Chrome 125+ from v4 on, and
the bundle is ESM-only. Qt WebEngine 6.9 ships Chromium 130 and 6.10 ships 134;
Qt 6.8.0-6.8.6 ship Chromium 122 (too old) and Qt 5.15 ships Chromium 83. The
built-in viewer is therefore compile-time gated to Qt >= 6.9; below it VNote
hands PDFs to the system default reader.

Because the bundle is ESM-only, the viewer page is served over VNote's own
`vxpdf://` URL scheme rather than `setHtml()`. See AGENTS.md in this folder
before touching anything here.
