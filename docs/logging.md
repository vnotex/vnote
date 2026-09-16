# VNote Logging System

VNote provides a structured logging system using Qt's `QLoggingCategory` framework. This document explains how to use and configure logging.

## Logging Categories

VNote defines the following logging categories:

| Category | Name | Purpose |
|----------|------|---------|
| `lcSync` | `vnote.sync` | Sync backend and notebook synchronization operations |
| `lcBuffer` | `vnote.buffer` | Buffer management, file open/close/save operations |
| `lcWorkspace` | `vnote.workspace` | Workspace and view split operations |
| `lcWebJs` | `vnote.web.js` | JavaScript console messages from web viewers |
| `lcVim` | `vnote.vim` | Vim mode and input handling |
| `lcVxBridge` | `vnote.vxbridge` | VxCore library integration and communication |
| `lcUi` | `vnote.ui` | User interface events and state changes |
| `lcConfig` | `vnote.config` | Configuration loading and management |

## Rule Precedence (CANONICAL)

Qt logging rules apply in order; the LAST matching rule wins. `installDefaultLoggingRules()` sets:
```
*.debug=false
vnote.web.js.debug=false
vnote.vim.debug=false
qt.qpa.*.debug=false
```

Then APPENDS `VNOTE_LOG_RULES` env if set. Therefore:
- Setting `vnote.web.js.debug=true` via env → web.js Debug ON (later rule wins over the explicit `=false`)
- Setting `*.debug=true` via env → web.js Debug ON AND vim Debug ON (wildcard rule, set after the explicit `=false` lines, wins)
- To get "everything debug except the loud ones": `*.debug=true;vnote.web.js.debug=false;vnote.vim.debug=false`

## Configuration

Logging is automatically configured by `installDefaultLoggingRules()` which is called during application startup before QApplication is created.

### Default Rules

By default, VNote silences Debug messages from:
- All categories (`*.debug=false`)
- Web JavaScript console (`vnote.web.js.debug=false`) — can be re-enabled for debugging
- Vim input handling (`vnote.vim.debug=false`) — can be re-enabled for input debugging
- Qt platform abstraction (`qt.qpa.*.debug=false`)

Warning and higher severity messages always display regardless of the rules.

### Environment Variables

Override logging with the `VNOTE_LOG_RULES` environment variable. Rules are appended to the default set, so later rules override earlier ones.

## Recipes

### 1. Enable all sync debug messages

```bash
# Linux/macOS
export QT_LOGGING_RULES="vnote.sync.debug=true"
vnote

# Windows (PowerShell)
$env:QT_LOGGING_RULES = "vnote.sync.debug=true"
& "path/to/vnote.exe"

# Windows (Command Prompt)
set QT_LOGGING_RULES=vnote.sync.debug=true
vnote.exe
```

### 2. Enable buffer and workspace debug

```bash
export QT_LOGGING_RULES="vnote.buffer.debug=true;vnote.workspace.debug=true"
vnote
```

### 3. Enable JavaScript console (verbose)

```bash
export QT_LOGGING_RULES="vnote.web.js.debug=true"
vnote
```

Expect chatty output from Markdown viewer JavaScript.

### 4. Enable vxcore debug logging

```bash
export VXCORE_LOG_LEVEL=debug
vnote
```

This enables logging from the vxcore C library used for notebook backend operations.

### 5. Everything debug EXCEPT the loud ones

```bash
export VNOTE_LOG_RULES="*.debug=true;vnote.web.js.debug=false;vnote.vim.debug=false"
vnote
```

This enables debug output for all categories except web.js and vim, keeping the logs manageable.

### 6. Trace math preview startup

Launch with `--verbose --log-stderr` (in Qt Creator, put these in the run arguments).
This enables both `vnote.perf.preview` and `vnote.web.js`; output goes to stderr / Qt
Creator's Application Output, not `vnote.log`. If `VNOTE_LOG_RULES` is already set,
`--verbose` preserves it: enable both categories explicitly, separated by newlines.

Entries beginning with `[math-trace]` cover:
- Viewer construction, loading, readiness, hide/show, and ready-triggered rehighlight
- Math generations, debounce restarts, cache hits, pre-ready request drops, and replay
- Renderer script/CSS initialization, typesetting, and `document.fonts.ready` wait/settlement
- Bounds, font embedding, SVG conversion, image loading, PNG encoding, and raster concurrency
- Bridge delivery, native decoding, stale/empty results, and editor publication

C++ `adapter` matches JavaScript `page`; the helper-mapping entry joins C++ helper events.
Preview contexts are `math:<generation>:<id>` or `fenced:<generation>:<id>`. Read passes
use `read:<sequence>`; a new `created` event starts a new page lifetime after reload.
Compare C++ `atMs` with JavaScript `epochMs`; JavaScript `ms` is monotonic within its page.
A `fonts-wait` without its matching `fonts-ready`/`fonts-error` identifies an unfinished
font/layout wait. Tracing does not inspect font status or force layout to observe it.
New trace entries contain IDs, counts and sizes, not formulas or image payloads; other
verbose messages can still contain filenames and paths.

