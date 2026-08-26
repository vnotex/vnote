# src/gui/ — GUI-Aware Services and Utilities

`src/gui/` contains GUI-aware services and utilities that depend on Qt Widgets/Gui modules. These are separated from `src/core/services/` which are Qt-minimal.

## services/

| Class | Purpose |
|-------|---------|
| `ThemeService` | GUI-aware theme management — loading themes, applying stylesheets |
| `ViewWindowFactory` | Registry pattern mapping file types to `ViewWindow2` creators; plugins register new viewers here. The built-in `"Pdf"` creator is **build-conditional**: it is registered only on **Qt 6.9+**, because the vendored pdf.js v6 bundle is ESM-only, needs Chromium 125+, and is served over the `vxpdf://` `QWebEngineUrlScheme` — see [`../data/extra/web/pdf.js/AGENTS.md`](../data/extra/web/pdf.js/AGENTS.md) |
| `WebEngineProfileService` | Owns the shared named `QWebEngineProfile` **and** the `vxpdf://` scheme handler (`VxPdfSchemeHandler`), plus the PDF document token registry (`registerPdfDocument` / `unregisterPdfDocument`) |
| `NavigationModeService` | Keyboard navigation mode service |

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

## Core vs GUI Distinction

Core services (`src/core/services/`) wrap the vxcore C API and have minimal Qt dependencies. GUI services (`src/gui/services/`) require Qt Widgets and handle presentation concerns.

## Related Modules

- [`../core/AGENTS.md`](../core/AGENTS.md) — Core services that GUI services extend/wrap
- [`../widgets/AGENTS.md`](../widgets/AGENTS.md) — Widgets that consume GUI services
- [`../../AGENTS.md`](../../AGENTS.md) — Architecture overview, code style
