# Utils Module

`src/utils/` contains general-purpose utilities used across the VNote codebase. These are non-GUI, non-service helpers for path manipulation, string processing, HTML handling, and more.

## Utility Classes

| Class | Purpose |
|-------|---------|
| `PathUtils` | Path manipulation, normalization, and resolution |
| `HtmlUtils` | HTML processing and sanitization |
| `FileUtils2` | File operations (new architecture) |
| `FileUtils` | File operations (legacy) |
| `StringUtils` | String manipulation utilities |
| `ProcessUtils` | External process execution |
| `ClipboardUtils` | Clipboard read/write operations |
| `ContentMediaUtils` | Content and media processing |
| `DocsUtils` | Documentation utilities |
| `WebUtils` | Web-related utilities |
| `VxUrlUtils` | VNote URL scheme utilities |
| `UrlDragDropUtils` | URL drag-and-drop handling |
| `AsyncWorker` | Asynchronous task execution |
| `CallbackPool` | Callback management pool |

## Related Modules

- [Core Services](../core/AGENTS.md) — Core services that use utilities
- [Root](../../AGENTS.md) — Code style guidelines
