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
