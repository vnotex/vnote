# Agent Guidelines for src/core
src/core contains VNote's dependency injection container, core service layer, and plugin hook system.
Use this module for ServiceLocator wiring, service contracts, Buffer2 workflows, and typed hook integration.

## ServiceLocator Pattern

### Registration (in main.cpp)

```cpp
#include <core/servicelocator.h>
#include <core/configcoreservice.h>
#include <core/notebookcoreservice.h>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  // Create vxcore context
  vxcore_context *ctx = vxcore_context_create();

  // Create and populate ServiceLocator
  vnotex::ServiceLocator services;
  services.registerService<vnotex::ConfigCoreService>(
      std::make_unique<vnotex::ConfigCoreService>(ctx));
  services.registerService<vnotex::NotebookCoreService>(
      std::make_unique<vnotex::NotebookCoreService>(ctx));

  // Pass to UI
  vnotex::MainWindow2 mainWindow(services);
  mainWindow.show();

  int result = app.exec();
  vxcore_context_destroy(ctx);
  return result;
}
```

### Usage in Widgets

```cpp
// src/widgets/mywidget.h
class MyWidget : public QWidget {
  Q_OBJECT
public:
  explicit MyWidget(ServiceLocator &p_services, QWidget *p_parent = nullptr);

private:
  ServiceLocator &m_services;
};

// src/widgets/mywidget.cpp
MyWidget::MyWidget(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {
  // Access services when needed
  auto *config = m_services.get<ConfigCoreService>();
  auto configJson = config->getConfig();
}
```

### Available Services

| Service | Purpose | Key Methods |
|---------|---------|-------------|
| `ConfigMgr2` | High-level config manager (owns `MainConfig`, `SessionConfig`, resolves paths) | `getConfig()`, `getEditorConfig()`, `getCoreConfig()`, `getFileFromConfigFolder()`, `getAppDataPath()` |
| `ConfigCoreService` | Low-level app configuration via vxcore C API | `getConfig()`, `getSessionConfig()`, `getDataPath()`, `updateConfigByName()` |
| `NotebookCoreService` | Notebook/folder/file operations | `createNotebook()`, `openNotebook()`, `createFile()`, `listFolderChildren()` |
| `BufferCoreService` | Low-level buffer operations via vxcore | `openBuffer()`, `closeBuffer()`, `saveBuffer()`, `getContentRaw()`, `setContentRaw()`, `getState()`, `insertAsset()`, `listAttachments()` |
| `BufferService` | Hook-aware buffer wrapper; returns `Buffer2` handles | `openBuffer(NodeIdentifier)` → `Buffer2`, `closeBuffer()`, `listBuffers()`, `autoSaveTick()` |
| `SearchCoreService` | Content and file search | `searchFiles()`, `searchContent()`, `searchByTags()` |
| `FileTypeCoreService` | File type detection | `getFileType()`, `getFileTypeBySuffix()`, `getAllFileTypes()` |
| `TemplateService` | Note template management | `getTemplates()`, `getTemplateContent()`, `getTemplateFilePath()` |
| `WorkspaceCoreService` | Workspace (split pane) operations via vxcore | `createWorkspace()`, `deleteWorkspace()`, `listWorkspaces()`, `addBuffer()`, `removeBuffer()` |
| `HookManager` | Plugin hook system | `addAction()`, `doAction()`, `addFilter()`, `applyFilters()` |
| `SyncStateClassifier` | Stateless sync state classifier (maps notebook ID to SyncState S0-S7) | `classify()`, `classifyFromPredicates()`, `tooltipFor()`, `isPartial()`, `isActionable()` |

### ConfigMgr2 vs ConfigCoreService (CRITICAL — Read Before Using Config)

`ConfigMgr2` and `ConfigCoreService` both deal with configuration, but they serve **different roles**. Using the wrong one will cause subtle runtime bugs.

| | `ConfigMgr2` | `ConfigCoreService` |
|---|---|---|
| **Level** | High-level (Qt/C++ typed config objects) | Low-level (raw JSON via vxcore C API) |
| **Owns** | `MainConfig`, `SessionConfig` (fully initialized with defaults + user overrides) | Nothing — stateless pass-through to vxcore |
| **Config access** | `getEditorConfig().getMarkdownEditorConfig()` → typed C++ objects | `getConfig()` → raw `QJsonObject` |
| **Path resolution** | `getFileFromConfigFolder(relativePath)` — resolves against app data path | `getDataPath(DataLocation::App)` — returns raw directory path |
| **Use when** | You need config values (editor settings, theme, shortcuts, etc.) | You need to read/write raw JSON config files directly |

