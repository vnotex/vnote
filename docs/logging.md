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
- **Windows**: `%APPDATA%/VNote/vnote.log`

View logs with a text editor or command line:

```bash
# Follow logs in real-time (Linux/macOS)
tail -f ~/.local/share/VNote/vnote.log

# On Windows
Get-Content "$env:APPDATA/VNote/vnote.log" -Wait
```

## Related

- [Architecture Documentation](../AGENTS.md) - Overall VNote architecture
- [Qt Logging Documentation](https://doc.qt.io/qt-6/qloggingcategory.html) - Qt logging framework reference
