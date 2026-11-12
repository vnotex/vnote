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

## File viewer routing

`Text` owns the built-in plaintext editor. `Others` has no built-in creator: explicit
file opens use the controller's system-application fallback, after any configured
per-suffix external program. Dispatch happens before tab replacement or detached-workspace
creation. Registered suffixes win; otherwise MIME inference uses the resolved file path
and may select `Text` or `Image`. Encrypted notes use their authenticated editor type.

Session and fresh-workspace restoration skip types without a registered creator, without
launching external applications or removing core buffers/workspace membership. Locked
encrypted notes retain their unlock placeholder. Coverage: `test_viewarea_tab_reuse` tests
Others dispatch, inferred viewers, legacy sessions and already-unlocked protected editors.

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

### Tree spacing

The optional `palette.json` metadata flag `compact-tree-spacing` enables 12px
indentation and 4px item padding/internal spacing for every `QTreeView`,
including `QTreeWidget` subclasses. All bundled themes except `native` opt in;
their generic QSS item padding is `4px 4px`, preserving row heights, fonts,
icons and theme-specific borders. Their tree inline editors drop the form-field
minimum height/padding to fit their rows.

Custom tree delegates use `TreeViewUtils::horizontalSpacing()` to distinguish
outer content insets from icon/count gaps. For opted-in trees, an undecorated
style probe measures the left and right insets independently, including theme
borders and Qt's native focus-frame text margin. `ItemViewUtils::textRect()`
shares that glyph-origin calculation with highlighted standard items. Initialize
the view option first so model fonts participate; do not treat the QSS padding
number alone as the text origin or count custom icons twice. Row heights are
unchanged, and non-opted-in views keep the caller's legacy spacing.

`Application::notify()` applies the active `ThemeService` policy through
`TreeViewUtils` before initial `Polish` and before Qt handles `StyleChange` for
an already-polished tree. Ignore constructor-time `StyleChange`: sidebar setup
may not have applied its default indentation yet. On entry, record that tree's
current indentation; repeated refreshes must not overwrite it. On exit, restore
it and clear the transient widget properties. This preserves platform defaults
and explicit per-view values rather than forcing every tree back to 16px.

The application's service reference is guarded; an absent/destroyed service is
a no-op. Preview theme services/local QSS do not drive this policy. Missing
metadata keeps the legacy spacing, including user-defined themes; this changes
bundled theme styling, not app/session configuration, so no migration is needed.
Existing installed theme copies still follow the normal versioned resource refresh;
this rollout does not overwrite user theme folders.

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

Daily and random dashboard tips use `DocsUtils::getUiLanguages()`, the expanded language
tags of the VNote translation catalog loaded at startup, just like bundled documents.
Do not use `QLocale().uiLanguages()` or `getLocaleToUse()`: Qt can skip an untranslated
system preference (including English) and load a later catalog, independently of the
regional-format locale. Both tip paths pass the resolved tags to `findLocalizedValue()`
in `utils/localeutils.h`; trim each candidate and fall back to `en_US` on no usable match.
The helper's tag/canonical/language ordering is documented in `utils/AGENTS.md`.
Coverage: `test_configmgr2` and `test_greetingsticker` deliberately conflict the loaded
catalog language with the regional locale, including the default language setting.

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

## Tree horizontal scrolling

`TreeHorizontalScrollHelper` coalesces resize, show, expansion/collapse and model
changes through one 50 ms single-shot timer. Start it only when inactive: continuous
resizing still updates the column periodically instead of waiting for the drag to end.
Each callback measures current content and viewport geometry. Tests should wait for
column coverage or scrollbar behavior, not a fixed sleep or the timer interval.

## Tree child-count badges

`TreeChildCountDelegate` (`utils/treechildcountdelegate.h`) is the shared opt-in
renderer for single-column trees. Parent the delegate to the `QTreeView` when
installing it: the horizontal scrollbar connection repaints viewport-fixed badges
after Qt scroll-copies the row contents. No per-row widgets or model writes.

The default count is the displayed model's immediate `rowCount(index)`, including
proxy filtering, as used by Outline and Tasks. Tags supplies `TagModel::NoteCountRole`
through the delegate's optional count-role constructor argument: direct note counts
are cached and do not change with filtering or child-tag expansion.
`NotebookNodeDelegate` derives from it and passes cached folder totals to the protected
layout/paint/width helpers, so lazy loading and the folder-only proxy do not change
notebook count semantics. Never query services or fetch children from a paint or
size-hint path. Zero/unknown counts are hidden.

Badges use the initialized font, theme spacing and foreground; single digits are
circular and longer counts widen into pills without increasing row height. Anchor
X to the viewport, not the column or indented row. Preserve name clearance in both
painting and width hints, including fractional text advances at maximum scroll.
The standard delegate also reserves the difference between text chrome's right inset
and the badge's viewport inset. `TreeHorizontalScrollHelper` already coalesces
`dataChanged` into column remeasurement, including count digit-boundary changes.
Keep badge painting active when indentation moves the label outside the viewport.
Selection, input and inline editing remain owned by Qt. No settings/schema changed;
no configuration migration is needed.

Coverage: `test_treechildcountdelegate` exercises scrolling, resize, row selection,
immediate/filtered counts and deeply indented rows; `test_notebooknodedelegate_sizehint`
covers cached notebook totals and content widths. Native smoke must also inspect
screen captures after scrolling, rather than relying only on forced widget renders.

## External-link confirmation

`ExternalLinkConfirmation::ask()` (`utils/externallinkconfirmation.{h,cpp}`) owns
the shared warning preference for every URL scheme, including local files and custom
protocols. Markdown, Typst, mind-map and desktop-opening warnings share this preference.
It may show a modal warning but never changes configuration or opens the URL.
Callers retain URL authorization and must recheck their lifetime, generation and
availability after the modal returns, before calling the result's `accept()`.
Only that call commits **Never Ask** through the guarded `ConfigMgr2`; discarding
a stale result has no preference side effects. Typst must also settle rejected
controller requests with an empty URL so pending one-shot consent is consumed.

`MessageBoxHelper::questionOpenLink()` owns the warning text and button choices,
using the existing `MainWindow` translations. Keep that generic helper config-free.
Every link warning offers **Yes / No / Never Ask**. Once accepted, **Never Ask**
suppresses all link warnings, regardless of scheme or which caller opens the next link.
`WidgetUtils::openUrlByDesktop()` receives configuration explicitly and uses the same
confirmation helper for local-file/program and other non-direct URLs; HTTP/HTTPS and
local directories continue to open directly. A destroyed originating parent discards
the desktop-opening result. `WebViewer` receives guarded configuration for its fallback.
URL authorization, including protected-note restrictions, remains independent.
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
