# src/gui/ — GUI-Aware Services and Utilities

`src/gui/` contains GUI-aware services and utilities that depend on Qt Widgets/Gui modules. These are separated from `src/core/services/` which are Qt-minimal.

## services/

| Class | Purpose |
|-------|---------|
| `ThemeService` | GUI-aware theme management — loading themes, applying stylesheets |
| `ViewWindowFactory` | Registry pattern mapping file types to `ViewWindow2` creators; plugins register new viewers here. The built-in `"Pdf"` creator is **build-conditional**: it is registered only on **Qt 6.9+**, because the vendored pdf.js v6 bundle is ESM-only, needs Chromium 125+, and is served over the `vxpdf://` `QWebEngineUrlScheme` — see [`../data/extra/web/pdf.js/AGENTS.md`](../data/extra/web/pdf.js/AGENTS.md) |
| `WebEngineProfileService` | Owns the shared named `QWebEngineProfile` **and** the `vxpdf://` scheme handler (`VxPdfSchemeHandler`), plus the PDF document token registry (`registerPdfDocument` / `unregisterPdfDocument`) |
| `WebViewService` / `WebViewJobSession` | Generic asynchronous JS jobs on a lazy shared ordinary page or a caller-owned page; no Markdown dependency |
| `MindMapImageLoader` | Scoped passive image transport shared by native editing and export; authorization stays with the controller |
| `NavigationModeService` | Keyboard navigation mode service |
| `ToolTipService` | One scoped startup attempt to post a localized daily usage tip |

## Qt application appearance

On Qt 6.8+, `ThemeService` synchronizes `QStyleHints::colorScheme` on startup,
switch and refresh, before `themeChanged`. Custom themes use the relative lightness
of resolved `base#normal#bg` and `base#normal#fg`, not their names. QSS alone does not
select the light/dark appearance of Qt's native standard context-menu icons.
Custom themes also set the default palette's `Text` roles from `base#normal#fg`
and `base#disabled#fg`: Qt's native font icons read that palette, and some Windows
styles retain light text roles even after a dark scheme request.
`Mid` uses `base#normal#border` and `Midlight` uses `base#hover#bg`: QSS
`palette(...)` colors resolve from the application palette, not a widget's palette.
These roles cover native mind-map properties borders, swatches and hover fills.

Themes with `backfill-system-palette` (including Native and renamed copies) clear
the palette overrides (zero resolve mask), then the scheme override **before**
`ThemeUtils::backfillSystemPalette` samples Qt's palette. Unrelated palette roles
remain platform-owned; do not replace the native style or pin a system snapshot.
Keep the current theme alive during loading: resetting the Qt scheme can notify
widgets synchronously. Only replace it after a successful load or fallback.
Settings previews remain local QSS; they must not change application appearance.
Older Qt versions retain their platform-selected scheme.

`tests/gui/test_themeservice.cpp` covers appearance transitions, Native backfill,
metadata-based system themes, and Windows standard-menu icon contrast in both
Undo/Redo states. Run on the native Windows platform to exercise the icon provider;
offscreen runs cannot verify it. Restore the system scheme and palette between test slots.

## Native MindMap image transport

`MindMapImageLoader` handles unprotected data/file/relative/HTTP(S) resources only. Preserve the
32 MiB limit, bounded reply buffers, 15-second inactivity and total deadlines, five redirects,
`NoLessSafeRedirectPolicy`, shared network request policy and sanitized image decoding. Every
unsupported/missing/invalid resource settles once with a null image. Cancellation advances the
request generation, aborts/disconnects replies, wipes temporary bytes and suppresses late results.

`MindMapViewWindowController` remains the authorization boundary. Protected buffers never enter
the unprotected loader: data URLs use the static decoder, relative resources use the authenticated
`Buffer2::readResource` path and file/network URLs remain forbidden. Keep lease/generation checks,
protected-lock invalidation and completion-time protection checks in the controller. Native export
uses a separate scoped loader; it must not borrow a live editor's resources or protected access.

## Daily usage tip state

`ToolTipService` uses `ConfigMgr2` for both stores: the permanent `toolTipsEnabled`
preference stays in `CoreConfig` (`vnotex.json` → `core`), while `lastToolTipDate`
and `nextToolTipIndex` live in `SessionConfig` (`session.json` → `core`). Session
reset therefore restarts daily eligibility and rotation without undoing opt-out.
`ConfigMgr2::init()` imports legacy main-config progress only for absent session
keys, then retires the old main-config keys through normal persistence. Explicit
session values, including an empty date and index zero, take precedence.

