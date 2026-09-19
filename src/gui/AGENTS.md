# src/gui/ — GUI-Aware Services and Utilities

`src/gui/` contains GUI-aware services and utilities that depend on Qt Widgets/Gui modules. These are separated from `src/core/services/` which are Qt-minimal.

## services/

| Class | Purpose |
|-------|---------|
| `ThemeService` | GUI-aware theme management — loading themes, applying stylesheets |
| `ViewWindowFactory` | Registry pattern mapping file types to `ViewWindow2` creators; plugins register new viewers here. The built-in `"Pdf"` creator is **build-conditional**: it is registered only on **Qt 6.9+**, because the vendored pdf.js v6 bundle is ESM-only, needs Chromium 125+, and is served over the `vxpdf://` `QWebEngineUrlScheme` — see [`../data/extra/web/pdf.js/AGENTS.md`](../data/extra/web/pdf.js/AGENTS.md) |
| `WebEngineProfileService` | Owns the shared named `QWebEngineProfile` **and** the `vxpdf://` scheme handler (`VxPdfSchemeHandler`), plus the PDF document token registry (`registerPdfDocument` / `unregisterPdfDocument`) |
| `NavigationModeService` | Keyboard navigation mode service |
| `ToolTipService` | One scoped startup attempt to post a localized daily usage tip |

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

## WebEngine profile storage

`WebEngineProfileService` uses VNote's injected Local data root for `webcache` and
`webstorage`. On Qt 6.9+, supply both paths through `QWebEngineProfileBuilder` before
creating the named profile: assigning them afterward still lets Qt create its default
Roaming AppData directory on Windows. Keep the older-Qt constructor/setter branch
for compatibility; do not change application identity or move caches to the App root.

`tests/gui/test_webengineprofileservice.cpp` guards construction-time directory isolation
and persistent browser storage in its own Qt 6.9+ target with a CTest-owned runtime
environment. Keep `test_vxpdfschemehandler` GUILESS; it must not initialize Chromium.

## utils/

| Class | Purpose |
|-------|---------|
| `WidgetUtils` | Widget utility helpers (focus, geometry, etc.) |
| `ThemeUtils` | Theme file loading and parsing utilities |
| `ImageUtils` | Image processing utilities |
| `GuiUtils` | General GUI utilities |
| `IconUtils` | Icon loading and management |
| `PrintUtils` | Print/export utilities |
| `CommentColorSwatch` | The **single** way to render a `CommentColor` token as a chip in Qt chrome — see below |

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
