https://github.com/mozilla/pdf.js
Legacy build, from the GitHub release ZIP (the npm `pdfjs-dist` package does NOT
contain `web/viewer.html`, `web/viewer.js`, `web/locale/` or `web/cmaps/`):
https://github.com/mozilla/pdf.js/releases/download/v3.11.174/pdfjs-3.11.174-legacy-dist.zip
v3.11.174

VNote-owned files in this folder are NOT part of pdf.js and must be preserved
across an upgrade:
- pdfviewer.js
- pdfviewercore.js
- pdfviewer.css
- web/pdf-viewer-template.html (stock `web/viewer.html` with three edits: the
  `<title>`, and the VX_STYLES_PLACEHOLDER / VX_SCRIPTS_PLACEHOLDER comments in
  place of the stock `<link>`/`<script>` tags)

`web/locale/` is deliberately trimmed to a subset of the upstream locales; keep
`web/locale/locale.properties` in sync with the folders actually vendored.

Not vendored from the ZIP: `*.map` source maps, `web/debugger.{js,css}`,
`web/compressed.tracemonkey-pldi-09.pdf`, and the top-level `LICENSE`.

Why v3 and not v4+: pdf.js's legacy build requires Chrome 125+ from v4 on, which
no Qt version VNote ships can meet. v3.11.174 is the last v3 release.

Note that v3's legacy build targets ~Chrome 92 and therefore does NOT run on
Qt 5.15's QtWebEngine (Chromium 83) either: the PDF viewer is known broken on the
win64-windows7 package. See AGENTS.md in this folder before touching anything here.