Select catalog translations using `QLocale().uiLanguages()` in preference order,
trying each tag with underscores, its canonical `QLocale(tag).name()` key (so
`zh-Hans-CN` can match `zh_CN` even without Qt-expanded aliases), and its language
prefix before falling back to `en_US`.
Do not use `getLocaleToUse()` here: it returns the regional-format locale, which
can differ from the system UI language. Startup already applies explicit language
settings through `QLocale::setDefault()` before tips are produced.

Keep the producer stack-scoped after the main-window startup hook. Its retained
opt-out action captures only a `QPointer<ConfigMgr2>`; no producer or config-field
reference may outlive that scope. `OK` dismisses only the current message;
acknowledgement never disables future tips or resets the consumed day/index.
The tip uses `Duration::Medium`: its toast auto-hides after 10 seconds. Hovering
pauses auto-hide; leaving restarts the full 10-second budget.

## WebEngine profile storage

`WebEngineProfileService` uses VNote's injected Local data root for `webcache` and
`webstorage`. On Qt 6.9+, supply both paths through `QWebEngineProfileBuilder` before
creating the named profile: assigning them afterward still lets Qt create its default
Roaming AppData directory on Windows. Keep the older-Qt constructor/setter branch
for compatibility; do not change application identity or move caches to the App root.

`tests/gui/test_webengineprofileservice.cpp` guards construction-time directory isolation
and persistent browser storage in its own Qt 6.9+ target with a CTest-owned runtime
environment. Keep `test_vxpdfschemehandler` GUILESS; it must not initialize Chromium.

## Shared JS execution

`WebViewService` is registered once through `ServiceLocator`, after its borrowed profile and
before the main window. It creates an ordinary hidden Qt WebEngine host only on the first job.
The application supplies bootstrap code; the service has no Markdown, buffer or widget-module
policy. Protected pages are consumer-owned and attach `WebViewJobSession` directly.

A page has one QWebChannel and one JavaScript handshake. Register `vxJobs` on the reader's
existing channel before loading; never replace its document bridge. Providers register named
Promise-capable handlers through `webviewjobs.js`, then announce readiness. Loading HTML alone
is not readiness. Transport IDs are strings and independent of editor-local block IDs.

`cancel(id)` and `cancelOwner()` settle live callbacks once; destroyed owners receive none.
Cancellation drops ownership but an active job retains its serial slot until JS cleanup finishes
or the page is torn down. Reloads retire generations; `fail()` reports provider/bootstrap failure.
Borrowed sessions never destroy the page/channel. Destroy owned pages before their profiles.

