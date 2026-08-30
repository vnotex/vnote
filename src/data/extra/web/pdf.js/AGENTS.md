# pdf.js (vendored) — VNote PDF viewer assets

Vendoring rules, VNote-owned files and the upgrade procedure live in
[README.md](README.md) next to this file (it is the file a non-agent contributor reads first).
This document holds the knowledge that is NOT obvious from the vendored tree.

---

## The built-in viewer requires Qt 6.9+

**Status: known, accepted, by design.** Below the floor, VNote hands PDFs to the system
default reader; this is not a regression and must not be "fixed" by patching a vendored file
(see [Do not hand-patch](#do-not-hand-patch-the-vendored-bundles) below).

### Why the floor is 6.9

| | |
|---|---|
| Vendored viewer | pdf.js **v6.2.108**, the **legacy** dist |
| pdf.js legacy browser target (v4+) | **Chrome 125+** |
| pdf.js module format (v4+) | **ESM only** — `build/pdf.mjs`, `build/pdf.worker.mjs`, `web/viewer.mjs` |

Qt WebEngine → Chromium: **6.10 → 134**, **6.9 → 130**, 6.8.0-6.8.6 → 122 (too old),
6.7 → 118, 5.15.2 → 83. So 6.9 is the first release series that clears the Chrome-125 floor
across the whole 6.9.x/6.10.x range. (6.8.7 and 6.8.8 happen to ship Chromium 134 too, but the
rest of 6.8.x does not; a `QT_VERSION_CHECK(6,8,7)` gate would be expressible and is
deliberately not used — it buys nothing and adds a branch.)

The ESM requirement is the second, independent reason: the pre-v4 integration built the page
with `QWebEngineView::setHtml()`, which yields an opaque `data:` origin. A module fetched from
such a page to a `file://` URL is cross-origin and Chromium blocks it. The fix is the
`vxpdf://` scheme below, which needs `QWebEngineUrlScheme` — Qt 6 only.

### Compile-time gates

| Site | Effect below Qt 6.9 |
|---|---|
| `ViewWindowFactory::registerBuiltInCreators` (`src/gui/services/viewwindowfactory.cpp`) | The `"Pdf"` creator (and its `pdfviewwindow2.h` include) is not compiled in, so `hasCreator("PDF")` is false. `ViewAreaController::openBuffer` then takes its existing `QDesktopServices::openUrl` fallback and hands the PDF to the system default reader; a per-suffix external program configured in Settings still wins, as it is checked earlier. |
| `ViewAreaController::openRestoredBuffer` (`src/controllers/viewareacontroller.cpp`) | A restored PDF tab is skipped with one `qInfo` line instead of raising a `viewWindowCreationFailed` notification at every startup. Nothing is closed or unregistered: the buffer stays in the workspace, so the same config opened under Qt 6.9+ restores the tab intact. |
| `VxPdfSchemeHandler` (`src/gui/services/vxpdfschemehandler.{h,cpp}`), its registration in `main.cpp` and its installation in `WebEngineProfileService` | Not compiled / not installed. |

`FileTypeCoreService`, vxcore's file-type config and the user's file associations are
deliberately **untouched** — a `.pdf` still resolves to the `PDF` type everywhere. Filtering
the type instead would map PDFs onto the editable, auto-saving `TextViewWindow2`, and the
File Associations page's get→edit→set round trip would permanently delete the PDF entry from
`vxcore.json`. `PdfViewWindow2`, `PdfViewWindowController`, `PdfViewerConfig` and
`pdfviewercore.js` all stay compiled and tested; only instantiation is gated.

### Do not trust the file name in JS console log lines

`web/js/markdownviewer.js` is 80 lines long, so a `markdownviewer.js:4003` in the log cannot
be real. QtWebEngine's default `javaScriptConsoleMessage` builds a `QMessageLogContext` from a
**temporary** UTF-8 buffer of `sourceID`, so the `const char *file` VNote's `Logger` prints can
be a dangling/recycled pointer naming some other script. **The line numbers are accurate; the
file name is not.** (`WebPage::javaScriptConsoleMessage`, `src/widgets/webpage.cpp`, only
intercepts `InfoMessageLevel`; warnings and errors fall through to the Qt default.)

---

## The `vxpdf://` URL scheme

URL contract, builders and the path-safety predicate: [`src/core/vxpdfscheme.h`](../../../vxpdfscheme.h)
(header-only, Qt-Core-only, so `core_services` can use it without a QtWebEngine dependency).
Handler: [`src/gui/services/vxpdfschemehandler.cpp`](../../../../gui/services/vxpdfschemehandler.cpp).

### Registration flags (validated by a Phase 0 spike, Windows, Qt 6.10.3)

```
Syntax::Host   +  SecureScheme | CorsEnabled | FetchApiAllowed
```

`Syntax::Host` gives the scheme a stable authority, which is what makes the same-origin policy
(and therefore ES module loading) work at all. `LocalAccessAllowed` is deliberately **not**
set: nothing the viewer loads is a `file:` URL any more. Do not add `LocalScheme`,
`NoAccessAllowed` or service-worker flags without a demonstrated need.

`QWebEngineUrlScheme::registerScheme()` MUST run before the `QApplication` is constructed —
QtWebEngine snapshots the registry at that point. It is called from `main.cpp` immediately
before `Application app(argc, argv)`.

### Routes

| Route | Serves |
|---|---|
| `vxpdf://pdf/asset/web/pdf.js/web/pdf-viewer-template.html` | the **generated** template from `HtmlTemplateService::getPdfViewerTemplate()` (never the file on disk) |
| `vxpdf://pdf/asset/<config-relative path under `web/`>` | the extracted asset tree in appData, resolved via `ConfigMgr2::getFileFromConfigFolder()` |
| `vxpdf://pdf/document/<token>` | the PDF bytes for a token registered through `WebEngineProfileService::registerPdfDocument()` |

Nothing else is served. GET/HEAD only. Absolute paths, `..`/`.` segments (encoded or not),
backslashes and anything outside `web/` are rejected by `VxPdfScheme::isSafeAssetPath()`.

**The viewer page's path is load-bearing.** Every pdf.js default option is resolved relative
to the *document* URL: `./images/`, `locale/locale.json`, `../web/cmaps/`,
`../web/standard_fonts/`, `../build/pdf.worker.mjs`. Serving the page at
`.../web/pdf.js/web/<something>.html` — i.e. exactly where the stock `web/viewer.html` sits —
is what makes all of them resolve with zero AppOptions overrides. Do not move it up or down a
directory.

### Reply device lifetime

`QWebEngineUrlRequestJob::reply()` does not take ownership synchronously. Every `QFile` /
`QBuffer` handed to it is **parented to the job**; a stack-local device is destroyed before
Chromium finishes reading.

### MIME types are explicit

Chromium applies **strict** MIME checking to module scripts: an empty or generic type on a
`.mjs` fails the module load. The map in `vxpdfschemehandler.cpp` covers `text/html`,
`text/css`, `text/javascript` (`.mjs`/`.js`), `application/json`, `application/pdf`,
`application/wasm`, SVG/PNG/GIF, the fonts, and `application/octet-stream` for `.bcmap` /
`.pfb` / `.icc`.

### Token lifetime

`PdfViewWindow2` owns exactly one token at a time. `syncEditorFromBuffer()` uses
**replace-then-revoke**: it registers the new document, loads the page, and only then revokes
the previous token — so a theme reload (which force-regenerates the template and re-syncs)
never leaves a window pointing at a revoked document, and the registry cannot grow for the
process lifetime. The destructor revokes whatever is left.

### Navigation policy

`WebPage` is shared by PDF, MindMap, Markdown and the Windows warm-up page, so the allowance is
per-consumer: `WebPage::setAllowedMainFrameUrlPredicate()` defaults to unset (byte-identical
behaviour for every existing consumer) and only `PdfViewer` installs one, matching **only** the
viewer route. `vx://home`, `vx://settings` and user-authored links keep flowing through
`externalLinkRequested`. The predicate is evaluated *before* the `isLocalFile()` branch so a
consumer-owned scheme can never be mistaken for a user link.

---

## The ESM load-order contract

`HtmlTemplateService::fillPdfResources()` emits `type="module"` for a `.mjs` source and
`type="text/javascript"` for everything else, decided **purely by file extension** — so the
persisted `WebResource` JSON needs no new key. Module scripts are deferred; classic scripts
are not. That gives two phases, and the script list in
[`src/core/pdfviewerconfig.cpp`](../../../core/pdfviewerconfig.cpp) is ordered around it:

| Phase | Scripts | Runs |
|---|---|---|
| classic (immediate, in order) | `web/js/qwebchannel.js`, `eventemitter.js`, `utils.js`, `vxcore.js`, `web/pdf.js/pdfviewercore.js` | during head parsing |
| module (deferred, in order) | `build/pdf.mjs`, `web/viewer.mjs`, `pdfviewer.mjs` | after parsing; `viewer.mjs` runs `PDFViewerApplication.run()` during its OWN evaluation, so `pdfviewer.mjs` is already post-init |

`pdfviewer.mjs` is a module **for its deferral, not for its module graph**: it contains no
`import`/`export`, because `tests/widgets/test_pdfviewercore_js.cpp` evaluates that exact file
with `QJSEngine`, which has no module loader. Keep it import-free.

Two consequences that bit during the upgrade:

- **There is no reliable `pdfjsLib` global** under ESM. Set the worker through the `workerSrc`
  AppOption; `viewer.mjs` feeds that into `GlobalWorkerOptions` itself.
- **A module is TOO LATE to configure AppOptions.** See below.

### AppOptions must be set from `webviewerloaded`, never from a module

`viewer.mjs` ends with:

```js
if (document.readyState === "interactive" || document.readyState === "complete") {
    webViewerLoad();            // -> PDFViewerApplication.run()
} else {
    document.addEventListener("DOMContentLoaded", webViewerLoad, true);
}
```

A deferred script — and **every `type="module"` is deferred** — runs *after* parsing completes,
and `readyState` is already `"interactive"` by then. So `viewer.mjs` takes the **first** branch
and runs `PDFViewerApplication.run()` synchronously during its own evaluation, **before**
`pdfviewer.mjs` (the next module in document order) executes at all.

Anything `pdfviewer.mjs` writes into AppOptions is therefore set *after* pdf.js has read it. It
**looks** like it worked — the value is in the options object, so a test that asserts the option
value goes green — but nothing consumes it. That is exactly how `sidebarViewOnLoad`,
`disablePreferences` and `annotationEditorMode` all silently did nothing until a browser probe
caught it.

`webviewerloaded` is pdf.js's documented hook: it is dispatched inside `webViewerLoad()`
immediately before `run()`. `pdfviewercore.js` is a **classic** script, so it runs before any
module and can register the listener in time; `PdfViewerCore::applyViewerOptions()` is the single
place every AppOption is set.

> Test the **hook**, not the value. `tests/widgets/test_pdfviewercore_js.cpp` asserts that a
> `'webviewerloaded'` listener is registered, that firing it sets the options, AND that the
> options are untouched before it fires. Asserting only the final value re-admits the bug.

### pdf.js's own annotation editors are DISABLED

v6's toolbar ships Comment / Signature / Highlight / Text / Draw / Image editors plus a built-in
comment sidebar. `applyViewerOptions()` sets `annotationEditorMode` to `-1`
(`AnnotationEditorType.DISABLE`), after which pdf.js hides `#editorModeButtons` and
`#editorModeSeparator` itself — no CSS override against its internal ids.

**This is a data-loss guard, not a UI preference.** Those tools mutate the IN-MEMORY PDF and are
persisted only by `PDFDocumentProxy.saveDocument()`, reached through Save/Download — which VNote
hides, because VNote never modifies the PDF binary. Left enabled they silently discard the user's
work at tab close, and their "Highlight" sits beside VNote's own doing something different and
incompatible. Do not re-enable them without first giving VNote a real save path for them.

---

## Extra-data extraction does NOT prune

`FileUtils2::installVersionedDir` compares/removes/writes a per-folder stamp and copies by
walking **source** entries only. A changed `ConfigMgr2::c_version` **does** force re-extraction
of `web/`, and new v6 files **do** overwrite matching deployed files, but files removed
upstream are **not** deleted from appData. After this upgrade an existing installation keeps
stale, unreferenced `web/pdf.js/build/pdf.js`, `pdf.worker.js`, `pdf.sandbox.js`,
`web/viewer.js`, `pdfviewer.js` and `web/locale/*/viewer.properties`.

**This is accepted, not a bug.** Nothing references them (the script list is force-reset by the
config migration below), they cost a few MB of disk, and a pruning step scoped to
`web/pdf.js/**` would have to special-case the VNote-owned files and would be the only
destructive operation in the extraction path. The `web` preserve list contains only
`css/user.css`, so nothing blocks overwriting `web/pdf.js/**`.

## The config migration is mandatory

`editor.pdf_viewer.viewerResource` is persisted per user, and `WebResource::init()` takes the
persisted object **wholesale** (it resizes the resource vector from the JSON). There is no
app-vs-user merge on that path. Every existing installation therefore pins the v3 script list,
and without a forced reset the viewer would load three files that no longer exist and render a
blank page.

`MainConfig::doVersionSpecificOverride` resets `pdf_viewer.viewerResource` to
`PdfViewerConfig::defaultViewerResource()` for any previous version below 4.6.0. `MainConfig`
is a friend of `PdfViewerConfig`, so it can reach the private default.

---

## Do not hand-patch the vendored bundles

`build/*.mjs`, `web/viewer.mjs`, `web/viewer.css`, `web/locale/`, `web/cmaps/`, `web/iccs/`,
`web/wasm/`, `web/standard_fonts/` and `web/images/` are upstream artifacts and are replaced
wholesale on upgrade. A local edit inside them is silently lost at the next bump and has no
test guarding it. The VNote-owned files are only the five listed in [README.md](README.md)
(`pdfviewer.mjs`, `pdfviewercore.js`, `pdfviewer.css`, `web/pdf-viewer-template.html`, and
`README.md` itself). Any behavioural change belongs in those, or in a documented,
reproducible post-processing step applied to the whole vendored tree.

---

## Related

| Where | What |
|---|---|
| [README.md](README.md) | Upstream source, version, VNote-owned files, what is deliberately not vendored |
| [`src/core/vxpdfscheme.h`](../../../vxpdfscheme.h) | The `vxpdf://` URL contract, shared by the template generator and the handler |
| [`src/gui/services/vxpdfschemehandler.cpp`](../../../../gui/services/vxpdfschemehandler.cpp) | Scheme registration, routes, MIME map, token registry |
| [`src/core/pdfviewerconfig.cpp`](../../../core/pdfviewerconfig.cpp) | The script/style load order injected into the template |
| [`src/core/services/htmltemplateservice.cpp`](../../../core/services/htmltemplateservice.cpp) | `fillPdfResources()`: `VX_STYLES_PLACEHOLDER` / `VX_SCRIPTS_PLACEHOLDER` substitution with `vxpdf://` URLs |
| [`src/widgets/editors/pdfvieweradapter.cpp`](../../../widgets/editors/pdfvieweradapter.cpp) | C++ side of the QWebChannel outline bridge |
| [`tests/gui/test_vxpdfschemehandler.cpp`](../../../../tests/gui/test_vxpdfschemehandler.cpp) | MIME map + token registry gate (routing needs a real `QWebEngineUrlRequestJob`, which a test cannot construct) |
| [`tests/utils/test_extra_qrc_coverage.cpp`](../../../../tests/utils/test_extra_qrc_coverage.cpp) | disk ↔ `extra.qrc` ↔ `locale/locale.json` three-way coverage gate |
| [`.github/AGENTS.md`](../../../../.github/AGENTS.md) | The Windows 7 / Qt 5.15 packaging variant, which has no built-in PDF viewer |

---

## The comment / highlight overlay

VNote owns highlighting entirely. pdf.js's own annotation editors are NOT used: there is no public
API to restore editor state from external JSON, binding to `AnnotationEditorUIManager` internals
would break at the next bump, and none of it would carry over to other file types. The PDF binary
is **never modified**.

All overlay code lives in the VNote-owned files (`pdfviewercore.js`, `pdfviewer.mjs`,
`pdfviewer.css`). Never hand-patch `build/*.mjs` or `web/viewer.mjs` — see
[Do not hand-patch](#do-not-hand-patch-the-vendored-bundles).

### Anchors are stored in PDF page space

`clientRectToPdfQuad()` / `pdfQuadToPageBox()` are `static` on `PdfViewerCore` and are pure math:
they take a viewport and return numbers, with no DOM. That is what lets
`tests/widgets/test_pdfviewercore_js.cpp` exercise them under QJSEngine against the REAL shipped
file.

Storing **page-space quads** (not CSS pixels) is the whole point: a zoom, rotation or resize only
re-projects, so `pagerendered` / `scalechanging` / `rotationchanging` / `updateviewarea` just
re-render from the same stored numbers. A highlight captured at 100% lands in exactly the same
place at 300%; `projectionRoundTripsThroughAScaledViewport` is the gate.

A selection spanning a page break becomes **one anchor per page** (`groupRectsByPage`), because an
anchor carries a single `page` field. Collapsed (zero-area) caret rects are dropped, and the quad
count is capped.

> The test harness reaches the statics through `window.vxcore.constructor`, NOT through
> `PdfViewerCore`. A top-level `class` is a lexical binding: it does not land on the global object
> and is invisible to a later, separate `QJSEngine::evaluate()` call.

### The three tools

Three authoring modes, mirroring pdf.js's own toolbar layout, on the VNote
view-window toolbar (`PdfViewWindow2::setupAnnotationToolBarActions`):

| Tool | Gesture | Anchor written |
|---|---|---|
| Highlight | drag over text | `pdf-quads` |
| Draw | drag on the page (one drag = one comment) | `pdf-ink` |
| Text box | click to place, then type IN the box; one-shot, then disarms | `pdf-freetext` |

**A MODE is the point.** Arm Highlight once and every selection is captured,
instead of a context-menu round trip per selection — that is the only reason
pdf.js's buttons felt better, and it costs nothing to copy.

**Each tool owns its OWN settings**, persisted under `PdfViewerConfig`'s
`tools` object and keyed by the *same* tool strings
`PdfViewerAdapter::toolToString()` and this file use — one vocabulary end to
end, so a value round-trips config → C++ → JS with no translation table to
drift:

| Tool | Key | Options |
|---|---|---|
| Highlight | `highlight` | `color` |
| Draw | `ink` | `color`, `width` (PDF units), `opacity` (0.1-1.0) |
| Text box | `freetext` | `color`, `fontSize` (PDF units) |

`VX_INK_WIDTH` / `VX_INK_OPACITY` / `VX_FREETEXT_FONT_SIZE` are **defaults only**; the live values
come from `this.toolOptions`, read through `optionsFor(tool)`. The C++ side
publishes them with `toolOptionsChanged(tool, options)`, and the readiness latch
republishes **every** tool (not just the armed one) so a reloaded page comes up
fully configured. `setCommentColor()` survives as a thin alias that sets **the
highlight colour only** — the page context menu carries an explicit colour with
its `captureSelectionRequested`.

Each toolbar button is a `QToolButton::MenuButtonPopup`: clicking the **body**
arms/disarms the tool, clicking the **indicator** opens that tool's settings
menu: five colour presets, plus **sliders** — Thickness + Opacity for Draw, Font
size for Text box. The slider rows are `QWidgetAction`s (a plain action would
close the menu on the first click and make the slider undraggable) and commit on
`valueChanged`, so the draft stroke previews live. The whole thing lives in
`PdfAnnotationToolBar` (`src/widgets/pdfannotationtoolbar.{h,cpp}`) rather than
in `PdfViewWindow2`, so it is constructible in a test with a bare `QToolBar` and
no WebEngine profile.

`opacity` is stored **per anchor**, beside `width`, so two strokes drawn at
different settings each keep their own; an anchor with no `opacity` key (written
by an older build) renders solid. The slider ranges are deliberately NARROWER
than the schema ranges (thickness tops out at 24 vs `PdfInkAnchor::maxWidth()`
64; font size 6-72 vs 4-144), so a hand-edited config value is clamped for
DISPLAY only and never written back.

The **normalize → persist → push-to-adapter** routing likewise lives outside the
window, in `PdfToolOptionsRouter` (`src/widgets/pdftooloptionsrouter.{h,cpp}`),
which takes the config and the adapter explicitly. Two things fail *silently*
without it and are gated there:

- **Startup hydration.** `PdfToolOptionsRouter::hydrate()` must run **before**
  the first `false → true` readiness transition. The reload latch republishes
  only what the adapter already holds, so skipping it means picks persist to
  JSON and then a newly opened window comes up on the defaults.
- **The context-menu route.** `captureHighlight()` persists the pick as the
  highlight tool's colour and captures with the **same normalized token**, so
  the page context menu and the toolbar menu cannot disagree.

> **The `NoMenuIndicator` trap.** Do NOT set that dynamic property on these
> buttons. Every theme's `interface.qss` hides the dropdown indicator for it
> (e.g. `themes/pure/interface.qss:361`), and the indicator is the entire
> affordance. `test_pdfannotationtoolbar` asserts the property is unset.

> **Do not re-add a `QSignalBlocker` around the tick repaint in
> `syncState()`.** `setChecked()` never emits `triggered`, so there is nothing
> to echo back — but it does emit `changed`, which is how `QActionGroup` tracks
> its current member. Blocking it leaves the group's bookkeeping stale and a
> later user pick fails to clear the previous tick. The **sliders** in the same
> function follow the OPPOSITE rule and MUST be blocked: `QSlider::setValue`
> does emit `valueChanged`, which is wired up as a user pick and would echo
> straight back out and persist. Do not "harmonise" the two.

The toolbar toggles are **not** authoritative: the web side can leave a tool by
itself (Esc, or the one-shot Text tool completing), which reaches C++ as
`notifyToolFinished()` and repaints the toggles from the adapter. The
`QActionGroup` is deliberately **non-exclusive**, or clicking the armed tool
again could not disarm it.

**Ink gestures are pointer-scoped.** `inkDraft` records the owning `pointerId`; a second
pointer (a palm alongside a pen, a second finger) is refused outright, and only a matching
`pointerup` COMMITS. `pointercancel` / `lostpointercapture` / a tool switch **discard** —
a cancelled gesture did not complete, and saving it would persist a stroke the user aborted.

**The draft owns its own DOM node** (`.vx-comment-ink-draft`), updated in place per pointer
sample. Calling `renderAllComments()` per sample instead is quadratic in the comment set —
a pen emits 60-240 samples a second — and visibly freezes the page. `endInk()` removes the
provisional node **before** dispatching, because the request can still be refused (read-only
file, comment cap, adapter validation) and nothing would ever repaint; the user would be left
looking at a stroke that was never saved. The authoring tools are also disabled outright when
the file is not editable.

While Ink or Text is armed the comment layer takes `pointer-events: auto`
(`.vx-comment-authoring`), otherwise pdf.js's text layer wins the drag and the
user gets a selection instead of a stroke.

> This is NOT pdf.js's editor subsystem — that stays disabled (see above).
> Reusing it was evaluated and rejected: capture would need monkey-patching
> `annotationStorage.onSetModified` (which viewer.mjs already owns), there is no
> public API to restore serialized editors (`layer.deserialize` is reached only
> from a private paste path), and the serialized form is a PDF annotation dict —
> storing it would couple `comments.json` to a pdf.js version and orphan the data
> on a bump.

### The Text tool types ON THE PAGE (`beginFreeTextEdit`)

**Placing a box is only half the gesture.** For one release the Text tool minted a
`pdf-freetext` comment with an empty body and stopped there: the box rendered as the
`.vx-comment-freetext-empty` ellipsis placeholder, `CommentController::commentAdded` had
**zero receivers**, and the only editor was the comment dock — which is closed by default.
The tool was indistinguishable from broken. Do not remove the inline editor without
providing another affordance on the page.

The box **is** the editor: while `editingCommentId` names it, `renderFreeText` marks the same
element `contenteditable="plaintext-only"`. Routing:

| Step | Where |
|---|---|
| place → mint comment | `placeFreeText` → `requestAddComment` → `CommentController::addComment` |
| `commentAdded(id)` → is it `pdf-freetext`? | `PdfViewWindow2::beginInlineTextEdit` (the ONLY receiver of that signal) |
| open the editor | `PdfViewerAdapter::beginCommentTextEdit` → `commentTextEditRequested` → `beginFreeTextEdit(id, /*isNew=*/true)` |
| type | `input` → `scheduleFreeTextFlush` → (400 ms trailing throttle) → `requestSetCommentText` |
| commit | blur / Ctrl+Enter → `applyFreeTextEdit` → `requestSetCommentText` |
| re-open an existing box | **double-click** → `beginFreeTextEdit(id, false)` |

Six rules that are load-bearing:

- **Typing is STREAMED, not held until blur.** A page teardown (tab close, reload, window
  close) does not reliably deliver a blur, and a body that only ever existed inside the
  contenteditable would be lost with it — `PdfViewWindow2`'s destructor can only flush what
  `CommentController` already received. `flushFreeTextDraft()` therefore pushes the current
  draft on a `VX_FREETEXT_FLUSH_MS` (400 ms) trailing throttle (the same shape the comment
  dock uses for its own keystrokes). It deliberately **never deletes and never writes a blank
  body**: those are commit decisions, and a user who has just selected-all before retyping
  must not have the box removed from under them.

  The debounce leaves a window in which the newest characters have not been streamed, so
  every boundary that can still reach the bridge flushes **first**: `resetComments()` (i.e.
  `documenterror` / `pagesdestroy`) calls `flushFreeTextDraft()` before it discards anything,
  `setCommentsEditable(false)` makes one best-effort flush before closing, `pdfviewer.mjs`
  flushes from `pagehide` and from a hidden `visibilitychange`, and the ordinary
  click-elsewhere / close-the-tab gesture blurs the box, which commits synchronously in the
  render process before the click is acted on.

  > **This is a NARROWING, not a guarantee, and the two residuals are known.** (1) A close
  > that never blurs the box and never delivers a page-lifecycle event — a killed render
  > process — loses up to one debounce window; nothing running in the page can cover that, and
  > `PdfViewWindow2`'s destructor can only flush what `CommentController` already received.
  > (2) When editability is lost *because a write was refused* (`saveRejectedReadOnly`),
  > `CommentController` has already cleared `m_editable`, so the best-effort flush is refused
  > too and the unstreamed tail is unrecoverable — but the file is read-only, so that text had
  > nowhere to go in the first place, and the window shows the read-only `InlineBanner`.
  > Do not delete these flush points, and do not "fix" the residuals by streaming on every
  > keystroke: each streamed write echoes back as a full `setComments` publish, which rebuilds
  > the comment dock's list and the page's comment layers.
- **`beginCommentTextEdit` is NOT queued across a reload** (same rule as `captureSelection`):
  an edit *session* only exists in a live document, and replaying it would open an editor on
  whatever comment inherited that id in the replacement document.
- **The blank/new × blank/existing table.** Abandoning a **new** box blank DELETES it — "I
  clicked by mistake" has to look like nothing happened, and the leftover `…` placeholder is
  exactly the bug this feature fixes. Clearing an **existing** box writes an empty body
  (`requestSetCommentText(id, "")`) rather than deleting it: the dock's editor and the box's
  editor edit the *same* field, so they must not disagree, and deleting a comment nobody
  asked to delete is worse than leaving a visible, clickable placeholder. Blank means
  whitespace-only too (`isBlankFreeTextBody`, NBSP included — that is what contenteditable
  stores for a run of spaces). `normalizeFreeTextBody` does **transport** fixes only (CRLF,
  NUL, the single trailing newline Chromium leaves for its closing `<br>`, the cap); it does
  NOT trim, because an indented body must round-trip.
- **Esc REVERTS, it does not merely stop.** Because the flush above may already have written
  part of the draft, `cancelFreeTextEdit` puts `editingOriginalText` back on an existing box
  (only when something was actually streamed), and deletes a new one whatever was typed. This
  is the USER-driven route only; see the editability rule below for the involuntary one.
- **A repaint must not end the session.** `renderAllComments` rebuilds every layer (scroll,
  zoom, rotate all trigger it), which destroys the editor node and fires a blur. The blur
  handler ignores it while `editingRerender` is set, `editingDraftText` re-seeds the
  re-created node, and `focusFreeTextEditor()` restores the caret to the offset
  `captureCaretOffset()` recorded *before* the rebuild — a caret silently reset to 0 would
  interleave the user's typing with what is already there. The caret is measured with
  `walkEditorText()`, the **same** walk that produces the body — measuring with
  `Range.toString()` while reading with `innerText` is a trap, because a Range contributes no
  character for a `<br>` or a block boundary while `innerText` contributes `\n`, so the offset
  would be short by one per line. Every editor listener is additionally **scoped to its own
  node** (`self.editingEl !== p_el` → return): the detached predecessors stay alive and wired,
  and an event from one of them must never act on the current session.
- **Editability is pushed, not assumed.** `PdfViewerAdapter::setCommentsEditable` (latched
  alongside the tool) gates `beginFreeTextEdit`, and `commentsEditable` defaults to `false`,
  so a page that comes up before C++ has spoken cannot swallow keystrokes the store would
  refuse. This is the same silent-discard failure pdf.js's own editors are disabled for.

  Losing editability mid-edit makes one **best-effort flush** and then closes the box with
  `closeFreeTextEdit()` — *not* `cancelFreeTextEdit()`. `CommentController` gates
  `setCommentText` and `deleteComment` on the same `m_editable` flag it has just cleared (a
  late `saveRejectedReadOnly` is exactly how this happens), so a revert or a delete emitted
  here would be refused too and would only fake a restore that never occurred. The flush is
  worth attempting anyway, because the flag also changes for reasons that leave the write gate
  open. See the residual noted under the streaming rule above.

Coverage: `freeTextEditIsRefusedWhenTheStoreIsReadOnly`,
`anAbandonedNewBoxIsRemovedNotLeftEmpty`, `anExistingBoxClearedOnPurposeIsEmptiedNotDeleted`,
`typingIsStreamedSoATeardownCannotLoseIt`, `escapeRevertsAnExistingBoxAndDropsANewOne`,
`aRepaintKeepsTheEditorOpenAndItsDraftIntact`, `aGenuineBlurCommitsExactlyOnce`,
`caretOffsetResolvesToATextNodePosition`, `caretAndBodyAreMeasuredInTheSameCoordinates`,
`glueWiresTheInlineEditorSignals`, `glueFlushesTheDraftOnPageLifecycleEvents` in
`tests/widgets/test_pdfviewercore_js.cpp`;
`setCommentTextIsBoundedAndTruncatedNotRejected`,
`beginCommentTextEditIsDroppedWhenThePageIsNotReady`, `editabilityIsLatchedAcrossAReload` in
`tests/widgets/test_pdfvieweradapter_comments.cpp`.

> The JS test harness models the DOM closely enough that these are not vacuous: nodes carry
> `nodeType` / `childNodes` and `textContent` installs a real text node (so the cases execute
> the production `flattenEditorText()` reader, not a stub-only fallback), stub elements
> **retain** their listeners, `window.__fire()` dispatches them, and the fake page div
> **retains** its comment layer so clearing it dispatches `blur` on the children it removes.
> That last part is a deliberately CONSERVATIVE worst case rather than a browser guarantee —
> it makes the blur-vs-DOM-churn guard run on every rebuild instead of only in timing nobody
> can reproduce. Do not "simplify" the harness back into a no-op `addEventListener`; that is
> what made the first version of these cases pass without executing anything.

### Capture gestures (there must always be a discoverable one)

Two routes, and the **context menu is the primary one**:

| Route | Path |
|---|---|
| Select text → right-click → **Highlight ▸ <color>** | `PdfViewer::contextMenuEvent` → `highlightSelectionRequested` → `PdfViewWindow2` (which ALSO persists it as the highlight tool's colour, so the two pickers cannot disagree) → `PdfViewerAdapter::captureSelection` → `captureSelectionRequested` → `vxcore.setCommentColor` + `captureSelection()` |
| **Alt + drag** (shortcut) | `mouseup` on `#viewerContainer` with `altKey` → `vxcore.captureSelection()` |

**Do not delete the context menu and leave only Alt+drag.** That was the first
implementation and it shipped a feature nobody could find: there was no button, no menu and no
hint, so the comment dock simply stayed empty forever. A modifier-key gesture is a shortcut for
people who already know the feature exists; it is never the way they find out.

Alt is used because Ctrl/Shift are already selection modifiers and Alt+drag is unbound in pdf.js.
`selectionchange` is deliberately not used for either route — it fires continuously during a
drag and would mint a comment per intermediate selection.

`captureSelection` is deliberately **not latched** across a reload (unlike the comment set): a
selection only exists in a live document, so replaying it would act on whatever happened to be
selected in the replacement document.

### Colors come only from CSS custom properties

`pdfviewer.css` is linked **verbatim** into the template and is **not** processed by the theme
token resolver, so it cannot contain a `@palette#` token — and it must not contain a literal color
either, since VNote ships 12 themes.

`ThemeService::commentHighlightCssVariables()` resolves every `CommentColor` token to a real color
and `HtmlTemplateService` injects the block at `VX_PDF_VARS_PLACEHOLDER`; the CSS references only
`var(--vx-comment-*)`. `PdfViewWindow2::handleThemeChanged()` force-regenerates the template and
reloads, so a theme switch picks up new values. The resolved colors are part of the template's
cache key, because a theme switch changes them without changing the config revision.

The **defaults are anchored to the page, not to the palette**: pdf.js renders the page from the
PDF itself and does not tint it with the app theme, so a "yellow" highlight has to stay readable on
paper in every theme. A theme MAY override any token with `widgets.pdfcomment.<token>`.

The built-in token→colour table itself lives in
`CommentColorSwatch::builtInColor()` (`src/gui/utils/commentcolorswatch.cpp`),
which `ThemeService::commentHighlightColor()` calls. There must be exactly one
table, so the chip drawn in Qt chrome cannot disagree with what is painted on
the page — see [`src/gui/AGENTS.md`](../../../../gui/AGENTS.md).

### Bounds are enforced on BOTH sides

The JS caps (`VX_MAX_COMMENT_QUADS`, `VX_MAX_ANCHOR_TEXT`) and the C++ caps
(`PdfQuadsAnchor::maxQuadsPerComment()`, `maxAnchorTextLength()`) are **independent by design**.
Everything crossing the QWebChannel is attacker-controlled — any script in the page can call
`requestAddComment` — so `PdfViewerAdapter` re-validates anchor type, page range (against the
document's reported page count), quad count, quad shape, coordinate finiteness, text length, color
token and id length. Coverage: `tests/widgets/test_pdfvieweradapter_comments.cpp`.

### Reload replay

The adapter outlives the page and `WebViewAdapter::setReady()` early-returns when unchanged, so a
comment set published while a reload is in flight would target a destroyed page and the replacement
page's `setReady(true)` would be a no-op. `setComments()` therefore **latches** (it does not queue
via `pendAction`): only the newest set is kept, and it is published exactly once on the false→true
transition. `clearComments()` runs during teardown and deliberately drops the latch, so its empty
set can never blank the replacement document's real set.