#### NEVER Do This (Anti-Pattern)

```cpp
// WRONG: Creating a throwaway MainConfig to parse raw JSON
// This fails because the raw JSON may not contain all sections
// (e.g., viewerResource, exportResource), causing defaults to be wiped.
ConfigCoreService *svc = m_services.get<ConfigCoreService>();
MainConfig tempConfig(someIConfigMgr);
tempConfig.fromJson(svc->getConfig());  // BUG: missing keys → empty defaults
auto &mdConfig = tempConfig.getEditorConfig().getMarkdownEditorConfig();
```

#### Correct Pattern

```cpp
// RIGHT: Get the already-initialized config from ConfigMgr2
auto *configMgr = m_services.get<ConfigMgr2>();
const auto &mdConfig = configMgr->getEditorConfig().getMarkdownEditorConfig();

// RIGHT: Resolve config-relative file paths
QString templatePath = configMgr->getFileFromConfigFolder("web/markdown-viewer-template.html");
// Returns: "C:/Users/.../AppData/Roaming/VNote/web/markdown-viewer-template.html"
// (or the path unchanged if it's already absolute)
```

#### Why ConfigMgr2 Exists

`ConfigMgr2` wraps `ConfigCoreService` and adds:
1. **Default merging** — `ConfigMgr2::init()` loads the `vnotex.json` config file via `ConfigCoreService::getConfigByName()`, then calls `MainConfig::fromJson()`. The `MainConfig` constructor first calls `initDefaults()` on all child configs, then `fromJson()` overlays only the keys present in the JSON. Missing keys keep their C++ defaults.
2. **Debounced persistence** — Changes to config objects auto-save via 500ms debounced timers.
3. **Path resolution** — `getFileFromConfigFolder()` resolves relative paths against the app data directory.
4. **Version upgrade** — Handles copying themes, web resources, etc. on version change.

### VxCoreLogBridge

`VxCoreLogBridge` routes vxcore's internal log lines through Qt's `qInstallMessageHandler` pipeline so they land in VNote's unified log file alongside `qDebug`/`qWarning`/`qCritical` output. Without it, vxcore would write to its own stderr/file sinks and the two log streams would diverge.

**Files:** `src/core/vxcorelogbridge.h` / `.cpp`. The bridge is exposed as two free functions in the `vnotex` namespace:

```cpp
namespace vnotex {
void installVxCoreLogBridge();
void uninstallVxCoreLogBridge();
}
```

**Install order.** `installVxCoreLogBridge()` MUST be called from `src/main.cpp` **before the first vxcore API call**, currently before `vxcore_set_app_info` at line 158. Installing earlier ensures the very first vxcore log lines (e.g., context creation, config load) are captured by the Qt handler. `uninstallVxCoreLogBridge()` restores the default vxcore sinks; it is safe to omit at shutdown but is provided for symmetry and tests.

**Level translation.**

| vxcore level | Qt level |
|---|---|
| `VXCORE_LOG_LEVEL_TRACE` | `QtDebugMsg` |
| `VXCORE_LOG_LEVEL_DEBUG` | `QtDebugMsg` |
| `VXCORE_LOG_LEVEL_INFO` | `QtInfoMsg` |
| `VXCORE_LOG_LEVEL_WARN` | `QtWarningMsg` |
| `VXCORE_LOG_LEVEL_ERROR` | `QtCriticalMsg` |
| `VXCORE_LOG_LEVEL_FATAL` | `QtCriticalMsg` (NOT `QtFatalMsg`) |
| `VXCORE_LOG_LEVEL_OFF` | no-op |

**Why FATAL maps to Critical, not Fatal.** VNote's installed Qt message handler calls `abort()` on `QtFatalMsg`. A vxcore FATAL log line should not terminate the application; mapping it to `QtCriticalMsg` preserves the log entry (with the original `[FATAL]` prefix carried in the message text) without killing the process.

**Why the thunk uses the `"%s", message` form.** vxcore hands the callback a pre-formatted message string. Passing it through Qt as `qCDebug(cat, "%s", message)` (rather than `qCDebug(cat, message)`) ensures any literal `%` characters inside the message are treated as data, not as printf-style format specifiers that Qt would try to re-interpret.

### Shared JSON Keys (SSOT)