Coverage: `tests/gui/test_webviewservice.cpp` runs real non-Markdown jobs without linking the
Markdown implementation. Consumer policy: [controllers](../controllers/AGENTS.md#markdown-js-jobs).

## utils/

| Class | Purpose |
|-------|---------|
| `WidgetUtils` | Widget utility helpers (focus, geometry, etc.) |
| `ExternalLinkConfirmation` | Shared external-link warning preference and deferred opt-out |
| `ThemeUtils` | Theme file loading and parsing utilities |
| `ImageUtils` | Image processing utilities |
| `GuiUtils` | General GUI utilities |
| `IconUtils` | Icon loading and management |
| `PrintUtils` | Print/export utilities |
| `CommentColorSwatch` | The **single** way to render a `CommentColor` token as a chip in Qt chrome — see below |

## External-link confirmation

`ExternalLinkConfirmation::ask()` (`utils/externallinkconfirmation.{h,cpp}`) owns
the HTTP/HTTPS/FTP/mailto preference policy for Markdown, Typst and mind-map links.
It may show a modal warning but never changes configuration or opens the URL.
Callers retain URL authorization and must recheck their lifetime, generation and
availability after the modal returns, before calling the result's `accept()`.
Only that call commits **Never Ask** through the guarded `ConfigMgr2`; discarding
a stale result has no preference side effects. Typst must also settle rejected
controller requests with an empty URL so pending one-shot consent is consumed.

`MessageBoxHelper::questionOpenLink()` owns the warning text and button choices,
using the existing `MainWindow` translations. Keep that generic helper config-free.
Local-file/program warnings never honor or modify the external-link opt-out.
`WidgetUtils::openUrlByDesktop()` shares this warning UI but keeps its independent
policy: HTTP/HTTPS and local directories open directly; other URLs require Yes/No.
Coverage: `tests/gui/test_externallinkconfirmation.cpp` and Typst preview consent cases.

## CommentColorSwatch

`CommentColorSwatch` (`utils/commentcolorswatch.{h,cpp}`) is the one way a
comment colour token becomes a `QIcon`. All three pickers use it: the PDF
annotation tool menus (`PdfAnnotationToolBar`), the Comment dock combo
(`CommentPanel`) and the page context menu (`PdfViewer`). **Do not hand-roll a
fourth mapping.**

It also **owns the built-in token→colour table** (`builtInColor()`).
`ThemeService::commentHighlightColor()` calls into it rather than holding its
own copy, so the chip and the colour painted on the PDF page cannot disagree.
Consequence for CMake: every test target that compiles `themeservice.cpp` must
also compile `commentcolorswatch.cpp`.

**It is a LEAF: it references no `ThemeService` and no widget.** The themed
colour arrives as an injected `ColorResolver` callback, and the themed border as
a plain string. A nullable `ThemeService *` would not do — `theme ? theme->x() :
y` still emits a link-time reference from the *caller's* translation unit, and
`test_commentpanel` / `test_pdfannotationtoolbar` deliberately compile the
widgets that draw swatches **without** `themeservice.cpp`. A default-constructed
resolver means the built-in colours, which is what those tests exercise.

Every widget that draws a swatch exposes the same
`setSwatchResolver(ColorResolver, QString borderCss)`. **Both** arguments are
re-supplied on a theme switch, not merely re-rendered: the border travels as a
value and would otherwise go stale. The owners do the wiring —
`MainWindow2::setupCommentPanel()` for the dock, `PdfViewWindow2::
handleThemeChanged()` for the toolbar and the page viewer.

The chip is composited over **white** (the tokens are translucent and are
anchored to the PDF page, which pdf.js renders from the document and never tints
with the theme), then given a 1px themed border. It is painted with `QPainter`,
never a stylesheet — colour as **data**, the sanctioned exception to
[`../widgets/AGENTS.md` § No Hardcoded Colors in C++](../widgets/AGENTS.md#no-hardcoded-colors-in-c).

Coverage: `tests/gui/test_commentcolorswatch.cpp`.

## Alternating item-view rows

Every non-native theme sets `alternate-background-color: @base#normal#bg` on both
`QTreeView` and `QListView`. Setting only `background-color` leaves Qt's
`QPalette::AlternateBase` inherited from the desktop, causing light stripes in dark
views that opt into alternating rows. Native stays on the system palette.
Location List and Comments explicitly disable alternating rows; no application view
currently enables them. `tests/gui/test_themeservice.cpp` deliberately opts in to
verify theme colors and Native restoration independently of application row policy.
Its background-only fixture parks the physical cursor outside the centered window
before exposure and restores it after destruction; the views are unfocusable.
Mouse transparency alone is insufficient: layout/scrollbar changes can derive
hover directly from the global cursor position.

## Light-theme graph canvases

Light reader themes leave graph containers and SVG canvases transparent so the page
background shows through. Override PlantUML's inline SVG background, and clear only
Graphviz's outer canvas polygon; never clear node or cluster fills. Keep dark-theme
backdrops unchanged. Backgrounds baked into raster images are image data, not CSS
surfaces, and are not recolored by the theme.

Light themes opt PlantUML in-place previews into renderer-generated transparency with
`body { --vx-plantuml-preview-background: transparent; }`. The PNG request inserts
`skinparam backgroundColor transparent` after `@startuml` (or before an unwrapped UML
source), ahead of user commands so explicit backgrounds retain precedence. It does
not recolor pixels, rewrite stored notes, or inject UML syntax into other PlantUML
languages such as JSON or Ditaa. Dark themes omit the property.

## Core vs GUI Distinction

Core services (`src/core/services/`) wrap the vxcore C API and have minimal Qt dependencies. GUI services (`src/gui/services/`) require Qt Widgets and handle presentation concerns.

## Related Modules

- [`../core/AGENTS.md`](../core/AGENTS.md) — Core services that GUI services extend/wrap
- [`../widgets/AGENTS.md`](../widgets/AGENTS.md) — Widgets that consume GUI services
- [`../../AGENTS.md`](../../AGENTS.md) — Architecture overview, code style
