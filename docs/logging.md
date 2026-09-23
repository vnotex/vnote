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

## Git Sync Diagnostics

Normal launches record sync lifecycle and Git phase diagnostics at INFO; no `--verbose`
or debug logging rules are required. Do not use `--quiet` when collecting a report.

For a sync failure, use **Sync Now** on the affected notebook, wait for the result (or
note that it remains running), and share the complete `vnote.log` with the VNote version
and approximate attempt time. Do not paste a PAT or credentials into the report.

Read the sequence as follows:

- `SyncService::triggerSyncNow` / `enqueueAutoSync`: accepted, coalesced, full or rejected
  queue submission. Accepted does not mean the worker has started.
- `SyncOps::triggerSync start`: worker entry, followed by IO-gate wait/acquisition,
  `stage_commit`, `network`, and the final numeric result and `elapsed_ms`.
- `GitSync ... phase=... event=start|finish`: phases are `config`, `stage`, `commit`,
  `fetch`, `rebase`, `push`, `initialize`, `clone`, `bootstrap-fetch`,
  `bootstrap-checkout`, `remote-probe` and `continue-rebase`. Nonzero `code` is a failure;
  an unmatched start identifies the last phase entered, not necessarily the cause of a stall.
- `phase=push-retry`: existing attempt limit and backoff delay. Fetch failures do not
  enter a new retry policy; only the existing push-retry loop is instrumented.
- `SyncService::onSyncFinished`: result delivered to the GUI, including ordinary success
  and non-authentication failures previously hidden at DEBUG.

Join GUI lifecycle entries by `notebookId`. Join a `SyncOps` worker entry to its Git
phases by the matching `worker` hash; `context` is an opaque in-process object identifier.
These identifiers may be reused and must not be compared across application sessions.
GUI callbacks run on a different thread and do not carry the Git worker identifier.

Credential diagnostics report only provider/PAT presence, URL-username presence or
`username_source=url|default`, allowed credential types and callback results. The first
three credential callbacks are logged, followed by one suppression marker; the network
phase summary contains the total `callback_attempts`. Callback `code=0` means credentials
were constructed, not accepted by the server. Zero callbacks can occur with anonymous or
preemptive authentication and does not prove a PAT was absent.

The added diagnostics never record credential values, remote URLs, HTTP headers/bodies,
usernames, branch/file names or note content. No raw HTTP tracing is enabled. Existing
log messages may still contain local paths and underlying error text; review before
sharing. A generic libgit2 "redirects or authentication replays" error remains ambiguous:
callback counts help distinguish the paths, but are not an HTTP status trace.

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