Cross-boundary JSON keys (vxcore writes ↔ Qt reads, or vice versa) live in the
public vxcore header [`<vxcore/notebook_json_keys.h>`](../../libs/vxcore/include/vxcore/notebook_json_keys.h).
Use the `kJsonKey*` constants instead of inline string literals on the Qt side:

```cpp
#include <vxcore/notebook_json_keys.h>
// ...
const QString rootFolder =
    notebookJson.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
```

The grep-gate test `tests/utils/test_json_key_drift.cpp` fails the build if
shared-schema literals are reintroduced in `src/`. See
[`libs/vxcore/AGENTS.md` § JSON Conventions](../../libs/vxcore/AGENTS.md#json-conventions)
for the full contract.

### Buffer2 Handle Pattern

`Buffer2` is a **lightweight copyable value type** (like `QModelIndex`) that represents an open file buffer. It is NOT a `QObject` and is NOT heap-allocated. Obtain a `Buffer2` from `BufferService::openBuffer()`.

#### Opening a Buffer

```cpp
auto *bufferSvc = m_services.get<BufferService>();

// Identify the file to open
NodeIdentifier nodeId;
nodeId.notebookId = "my-notebook-guid";
nodeId.relativePath = "notes/hello.md";

// Open returns a lightweight handle (fires vnote.file.before_open / after_open hooks)
Buffer2 buf = bufferSvc->openBuffer(nodeId);
if (!buf.isValid()) {
  // Open failed or was cancelled by a hook
  return;
}
```

#### Per-Buffer Operations via Handle

```cpp
// Read/write content
QByteArray content = buf.getContentRaw();
buf.setContentRaw("# Updated content\n");

// Save (fires vnote.file.before_save / after_save hooks)
buf.save();

// Check state
if (buf.isModified()) { /* unsaved changes */ }

// Assets and attachments
buf.insertAssetRaw("image.png", pngData);
buf.insertAttachment("/path/to/file.pdf");
QJsonArray attachments = buf.listAttachments();

// Access node identity
NodeIdentifier id = buf.nodeId();  // notebookId + relativePath
```

#### Closing a Buffer

```cpp
// Close via BufferService (cleans up auto-save state and closes the vxcore buffer)
bufferSvc->closeBuffer(buf.id());
```

## Theme Token Resolution

VNote's theme system supports a one-time opt-in palette token syntax that lets theme authors reference shared colors instead of hardcoding hex values. The token system was originally limited to `interface.qss` and is now extended to `web.css` and `text-editor.theme`.

### Token Syntax

```
@palette#<key>          # Direct palette lookup (e.g. @palette#fg3_5 → #222222)
@base#<role>            # Semantic role (e.g. @base#normal#fg → resolved hex)
@base#<role>#<sub>      # Nested role
@widgets#<class>#...    # Per-widget mapping
```

The `#` character is a path separator (NOT a hex marker). Resolution walks the JSON in `palette.json` recursively until all references collapse to hex strings.

### Files In Scope (token-aware)

| File | Resolved by | Purpose |
|------|-------------|---------|
| `interface.qss` | `Theme::fetchQtStyleSheet()` (existing) | Qt desktop UI stylesheet |
| `web.css` | `Theme::fetchWebStyleSheet()` (NEW) | Markdown viewer CSS |
| `text-editor.theme` | `Theme::fetchTextEditorStyle()` (NEW) | vtextedit editor JSON |

The corresponding `ThemeService` delegates expose these to consumers:

```cpp
auto *themeService = m_services.get<ThemeService>();
QString webCss   = themeService->fetchWebStyleSheet();
QString editor   = themeService->fetchTextEditorStyle();
```

### Files Out of Scope (not token-aware)

These files load and apply as-is, with no `@`-token substitution:

- `highlight.css`, PrismJS code-block CSS (third-party, kept verbatim)
- `editor-highlight.theme` / `markdown-editor-highlight.theme`, KSyntaxHighlighting JSON (vendored, format-specific)

### Backward Compatibility

Existing themes use hardcoded hex throughout. The new resolver is a no-op on files that contain no `@` tokens. Running `translateStyleByPalette` on `body { color: #222222; }` returns the input unchanged. All 10 shipped themes (pure, native, vue-light, vue-dark, vscode-dark, solarized-light, solarized-dark, moonlight, everforest-dark, vx-idea) continue to render exactly as before.

### Authoring Example

A theme author can now write `text-editor.theme` like this:

```json
{
  "metadata": { "name": "MyTheme", "revision": 1, "type": "vtextedit" },
  "editor-styles": {
    "Text": {
      "text-color": "@base#normal#fg",
      "background-color": "@base#normal#bg"
    },
    "CursorLine": { "background-color": "@palette#bg3_8" }
  }
}
```

At theme load time, `Theme::fetchTextEditorStyle()` reads the file, resolves `@base#normal#fg` to e.g. `#222222`, and returns the resolved JSON to vtextedit via `vte::Theme::createThemeFromContent()`.

The same applies to `web.css`:

```css
body {
  color: @base#normal#fg;
  background-color: @base#normal#bg;
}
```

### Implementation Notes

- The token regex (`Theme::translateStyleByPalette` in `src/core/theme.cpp`) accepts whitespace, colon, or double-quote as the prefix character. This is the one extension that enables JSON-quoted values like `"text-color": "@palette#fg3_5"`.
- Resolution is performed lazily, on each `fetch*()` call. There is no caching, no temp file, no precomputed asset.
- Consumers (`MarkdownViewWindow2`, `TextViewWindow2`) pass the resolved string content directly into `vte::Theme::createThemeFromContent()` and `HtmlTemplateService::updateMarkdownViewerTemplate()`. Both were updated to accept content rather than file paths.
- This capability is **opt-in**: existing themes do not need any changes. New themes can adopt tokens incrementally per file.

## Theme Token Conversion Convention

Theme authors should use palette tokens to share colors between `web.css` and `text-editor.theme`. This convention has been demonstrated on 3 themes (pure, everforest-dark, moonlight) as a pilot. The remaining 7 themes ship hardcoded hex and will convert in future tasks.

### Naming Convention

Tokens follow a flat naming scheme with semantic prefixes that describe the concept being colored, not the syntax category:

| Prefix | Purpose | Example tokens |
|---|---|---|
| `fg_<concept>` | text color | `fg_link`, `fg_inline_code`, `fg_blockquote`, `fg_heading` |
| `bg_<concept>` | background color | `bg_code_block`, `bg_search`, `bg_search_current`, `bg_search_incremental`, `bg_mark`, `bg_hr` |
| `border_<concept>` | border color | `border_table` |

### Semantic `@base#...` vs Raw `@palette#...`

Prefer semantic `@base#...` tokens when the theme's `base` mappings actually resolve to the value being substituted. Each theme MUST be preflight-verified before substitution because not all themes use the same role names.

Common semantic uses:

- `@base#normal#fg` or `@base#content#fg` for body text color (depends on theme: pure uses `normal`, moonlight uses `content`).
- `@base#content#bg` for body background color.
- `@base#content#selection#fg` and `@base#content#selection#bg` for selection foreground and background.

If the theme's `base` mapping does NOT match the value being replaced, fall back to a raw `@palette#xxx` reference instead.

### Per-Concept Opt-Out Matrix

The viewer (`web.css`) color wins by default for shared concepts. A theme may opt out per concept to preserve source-syntax distinction inside the editor:

| Theme | Concept | Decision |
|---|---|---|
| pure | FENCEDCODEBLOCK + VERBATIM | OPT-OUT (keep editor purple `#673ab7` to distinguish source code from prose) |
| everforest-dark | FENCEDCODEBLOCK + VERBATIM | OPT-OUT (keep editor green `#A7C080` to distinguish source code from prose) |
| moonlight | (none) | UNIFIED (editor and web both already used `#98c379`) |

Authors should document any opt-out decision in the plan or commit message with a one-line rationale so future contributors understand why a token was not adopted.

### Out-of-Scope Items

The conversion pilot does NOT tokenize:

- Editor chrome: `CursorLine`, `TrailingSpace`, `Tab`, `IndicatorsBorder`, `Folding`, `FoldedFolding`, `FoldingHighlight`, `FoldedFoldingRangeLine`.
- Font families, font sizes, padding, margin, line-height, border-radius.
- Alert system (`.vx-alert*`, `.alert-*`).
- Graph backgrounds (`div.vx-mermaid-graph` and similar).
- Scrollbar `rgba(...)` values (the token regex does not accept `(` as a prefix character).
- CSS named colors (`purple`, `transparent`, `currentColor`).
- CSS comments (`/* ... */`).
- Source-text concepts with no rendered HTML analog: `LIST_BULLET`, `LIST_ENUMERATOR`, `IMAGE`, `REFERENCE`, `HTML_ENTITY`, `HTML`, `HTMLBLOCK`, `COMMENT`, `NOTE`, `FRONTMATTER`, `INLINEEQUATION`, `DISPLAYFORMULA`, `TABLEBORDER`.

### Verification Template

When converting a new theme, add 2 tests to `tests/core/test_theme.cpp`:

- `testThemeFullyResolved_<name>()`: asserts that the resolved `web.css` and `text-editor.theme` contain no `@palette#` or `@base#` substrings, and that the resolved JSON still parses cleanly.
- `testEditorWebConceptParity_<name>()`: asserts that resolved editor concept colors equal resolved viewer concept colors for heading, link, inline code, blockquote, search match background, and current search background.

### Token Regex Limitations

The token resolver `Theme::translateStyleByPalette` in `src/core/theme.cpp` requires the `@` character to be preceded by whitespace, a colon, or a double-quote. Consequences:

- Tokens cannot appear inside `rgba(...)` because `(` is not an accepted prefix.
- Tokens cannot appear in CSS at-rules like `@media` or `@font-face`.
- Tokens MUST be quoted strings in JSON: `"text-color": "@palette#fg_link"`, not `"text-color": @palette#fg_link`.
- The palette section is NOT token-resolved. All entries in the `palette` object of `palette.json` must be literal hex; aliases like `"fg_link": "@palette#bg11"` will fail. Reference: comment at `src/core/theme.cpp:107` "Skip paletteSection since it will not contain any reference."
- Tokens inside `/* */` CSS comments would still be resolved by the regex, so keep tokens out of comments to avoid confusing the resolver.

### Reference

For per-theme conversion matrices used by the pilot (which palette key maps to which CSS concept on each theme), see `.sisyphus/plans/theme-token-pilot.md` (Tasks 3, 4, 5).

## SyncService

`SyncService` wraps the vxcore sync C API for the Qt layer. It owns the worker thread, signal contract, and partial-state recovery logic. See the root [Sync State Model](../../AGENTS.md#sync-state-model) for the S0-S7 predicates.

### `isSyncRegistered(notebookId)` (runtime state check)

`bool SyncService::isSyncRegistered(const QString &p_notebookId) const` (`src/core/services/syncservice.h:125-138`, `.cpp:375-387`) answers the runtime question "is this notebook in vxcore's `states_` map?" It is distinct from `isSyncReady` (disk-only check on JSON sync fields).

| Helper | Source | Answers |
|---|---|---|
| `isSyncReady(id)` | disk JSON (`syncEnabled` + `syncBackend` + `syncRemoteUrl`) | "should sync be enabled?" |
| `isSyncRegistered(id)` | vxcore `states_` map via `vxcore_sync_get_status` | "is sync runtime-registered right now?" |

Use `isSyncRegistered` to detect S4 (disk-complete but runtime-absent) from S5 (fully registered). Main-thread-only; SyncManager is not thread-safe. Do not cache (state changes asynchronously).

### `updateCredentials(notebookId, newPat)` routing

`updateCredentials` (`src/core/services/syncservice.cpp:282-336`) branches on registration state:

- **Registered (S5)** → dispatches `SyncWorker::setCredentials` which calls `vxcore_sync_set_credentials`. PAT-only refresh; runtime `states_` entry preserved.
- **Unregistered + enabled-on-disk (S2/S4)** → reads `syncRemoteUrl` from notebook JSON and re-routes through `enableSyncForNotebook(id, persistedUrl, newPat)`. This is the chicken-and-egg fix: `vxcore_sync_set_credentials` returns `VXCORE_ERR_SYNC_NOT_ENABLED (25)` on unregistered notebooks, so the call must instead drive the full atomic enable flow.
- **Unregistered + no URL on disk** → emits `credentialsSetFinished(id, VXCORE_ERR_INVALID_PARAM)` with a `qCWarning` log; cannot recover without a URL.

The unregistered branch installs a one-shot bridge lambda on `enableFinished` that filters by notebookId, self-disconnects via heap-stored `QMetaObject::Connection*`, then re-emits as `credentialsSetFinished`. Callers can rely on `credentialsSetFinished` as the single completion signal regardless of which internal path runs.

## Plugin Hook System (WordPress-style)

VNote implements a WordPress-inspired hook system for plugin extensibility. This enables plugins to:
- **Intercept events** (e.g., notebook opening, node activation) and optionally cancel them
- **Transform data** (e.g., modify display names, filter content before save)

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     HookManager                             │
│  (Registered in ServiceLocator, single-threaded)            │
│                                                             │
│  ┌─────────────────────┐  ┌─────────────────────────────┐  │
│  │  Actions (Events)   │  │  Filters (Data Transform)   │  │
│  │  - Cancellable      │  │  - Chain execution          │  │
│  │  - Priority-ordered │  │  - Return modified value    │  │
│  └─────────────────────┘  └─────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### Hook Types

| Type | Purpose | Return Value |
|------|---------|--------------|
| **Action** | Event notification, can cancel downstream processing | `void` (via `HookContext`) |
| **Filter** | Transform data through a chain of handlers | Modified `QVariant` |

### Key Files

| File | Purpose |
|------|---------|
| `src/core/hookcontext.h` | Context passed to callbacks (cancellation, metadata) |
| `src/core/hooknames.h` | Hook name constants (`vnote.notebook.before_open`, etc.) |
| `src/core/services/hookmanager.h` | Main hook manager API |
| `src/core/exampleplugin.h` | Example plugin demonstrating hook usage |
| `tests/core/test_hookmanager.cpp` | Unit tests (24 test cases) |
| `tests/core/test_hookintegration.cpp` | Integration tests (10 test cases) |

### Registering Hooks

```cpp
#include <core/hooknames.h>
#include <core/services/hookmanager.h>

// Get HookManager from ServiceLocator
auto *hookMgr = m_services.get<HookManager>();

// Register an action (event handler)
int actionId = hookMgr->addAction(
    HookNames::NotebookBeforeOpen,
    [](HookContext &p_ctx, const QVariantMap &p_args) {
      QString notebookId = p_args.value("notebookId").toString();
      qDebug() << "Opening notebook:" << notebookId;

      // Optional: Cancel the action
      // p_ctx.cancel();
    },
    10); // Priority: lower = earlier execution

// Register a filter (data transformer)
int filterId = hookMgr->addFilter(
    HookNames::FilterNodeDisplayName,
    [](const QVariant &p_value, const QVariantMap &p_context) -> QVariant {
      QString name = p_value.toString();
      // Transform and return
      return "[Modified] " + name;
    },
    10);
```

### Hook Emission Rule (CRITICAL)

**Hooks MUST be emitted inside services, NOT in controllers or widgets.**

The service layer owns the hook contract. Any caller that invokes a service operation automatically gets the correct before/after hooks fired. Controllers translate user actions into service calls and service results into UI feedback — they do not orchestrate hooks.

**Why:**
1. **Enforced contract** — Every caller gets hooks automatically; no caller can accidentally skip them
2. **No duplication** — A second controller or CLI command doesn't need to re-implement hook logic
3. **Clean separation** — Controllers handle user intent → service calls → UI feedback; services handle operation + hooks
4. **Testability** — Testing a service tests its hooks; testing a controller doesn't need hook setup

**Correct (hooks inside service):**
```cpp
// In BufferService::save() — service fires hooks around the operation
OperationResult BufferService::save(const Buffer2 &p_buffer) {
    QVariantMap args;
    args["bufferId"] = p_buffer.id();
    if (m_hookMgr->doAction(HookNames::FileBeforeSave, args)) {
        return OperationResult::Cancelled;
    }

    bool ok = m_coreService.saveBuffer(p_buffer.id());

    if (ok) {
        m_hookMgr->doAction(HookNames::FileAfterSave, args);
    }
    return ok ? OperationResult::Success : OperationResult::Failed;
}

// In Controller — just calls service, checks result
void MyController::onSaveRequested(const Buffer2 &p_buffer) {
    auto result = m_bufferService->save(p_buffer);
    if (result == OperationResult::Cancelled) {
        emit statusMessage(tr("Save blocked by plugin"));
    }
}
```

**Wrong (hooks in controller/widget):**
```cpp
// DON'T DO THIS — controller firing hooks directly
void MyController::doSomething() {
    auto *hookMgr = m_services.get<HookManager>();
    if (hookMgr && hookMgr->doAction(HookNames::SomeBeforeHook, args)) {
        return; // Cancelled
    }
    m_someService->doOperation();  // Hook contract bypassed if called elsewhere
    if (hookMgr) {
        hookMgr->doAction(HookNames::SomeAfterHook, args);
    }
}
```

**Exception — UI lifecycle hooks:** `MainWindowBeforeShow`, `MainWindowAfterShow`, `MainWindowAfterStart`, `MainWindowBeforeClose` are inherently tied to the widget lifecycle (QWidget::show, QCloseEvent). These may remain in `MainWindow2` until a dedicated `AppLifecycleService` is introduced.

#### Hook Ownership Map

| Service | Hook(s) Owned |
|---------|---------------|
| `BufferService` | `FileBeforeOpen`/`AfterOpen` |
| `Buffer2` (handle) | `FileBeforeSave`/`AfterSave` |
| `NotebookCoreService` | `NodeBeforeDelete`, `NodeBeforeMove`, `NodeBeforeRename`/`AfterRename` |
| `WorkspaceCoreService` | `ViewWindowBefore/AfterOpen/Close/Move`, `ViewSplitBefore/AfterCreate/Remove/Activate` |
| `MainWindow2` (exception) | `MainWindowBefore/AfterShow`, `MainWindowAfterStart`, `MainWindowBeforeClose` |

### Typed Hook API

VNote uses a type-safe hook event system. Each hook has a corresponding C++ struct defined in `src/core/hookevents.h`. **Always use the typed API in C++ code.** The raw `QVariantMap` API exists only for plugin compatibility.

**Event structs:** Each struct has `toVariantMap()` and `static fromVariantMap()` for bridging to the raw API.

| Struct | Used by hooks |
|--------|--------------|
| `NodeOperationEvent` | `NodeBeforeDelete`, `NodeBeforeMove` |
| `NodeRenameEvent` | `NodeBeforeRename`, `NodeAfterRename` |
| `FileOpenEvent` | `FileBeforeOpen`, `FileAfterOpen` |
| `BufferEvent` | `FileBeforeSave`, `FileAfterSave` |
| `ViewWindowOpenEvent` | `ViewWindowBeforeOpen`, `ViewWindowAfterOpen` |
| `ViewWindowCloseEvent` | `ViewWindowBeforeClose`, `ViewWindowAfterClose` |
| `ViewWindowMoveEvent` | `ViewWindowBeforeMove`, `ViewWindowAfterMove` |
| `ViewSplitCreateEvent` | `ViewSplitBeforeCreate`, `ViewSplitAfterCreate` |
| `ViewSplitRemoveEvent` | `ViewSplitBeforeRemove`, `ViewSplitAfterRemove` |
| `ViewSplitActivateEvent` | `ViewSplitBeforeActivate`, `ViewSplitAfterActivate` |

**Emitting hooks (services):**
```cpp
// CORRECT: use typed event
NodeOperationEvent event;
event.notebookId = p_notebookId;
event.relativePath = p_filePath;
event.isFolder = false;
event.name = extractName(p_filePath);
event.operation = QStringLiteral("delete");
if (m_hookMgr->doAction(HookNames::NodeBeforeDelete, event)) { return false; }

// WRONG: manual QVariantMap construction
QVariantMap args;
args["notebookId"] = p_notebookId;  // fragile string keys, no compile-time checking
```

**Subscribing to hooks (C++ code):**
```cpp
// CORRECT: typed subscription
hookMgr->addAction<NodeRenameEvent>(HookNames::NodeAfterRename,
    [this](HookContext &ctx, const NodeRenameEvent &event) {
      qDebug() << event.oldName << "->" << event.newName;  // type-safe access
    }, 10);

// WRONG: raw QVariantMap subscription in C++ code
hookMgr->addAction(HookNames::NodeAfterRename,
    [](HookContext &ctx, const QVariantMap &args) {
      QString oldName = args["oldName"].toString();  // fragile
    }, 10);
```

**Adding a new hook:**
1. Add hook name constant to `src/core/hooknames.h`
2. Add (or reuse) event struct in `src/core/hookevents.h` with `toVariantMap()` / `fromVariantMap()`
3. Add typed `doAction()` overload to `HookManager` if new struct type
4. Emit from the appropriate service using the typed API
5. Add round-trip test in `tests/core/test_hookevents.cpp`

### Available Hook Names

**Notebook Events:**
- `vnote.notebook.before_open` / `after_open`
- `vnote.notebook.before_close` / `after_close`

**Node Events:**
- `vnote.node.before_activate` / `after_activate`
- `vnote.node.before_create` / `after_create`
- `vnote.node.before_rename` / `after_rename`
- `vnote.node.before_delete` / `after_delete`
- `vnote.node.before_move` / `after_move`

**File Events:**
- `vnote.file.before_open` / `after_open`
- `vnote.file.before_save` / `after_save`

**UI Events:**
- `vnote.ui.mainwindow.before_show` / `after_show`
- `vnote.ui.contextmenu.before_show`

**ViewSplit Events:**
- `vnote.viewsplit.before_create` / `after_create`
- `vnote.viewsplit.before_remove` / `after_remove`

**ViewWindow Events:**
- `vnote.viewwindow.before_open` / `after_open`
- `vnote.viewwindow.before_close` / `after_close`
- `vnote.viewwindow.before_move` / `after_move`

**Filters:**
- `vnote.filter.node_display_name`
- `vnote.filter.file_content_before_save`
- `vnote.filter.file_content_after_load`
- `vnote.filter.context_menu_items`

**Sync Events (observe-only — see table below):**
- `vnote.sync.before_enable`
- `vnote.sync.after_disable`
- `vnote.sync.conflict_detected`
- `vnote.sync.cancelled`

### Sync Hooks (F4.5)

Fired by `SyncService`. ALL sync hooks are **observe-only**: `HookContext::cancel()` is ignored — handlers cannot abort the underlying op. Hooks fire OUTSIDE any `SyncService` mutex; conflict_detected fires on the GUI thread post-`QueuedConnection` bounce from the worker (no SyncManager locks held). PAT values are NEVER included in any sync hook payload.

| Hook | Fire site | Context keys |
|---|---|---|
| `vnote.sync.before_enable` | `SyncService::enableSyncForNotebook` BEFORE keychain store / worker dispatch | `notebookId`, `remoteUrl` |
| `vnote.sync.after_disable` | `SyncService::disableSyncForNotebook` AFTER vxcore disable returns VXCORE_OK and JSON sync fields are cleared. Not fired on disable failure. | `notebookId` |
| `vnote.sync.conflict_detected` | `SyncService::onWorkerConflictsDetected` (worker→GUI queued slot) AND `SyncService::onAutoSyncConflict` (EventBridge auto-sync) | `notebookId`, `conflictCount` (-1 if unknown at fire time, auto-sync only) |
| `vnote.sync.cancelled` | `SyncService::cancelSync` AFTER `vxcore_sync_cancel` returns (in-flight case) or after `SyncWorkQueueManager::cancelPending` returns (queued case). Best-effort: also fires when no in-flight token was found. | `notebookId`, `wasQueued` (bool) |

### Unregistering Hooks

```cpp
// Store the ID when registering
int actionId = hookMgr->addAction(...);

// Later, remove by ID
hookMgr->removeAction(actionId);
hookMgr->removeFilter(filterId);
```

### Error Handling

The hook system isolates errors to prevent app crashes:
- Exceptions in callbacks are caught and logged
- Errors emit `actionError` / `filterError` signals
- Other callbacks continue executing

### Testing Hooks

```cpp
void TestMyPlugin::testHookFires() {
  vnotex::HookManager hookMgr;
  bool hookFired = false;

  hookMgr.addAction(
      vnotex::HookNames::SomeHook,
      [&](vnotex::HookContext &, const QVariantMap &) {
        hookFired = true;
      },
      10);

  hookMgr.doAction(vnotex::HookNames::SomeHook, {});
  QVERIFY(hookFired);
}
```

### Adding a New Service

1. **Create service class** in `src/core/`:
```cpp
// src/core/myservice.h
#ifndef MYSERVICE_H
#define MYSERVICE_H

#include <QObject>

struct vxcore_context;

namespace vnotex {

class MyService : public QObject {
  Q_OBJECT
public:
  explicit MyService(vxcore_context *p_ctx, QObject *p_parent = nullptr);

  // Service methods
  QString doSomething() const;

signals:
  void somethingHappened();

private:
  vxcore_context *m_ctx;
};

} // namespace vnotex

#endif // MYSERVICE_H
```

2. **Register in main.cpp**:
```cpp
services.registerService<vnotex::MyService>(
    std::make_unique<vnotex::MyService>(ctx));
```

3. **Add to CMakeLists.txt**:
```cmake
# src/core/CMakeLists.txt
target_sources(core2 PRIVATE
  myservice.h
  myservice.cpp
)
```

## Related Modules

- [`../controllers/AGENTS.md`](../controllers/AGENTS.md), Controllers that use services
- [`../widgets/AGENTS.md`](../widgets/AGENTS.md), Widgets that receive ServiceLocator
- [`../gui/AGENTS.md`](../gui/AGENTS.md), GUI-aware services (ThemeService, ViewWindowFactory)
- [`../../tests/AGENTS.md`](../../tests/AGENTS.md), Testing services and hooks
- [`../../AGENTS.md`](../../AGENTS.md), Architecture overview, MVC rules, code style
