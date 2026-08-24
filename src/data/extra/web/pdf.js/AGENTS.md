# pdf.js (vendored) — VNote PDF viewer assets

Vendoring rules, VNote-owned files and the upgrade procedure live in
[README.md](README.md) next to this file (it is the file a non-agent contributor reads first).
This document holds the knowledge that is NOT obvious from the vendored tree.

---

## KNOWN BROKEN: the PDF viewer does not work on the Qt 5.15 / Windows 7 build

**Status: known, accepted, NOT fixed.** Do not file it as a regression, and do not
"fix" it with a one-off patch to a vendored file (see [Do not hand-patch](#do-not-hand-patch-the-vendored-bundles) below).

### Symptom

Opening any PDF in the `win64-windows7` (Qt 5.15.2) package logs three errors and shows an
empty viewer:

```
Critical:(...:4003) Uncaught SyntaxError: Unexpected token '='
Critical:(...:2729) Uncaught SyntaxError: Unexpected token '='
Critical:(...:29)   Uncaught ReferenceError: pdfjsLib is not defined
```

Qt 6 builds (Windows/Linux/macOS) are unaffected.

### Cause

| | |
|---|---|
| Vendored viewer | pdf.js **v3.11.174**, the **legacy** dist (verified byte-identical to the upstream `pdfjs-3.11.174-legacy-dist.zip` — this is not a mis-vendoring) |
| pdf.js v3 legacy browser target | ~Chrome 92 |
| Qt 5.15.2 QtWebEngine | **Chromium 83** |

The bundle therefore contains syntax Chromium 83 cannot **parse**:

| Feature | Shipped in | Occurrences (`build/pdf.js` + `web/viewer.js` + `pdf.worker.js`) |
|---|---|---|
| logical assignment `\|\|=`, `??=`, `&&=` | Chrome 85 | 22 + 23 + 41 |
| private methods / `static #x` | Chrome 84 | 299 + 307 + 95 |

The two `SyntaxError`s in the log are exactly `build/pdf.js:4003`
(`(intentState.renderTasks ||= new Set())`) and `web/viewer.js:2729`
(`this._contentDispositionFilename ??= …`). Because `build/pdf.js` fails to parse as a whole,
the global `pdfjsLib` is never created, which is the third error (raised from
`pdfviewer.js:29`, `pdfjsLib.GlobalWorkerOptions.workerSrc = …`).

The scripts are injected as plain `<script src>` tags by
`HtmlTemplateService::fillResources`, from the list in
[`src/core/pdfviewerconfig.cpp`](../../../core/pdfviewerconfig.cpp)
(`PdfViewerConfig::defaultViewerResource`). Nothing in VNote's own JS is at fault, and
`pdfviewercore.js` / `pdfviewer.js` themselves are ES5-compatible.

### Do not trust the file name in those log lines

`web/js/markdownviewer.js` is 80 lines long, so `markdownviewer.js:4003` cannot be real.
QtWebEngine's default `javaScriptConsoleMessage` builds a `QMessageLogContext` from a
**temporary** UTF-8 buffer of `sourceID`, so the `const char *file` VNote's `Logger` prints can
be a dangling/recycled pointer naming some other script. **The line numbers are accurate; the
file name is not.** (`WebPage::javaScriptConsoleMessage`, `src/widgets/webpage.cpp:34`, only
intercepts `InfoMessageLevel`; warnings and errors fall through to the Qt default.)

### Why it is not fixed

Every route costs more than the platform is worth today:

| Option | Cost |
|---|---|
| Transpile the 4 bundles with `@babel/preset-env` `targets: chrome 83` and vendor the output | A ~4 MB vendored diff that no longer matches any upstream artifact, plus a Node/Babel step in the vendoring procedure |
| Downgrade to pdf.js v2.16.105 (last v2; its legacy dist is ES5) | Re-derive `web/pdf-viewer-template.html` from v2's `viewer.html`, re-verify the `pdfviewercore.js` outline bridge, new CSS/images/locales — and it downgrades the viewer on *every* platform |
| Ship a second, Chromium-83-safe asset tree selected per build | ~10 MB of duplicate assets, CMake/packaging branching, two code paths to test |

Users on the Qt 5.15 / Windows 7 package should open PDFs in an external viewer.

### The viewer is compile-time gated off on Qt 5 (since 2026-08)

VNote no longer tries to render the broken viewer. Two `QT_VERSION` gates:

| Site | Effect on Qt 5 |
|---|---|
| `ViewWindowFactory::registerBuiltInCreators` (`src/gui/services/viewwindowfactory.cpp`) | The `"Pdf"` creator (and its `pdfviewwindow2.h` include) is not compiled in, so `hasCreator("PDF")` is false. `ViewAreaController::openBuffer` then takes its existing `QDesktopServices::openUrl` fallback and hands the PDF to the system default reader; a per-suffix external program configured in Settings still wins, as it is checked earlier. |
| `ViewAreaController::openRestoredBuffer` (`src/controllers/viewareacontroller.cpp`) | A restored PDF tab is skipped with one `qInfo` line instead of raising a `viewWindowCreationFailed` notification at every startup. Nothing is closed or unregistered: the buffer stays in the workspace, so the same config opened under Qt 6 restores the tab intact. |

`FileTypeCoreService`, vxcore's file-type config and the user's file associations are
deliberately **untouched** — a `.pdf` still resolves to the `PDF` type everywhere. Filtering
the type instead would map PDFs onto the editable, auto-saving `TextViewWindow2`, and the
File Associations page's get→edit→set round trip would permanently delete the PDF entry from
`vxcore.json`. `PdfViewWindow2`, `PdfViewWindowController`, `PdfViewerConfig` and
`pdfviewercore.js` all stay compiled and tested; only instantiation is gated. If the bundles
are ever transpiled per the section below, delete both gates.

### If you do decide to fix it

The check to run first is a syntax-only one; do not assume `||=` is the whole gap:

```powershell
rg -c "(\|\||\?\?|&&)=" src/data/extra/web/pdf.js/build/pdf.js src/data/extra/web/pdf.js/web/viewer.js
rg -c "#[a-zA-Z_]+\(|static #"  src/data/extra/web/pdf.js/build/pdf.js src/data/extra/web/pdf.js/web/viewer.js
```

`build/pdf.worker.js` and `build/pdf.sandbox.js` must be converted together with the two
above — the worker parses in its own realm and fails independently of the main frame.

---

## Do not hand-patch the vendored bundles

`build/*.js`, `web/viewer.js`, `web/viewer.css`, `web/locale/`, `web/cmaps/`,
`web/standard_fonts/` and `web/images/` are upstream artifacts and are replaced wholesale on
upgrade. A local edit inside them is silently lost at the next bump and has no test guarding
it. The VNote-owned files are only the five listed in [README.md](README.md)
(`pdfviewer.js`, `pdfviewercore.js`, `pdfviewer.css`, `web/pdf-viewer-template.html`, and
`README.md` itself). Any behavioural change belongs in those, or in a documented,
reproducible post-processing step applied to the whole vendored tree.

---

## Related

| Where | What |
|---|---|
| [README.md](README.md) | Upstream source, version, VNote-owned files, what is deliberately not vendored |
| [`src/core/pdfviewerconfig.cpp`](../../../core/pdfviewerconfig.cpp) | The script/style load order injected into the template |
| [`src/core/services/htmltemplateservice.cpp`](../../../core/services/htmltemplateservice.cpp) | `VX_STYLES_PLACEHOLDER` / `VX_SCRIPTS_PLACEHOLDER` substitution |
| [`src/widgets/editors/pdfvieweradapter.cpp`](../../../widgets/editors/pdfvieweradapter.cpp) | C++ side of the QWebChannel outline bridge |
| [`.github/AGENTS.md`](../../../../.github/AGENTS.md) | The Windows 7 / Qt 5.15 packaging variant this limitation is scoped to |