**Development builds need the updated scripts installed too.** Web assets are copied
from `vnote_extra.rcc` to the app-data directory and refreshed by version, not by source
mtime. After rebuilding and closing VNote, refresh only the two changed scripts on Windows:

```powershell
Copy-Item -Path src/data/extra/web/js/mathjax.js,src/data/extra/web/js/graphpreviewer.js -Destination "$env:APPDATA/VNote/web/js/"
```

For a cold-start capture, close VNote with the affected note in Edit Only, clear the
previous output, then restart with the flags above. Leave the window visible and active;
do not type, scroll, resize, switch modes/themes, or open DevTools. If previews are still
missing after 90 seconds, switch to Read once and wait another 10 seconds. Save the full
output from launch onward and note when previews appeared or the mode was switched.
Normal launches without the preview debug category do not enable math tracing.

## Using Logging in Code

Use Qt's logging macros with VNote categories:

```cpp
#include <core/logging.h>

// In source files:
using namespace vnotex;

void MyClass::doSomething() {
  qDebug(lcSync) << "Sync started for notebook" << notebookId;
  qInfo(lcBuffer) << "Opened buffer:" << bufferPath;
  qWarning(lcConfig) << "Config key missing:" << key;
  qCritical(lcUi) << "Critical UI error:" << error;
}
```

Each category is a `QLoggingCategory` constant that can be passed to Qt's logging macros.

## Viewing Logs

By default, VNote writes logs to:
- **Linux/macOS**: `~/.local/share/VNote/vnote.log`
- **Windows**: `%LOCALAPPDATA%/VNote/vnote.log`

View logs with a text editor or command line:

```bash
# Follow logs in real-time (Linux/macOS)
tail -f ~/.local/share/VNote/vnote.log

# On Windows
Get-Content "$env:LOCALAPPDATA/VNote/vnote.log" -Wait
```

## Trade-offs and Caveats

### Chromium Logging Suppression

At startup (in `src/main.cpp`), VNote **merges** its default flags into `QTWEBENGINE_CHROMIUM_FLAGS` instead of overwriting the variable. On every platform it adds `--disable-logging`; on Linux it also adds `--no-sandbox`. Any flags you set yourself are preserved, and a default is skipped when you already supplied it (or its counterpart). `--disable-logging` suppresses verbose Chromium debug output, particularly:
- `[####:####:date/time:WARNING:viz_main_impl.cc(...)] VizNullHypothesis is disabled` (spurious warning from Chromium's graphics pipeline)
- Other internal Chromium chatter

**Trade-off**: `--disable-logging` also hides some genuine Chromium ERROR and WARNING lines that may be useful when debugging WebEngine rendering issues or JavaScript errors in the Markdown viewer.

**Restore Chromium logs**: run VNote with `--enable-logging` in the variable; VNote detects it and skips its own `--disable-logging`:

```bash
# Linux/macOS
export QTWEBENGINE_CHROMIUM_FLAGS=--enable-logging
vnote

# Windows (PowerShell)
$env:QTWEBENGINE_CHROMIUM_FLAGS = "--enable-logging"
& "path/to/vnote.exe"

# Windows (Command Prompt)
set QTWEBENGINE_CHROMIUM_FLAGS=--enable-logging
vnote.exe
```

**Pass Chromium workaround flags**: because VNote no longer clobbers this variable, you can add your own flags to work around platform-specific QtWebEngine crashes (for example the `Failed to parse extension manifest` abort seen on some Linux stacks) and VNote keeps them alongside its defaults:

```bash
# Linux example: try single-process / software rendering
export QTWEBENGINE_CHROMIUM_FLAGS="--single-process --disable-gpu"
vnote
```

### VTextEdit Debug Suppression

VTextEdit (the editor library) emits bare `qDebug()` calls (e.g., `"XXX : not implemented yet"`). These are suppressed by the default logging rule `*.debug=false`. To see them during development:

```bash
# Linux/macOS
export QT_LOGGING_RULES="*.debug=true"
vnote

# Windows (PowerShell)
$env:QT_LOGGING_RULES = "*.debug=true"
& "path/to/vnote.exe"

# Windows (Command Prompt)
set QT_LOGGING_RULES=*.debug=true
vnote.exe
```

This will enable debug output for all categories, including VTextEdit. To selectively re-enable only VTextEdit debug while keeping VNote's web.js and vim categories suppressed:

```bash
export QT_LOGGING_RULES="*.debug=true;vnote.web.js.debug=false;vnote.vim.debug=false"
vnote
```

## Related

- [Architecture Documentation](../AGENTS.md) - Overall VNote architecture
- [Qt Logging Documentation](https://doc.qt.io/qt-6/qloggingcategory.html) - Qt logging framework reference
