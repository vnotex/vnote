# Utils Module

`src/utils/` contains general-purpose utilities used across the VNote codebase. These are non-GUI, non-service helpers for path manipulation, string processing, HTML handling, and more.

## Utility Classes

| Class | Purpose |
|-------|---------|
| `PathUtils` | Path manipulation, normalization, and resolution |
| `HtmlUtils` | HTML processing and sanitization |
| `FileUtils2` | File operations |
| `stringutils.h` | Free string comparison and size-formatting helpers |
| `ProcessUtils` | External process execution |
| `ClipboardUtils` | Clipboard read/write operations |
| `ContentMediaUtils` | Content and media processing |
| `DocsUtils` | Documentation utilities |
| `WebUtils` | Web-related utilities |
| `UrlDragDropUtils` | URL drag-and-drop handling |
| `AsyncWorker` | Asynchronous task execution |
| `CallbackPool` | Callback management pool |

## String helpers

`stringutils.h` exposes free functions in `vnotex`, including `naturalCompare()` and
`formatFileSize(quint64, const QLocale &)`. The formatter defaults to the current locale
and performs no filesystem access. It returns `"<number> <unit>"` with invariant unit
symbols `B`, `KB`, `MB`, `GB`, `TB`, `PB`, `EB`, using traditional 1024-based steps.
Bytes are integral; larger units have two decimal places (1536 bytes becomes `1.50 KB`
in the C locale). Numbers follow the supplied locale, including decimal separators and
grouping. The full unsigned 64-bit range is supported without signed narrowing.

Units are selected from the original byte count, before display rounding; a value just
below the next threshold can therefore display `1024.00` in the lower unit.
Coverage: `tests/utils/test_stringutils.cpp`.

## C++ to web translations

`WebUtils::translationScript()` snapshots the current Qt translations into a standalone
`window.vxI18n.tr(id)` lookup. Inject the returned JavaScript before scripts that consume
it; it needs neither WebChannel nor network access. `MarkdownWebGlobalOptions` includes it
for live Markdown viewers (including protected pages), and `WebViewExporter` includes it
for standalone HTML. Existing exports keep their export-time language; generate a new snapshot for a
new page rather than caching translated strings across language changes.

Maintain stable IDs and literal `QCoreApplication::translate("WebUtils", ...)` calls in
`webutils.cpp`, and update both Qt catalogs when adding an entry. Missing Qt translations
fall back to the English source text; unknown web IDs return the ID itself. Assign results
with `textContent`, `title`, or `setAttribute`, not `innerHTML`:

```javascript
label.textContent = window.vxI18n.tr('outline.title');
```

The serializer escapes HTML script delimiters, template comment markers, and Unicode line
separators. `test_htmltemplateservice` executes the emitted JavaScript with the compiled
Chinese/Japanese catalogs and checks snapshot lifetime and hostile-text round trips when
Qt Qml is available.

## Related Modules

- [Core Services](../core/AGENTS.md) — Core services that use utilities
- [Root](../../AGENTS.md) — Code style guidelines
