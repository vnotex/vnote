# Utils Module

`src/utils/` contains general-purpose utilities used across the VNote codebase. These are non-GUI, non-service helpers for path manipulation, string processing, HTML handling, and more.

## Utility Classes

| Class | Purpose |
|-------|---------|
| `PathUtils` | Path manipulation, normalization, and resolution |
| `HtmlUtils` | HTML processing and sanitization |
| `FileUtils2` | File operations |
| `StringUtils` | String manipulation utilities |
| `ProcessUtils` | External process execution |
| `ClipboardUtils` | Clipboard read/write operations |
| `ContentMediaUtils` | Content and media processing |
| `DocsUtils` | Documentation utilities |
| `WebUtils` | Web-related utilities |
| `UrlDragDropUtils` | URL drag-and-drop handling |
| `AsyncWorker` | Asynchronous task execution |
| `CallbackPool` | Callback management pool |

## C++ to web translations

`WebUtils::translationScript()` snapshots the current Qt translations into a standalone
`window.vxI18n.tr(id)` lookup. Inject the returned JavaScript before scripts that consume
it; it needs neither WebChannel nor network access. `WebViewExporter` does this for HTML
exports. Existing pages keep their export-time language; generate a new snapshot for a
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
