# VNote Agent Development Guide

## Prerequisites

- **Git** (with Git Bash on Windows)
- **CMake** 3.20+
- **Qt** 5.x or 6.x (with QtWebEngine)
- **C++14** compatible compiler (MSVC, GCC, Clang)
- **clang-format** (optional, for automatic code formatting)

## Setup

After cloning the repository, run the init script:

| Platform | Command |
|----------|---------|
| Linux/macOS | `bash scripts/init.sh` |
| Windows | `scripts\init.cmd` |

The init script:
1. Initializes and updates git submodules recursively
2. Installs pre-commit hook for automatic clang-format on staged C++ files
3. Sets up vtextedit submodule pre-commit hook

## Build Commands

### Release Build
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Debug Build
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug
```

### Windows (PowerShell)
```powershell
New-Item -ItemType Directory -Force -Path build
Set-Location build
cmake .. -GNinja
cmake --build . --config Release
```

### Clean Build
```bash
rm -rf build && mkdir build && cd build && cmake .. && cmake --build .
```

---

## Architecture Overview

VNote uses a **clean architecture** with **Model-View-Controller (MVC)** pattern and dependency injection for testability and future plugin support.

### Core Principles

1. **MVC Separation** — Models hold data, Views display it, Controllers handle logic
2. **Dependency Injection** — No singletons; dependencies passed via ServiceLocator
3. **Service Layer** — Business logic encapsulated in services, accessed via ServiceLocator
4. **Hook System** — WordPress-style extensibility for plugins

### MVC Architecture (CRITICAL)

VNote strictly follows the MVC pattern. **All new code MUST adhere to this structure.**

```
┌─────────────────────────────────────────────────────────────┐
│                     Controllers                             │
│  (src/controllers/ - Handle user actions, business logic)   │
│                                                             │
│  NotebookNodeController, NewNoteController, etc.            │
└─────────────────────────────────────────────────────────────┘
        │                                       │
        │ Manipulates                          │ Emits signals to
        ▼                                       ▼
┌───────────────────────┐       ┌──────────────────────────────┐
│        Models         │       │           Views              │
│  (src/models/)        │◄──────│  (src/views/)                │
│                       │       │                              │
│  NotebookNodeModel    │ Data  │  NotebookNodeView            │
│  (QAbstractItemModel) │ flows │  (QTreeView subclass)        │
└───────────────────────┘       └──────────────────────────────┘
        │                                       │
        │ Fetches data from                    │ Receives from
        ▼                                       ▼
┌─────────────────────────────────────────────────────────────┐
│                     ServiceLocator                          │
│  (DI container - NOT a singleton, passed by reference)      │
│                                                             │
│  ┌─────────────────┐  ┌─────────────────────┐  ┌───────────────────┐  │
│  │ConfigCoreService│  │ NotebookCoreService │  │SearchCoreService  │  │
│  └─────────────────┘  └─────────────────────┘  └───────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                         vxcore                              │
│  (C library: notebook/config/search backend in libs/vxcore) │
└─────────────────────────────────────────────────────────────┘
```

### MVC Responsibilities

| Layer | Location | Responsibility | Example |
|-------|----------|----------------|---------|
| **Model** | `src/models/` | Data representation, Qt Model/View integration | `NotebookNodeModel` exposes node hierarchy via `QAbstractItemModel` |
| **View** | `src/views/` | Display data, capture user input, emit signals | `NotebookNodeView` renders tree, emits `nodeActivated` signal |
| **Controller** | `src/controllers/` | Handle actions, orchestrate Model/View, business logic | `NotebookNodeController` handles new/delete/rename operations |
| **Service** | `src/core/services/` | Domain operations, data access via vxcore | `NotebookCoreService` wraps vxcore C API for notebook CRUD |

### MVC Example: Notebook Node Operations

```cpp
// Controller handles user action
void NotebookNodeController::newNote(const NodeIdentifier &p_parentId) {
  // 1. Emit signal to View to show dialog
  emit newNoteRequested(p_parentId);
}

// View shows dialog, then calls back to Controller
void NotebookExplorer2::onNewNoteResult(const NodeIdentifier &p_parentId,
                                        const NodeIdentifier &p_newNodeId) {
  m_controller->handleNewNoteResult(p_parentId, p_newNodeId);
}

// Controller updates Model
void NotebookNodeController::handleNewNoteResult(const NodeIdentifier &p_parentId,
                                                  const NodeIdentifier &p_newNodeId) {
  // Model reloads from NotebookCoreService
  m_model->reloadNode(p_parentId);
}
```

### MVC Rules (MUST FOLLOW)

| Rule | Rationale |
|------|-----------|
| **Models MUST NOT** contain UI logic | Models are reusable across different views |
| **Views MUST NOT** modify data directly | Views only display and emit signals |
| **Controllers MUST NOT** inherit from QWidget | Controllers are testable without GUI |
| **All layers receive `ServiceLocator&`** | Enables dependency injection and testing |
| **Use signals/slots between layers** | Loose coupling between M, V, C |

### Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| ServiceLocator is NOT a singleton | Enables testing with mock services; explicit dependencies |
| Services wrap vxcore C API | Qt-friendly interface; encapsulates C interop |
| Controllers are QObject, not QWidget | Testable business logic without GUI dependencies |
| Widgets receive `ServiceLocator&` | Constructor injection; no global state |
| New files use `2` suffix | Coexist with legacy code during migration |
| `Buffer2` is a lightweight copyable handle (like `QModelIndex`) | Returned by `BufferService::openBuffer()`, delegates to `BufferCoreService`; NOT a `QObject`, not heap-allocated |
| `BufferService` privately inherits `BufferCoreService` | Hook-aware wrapper that fires `vnote.file.*` hooks around core operations |
| `NodeIdentifier` is a standalone value type | Identifies a node by `notebookId` + `relativePath`; used by `Buffer2`, controllers, and views |
| `ConfigMgr2` is the ONLY way to access typed config | Owns `MainConfig`/`SessionConfig` with properly merged defaults. NEVER construct a throwaway `MainConfig` from raw JSON — use `m_services.get<ConfigMgr2>()` instead. See `src/core/AGENTS.md` for details. |
| `ConfigMgr2::getFileFromConfigFolder()` for path resolution | Resolves relative config paths (e.g., `"web/markdown-viewer-template.html"`) against the app data directory. Do NOT use `ConfigCoreService::getDataPath()` + manual `QDir::filePath()`. |
| `PathExists()`/`IsDirectory()`/`IsRegularFile()` wrappers | NEVER pass raw `std::string` to `std::filesystem` — use these wrappers or `PathFromUtf8()` for non-ASCII path safety on Windows |

### Key Design Decisions (ViewArea2 Framework)

| Decision | Rationale |
|----------|-----------|
| `ViewAreaController` is the orchestrator | Handles open/close/split/move logic, fires hooks, uses WorkspaceCoreService |
| `ViewArea2` is a pure view | Owns QSplitter tree + ViewSplit2 instances, no business logic |
| `ViewSplit2` maps 1:1 to vxcore workspace | Each tab widget pane corresponds to one `WorkspaceCoreService` workspace |
| `ViewWindow2` receives `Buffer2` in constructor | Not attach/detach pattern; one window = one buffer for its lifetime |
| `ViewWindowFactory` maps file types to creators | Registry pattern; plugins register creators for new file types |
| Splitter orientation follows Vim convention | Left/Right split → `Qt::Horizontal`, Up/Down split → `Qt::Vertical` |
| Session layout stored as JSON in `SessionConfig` | Recursive splitter tree + workspace IDs for full layout persistence |
| Framework only — no concrete ViewWindow implementations | `MarkdownViewWindow2`, etc. will be added separately |

### Directory Structure (New Architecture)

```
src/
├── main.cpp                # Entry point with DI wiring
├── core/
│   ├── servicelocator.h    # DI container
│   ├── nodeidentifier.h    # Lightweight node ID (notebookId + relativePath)
│   ├── services/           # Service layer (wraps vxcore)
│   │   ├── configcoreservice.h/.cpp
│   │   ├── notebookcoreservice.h/.cpp
│   │   ├── searchcoreservice.h/.cpp
│   │   ├── filetypecoreservice.h/.cpp
│   │   ├── buffercoreservice.h/.cpp
│   │   ├── bufferservice.h/.cpp    # Hook-aware wrapper, returns Buffer2
│   │   ├── buffer2.h/.cpp          # Lightweight buffer handle (like QModelIndex)
│   │   ├── templateservice.h/.cpp
│   │   ├── workspacecoreservice.h/.cpp  # Workspace operations (split pane ↔ vxcore workspace)
│   │   └── hookmanager.h/.cpp
│   ├── hookcontext.h       # Hook callback context
│   ├── hooknames.h         # Hook name constants
│   ├── configmgr2.h/.cpp   # High-level config manager using DI
│   └── iconfigmgr.h        # Interface for config managers
├── gui/                    # GUI-aware services and utilities
│   ├── services/
│   │   ├── themeservice.h/.cpp         # GUI-aware theme management service
│   │   └── viewwindowfactory.h/.cpp    # Registry mapping file types to ViewWindow2 creators
│   └── utils/
│       ├── widgetutils.h/.cpp          # Widget utility helpers
│       ├── themeutils.h/.cpp           # Theme utility helpers
│       ├── imageutils.h/.cpp           # Image utility helpers
│       └── guiutils.h/.cpp             # General GUI utilities
├── models/                 # Qt Model/View models
│   ├── notebooknodemodel.h/.cpp       # QAbstractItemModel for node hierarchy
│   └── notebooknodeproxymodel.h/.cpp  # Proxy model for sorting/filtering
├── views/                  # Qt views and delegates
│   ├── notebooknodeview.h/.cpp        # QTreeView for nodes
│   ├── notebooknodedelegate.h/.cpp    # Item delegate for node rendering
│   ├── combinednodeexplorer.h/.cpp    # Composite MVC wiring widget
│   └── filenodedelegate.h/.cpp        # Item delegate for file list
├── controllers/            # Controllers (business logic mediators)
│   ├── notebooknodecontroller.h/.cpp  # Node operations controller
│   ├── newnotecontroller.h/.cpp       # New note dialog controller
│   ├── newfoldercontroller.h/.cpp     # New folder dialog controller
│   ├── newnotebookcontroller.h/.cpp   # New notebook dialog controller
│   ├── opennotebookcontroller.h/.cpp  # Open notebook flow controller
│   ├── managenotebookscontroller.h/.cpp
│   ├── importfoldercontroller.h/.cpp
│   ├── recyclebincontroller.h/.cpp
│   └── viewareacontroller.h/.cpp      # View area orchestrator (open/close/split/move)
├── widgets/                # UI widgets (views receiving ServiceLocator&)
│   ├── mainwindow.h        # Legacy main window
│   ├── mainwindow2.h/.cpp  # New main window shell
│   ├── notebookexplorer2.h/.cpp
│   ├── notebookselector2.h/.cpp
│   ├── toolbarhelper2.h/.cpp
│   ├── viewwindow2.h/.cpp  # Abstract base for file viewer windows
│   ├── viewsplit2.h/.cpp   # QTabWidget-based split pane (one vxcore workspace)
│   ├── viewarea2.h/.cpp    # Splitter tree view (manages ViewSplit2 layout)
│   └── dialogs/            # Dialog widgets (new architecture)
│       ├── newnotedialog2.h/.cpp
│       ├── newfolderdialog2.h/.cpp
│       ├── newnotebookdialog2.h/.cpp
│       ├── managenotebooksdialog2.h/.cpp
│       └── importfolderdialog2.h/.cpp
├── utils/
│   └── fileutils2.h/.cpp   # File utilities (new architecture)
└── ... (legacy code remains for reference)
```

---

## Code Style Guidelines

### Standards
- **C++14** standard
- **Qt 5/6** framework
- CMake with `CMAKE_AUTOMOC`, `CMAKE_AUTOUIC`, `CMAKE_AUTORCC` enabled

### Formatting
- 2-space indentation
- 100 character line limit
- Pointer alignment right: `int *ptr`, not `int* ptr`
- Use provided `.clang-format` (auto-applied via pre-commit hook)

### Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Classes | CamelCase | `ConfigMgr`, `MainWindow` |
| Methods | camelCase | `getInst()`, `initLoad()` |
| Parameters | `p_` prefix | `p_parent`, `p_config` |
| Members | `m_` prefix | `m_themeMgr`, `m_config` |
| Constants | `c_` prefix | `c_orgName`, `c_appName` |
| Getters | `get` prefix | `getThemeMgr()`, `getName()` |

### Include Order
```cpp
#include "ownheader.h"      // Own header first

#include <QDateTime>        // Qt includes
#include <QObject>

#include "localheader.h"    // Local includes
#include <core/configmgr.h>
#include <utils/utils.h>

using namespace vnotex;     // Namespace declaration in .cpp
```

### Header Guards
```cpp
#ifndef CLASSNAME_H
#define CLASSNAME_H
// ...
#endif // CLASSNAME_H
```

### Namespaces

VNote uses a single `vnotex` namespace. Services that wrap the vxcore C library use the `CoreService` suffix to distinguish them from higher-level wrapper services:

| Class Pattern | Purpose | Examples |
|---------------|---------|----------|
| `XXXCoreService` | Low-level services that wrap the vxcore C library (hold `VxCoreContextHandle`) | `ConfigCoreService`, `NotebookCoreService`, `BufferCoreService`, `SearchCoreService`, `FileTypeCoreService` |
| Other classes | Everything else: UI, controllers, models, hook-aware wrapper services | `BufferService` (hook wrapper), `HookManager`, `TemplateService`, `ConfigMgr2`, controllers, widgets |

**Rules:**
- `using namespace vnotex;` in `.cpp` files only, never in headers
- Forward declarations preferred in headers

---

## Common Patterns

### Noncopyable Pattern
```cpp
// src/core/noncopyable.h
class Noncopyable {
protected:
  Noncopyable() = default;
  virtual ~Noncopyable() = default;
  Noncopyable(const Noncopyable &) = delete;
  Noncopyable &operator=(const Noncopyable &) = delete;
};

// Usage: private inheritance
class MyClass : public QObject, private Noncopyable {
  // ...
};
```

### Deprecation Macro

Use `VNOTEX_DEPRECATED(msg)` from `src/core/global.h` to mark legacy classes, structs, and functions. It expands to `[[deprecated(msg)]]` on C++14+ (MSVC and GCC/Clang), or is a no-op on older compilers.

```cpp
#include <core/global.h>

// Deprecate a class — place between 'class' keyword and class name
class VNOTEX_DEPRECATED("Use MainWindow2 with ServiceLocator pattern instead") MainWindow
    : public QMainWindow {
  // ...
};

// Deprecate a struct
struct VNOTEX_DEPRECATED("Use FileOpenSettings instead") FileOpenParameters {
  // ...
};

// Deprecate a function
VNOTEX_DEPRECATED("Use newMethod() instead")
void oldMethod();
```

**Rules:**
- Always include a migration message explaining what to use instead
- For type deprecation, place the macro **between** the `class`/`struct` keyword and the type name
- Requires `#include <core/global.h>`

### Exception Handling
```cpp
// src/core/exception.h
class Exception : virtual public std::runtime_error {
public:
  enum class Type {
    InvalidPath, FailToCreateDir, FailToWriteFile,
    FailToReadFile, FailToRenameFile, /* ... */
  };

  [[noreturn]] static void throwOne(Type p_type, const QString &p_what) {
    qCritical() << typeToString(p_type) << p_what;
    throw Exception(p_type, p_what);
  }
};

// Usage
Exception::throwOne(Exception::Type::FailToReadFile, "Cannot read config");
```

### Signal/Slot Connections
```cpp
// Preferred: new Qt5 syntax
connect(m_taskMgr, &TaskMgr::taskOutputRequested,
        this, &VNoteX::showOutputRequested);

// With overloaded methods
connect(this, &VNoteX::openNodeRequested, m_bufferMgr,
        QOverload<Node *, const QSharedPointer<FileOpenParameters> &>::of(&BufferMgr::open));
```

### Memory Management
```cpp
// Use Qt smart pointers
QScopedPointer<MainConfig> m_config;
QSharedPointer<Task> task;

// QObject parent-child for automatic cleanup
m_themeMgr = new ThemeMgr(this);  // 'this' takes ownership
```

---

## Migration Guide

### Migrating a Widget to New Architecture

**Before (legacy):**
```cpp
// Uses global singletons
class OldWidget : public QWidget {
  void doSomething() {
    auto &config = ConfigMgr::getInst();
    auto &notebooks = VNoteX::getInst().getNotebookMgr();
  }
};
```

**After (new architecture):**
```cpp
// Receives dependencies via constructor
class NewWidget : public QWidget {
  Q_OBJECT
public:
  explicit NewWidget(ServiceLocator &p_services, QWidget *p_parent = nullptr);

private:
  ServiceLocator &m_services;

  void doSomething() {
    auto &config = m_services.get<ConfigCoreService>();
    auto &notebooks = m_services.get<NotebookCoreService>();
  }
};
```

### Migration Checklist

- [ ] Create new file with `2` suffix (e.g., `mywidget2.h`)
- [ ] Add `ServiceLocator &p_services` to constructor
- [ ] Store reference: `ServiceLocator &m_services`
- [ ] Replace `ConfigMgr::getInst()` → `m_services.get<ConfigCoreService>()`
- [ ] Replace `VNoteX::getInst().getNotebookMgr()` → `m_services.get<NotebookCoreService>()`
- [ ] Update parent widget to pass `ServiceLocator&`
- [ ] Add to CMakeLists.txt
- [ ] Write unit test with mock services

---

## Legacy Architecture (Reference Only)

The following patterns exist in legacy code. **Do NOT use for new code.**

### Legacy Singleton Pattern
```cpp
// LEGACY - Do not use in new code
class VNoteX : public QObject {
public:
  static VNoteX &getInst();  // Global singleton
};

// LEGACY usage
auto &themeMgr = VNoteX::getInst().getThemeMgr();
```

### Legacy ConfigMgr
```cpp
// LEGACY - Use ConfigCoreService via ServiceLocator instead
auto &config = ConfigMgr::getInst();
```

---

## Logging

Use Qt logging macros:
```cpp
qDebug() << "Debug message";
qInfo() << "Info message";
qWarning() << "Warning message";
qCritical() << "Critical error";
```

## Code Formatting

The pre-commit hook automatically formats staged C++ files using clang-format.

**Manual formatting:**
```bash
clang-format -i src/core/myfile.cpp
```

**Excluded from formatting:** `libs/` directory (third-party code)

---

## Module Documentation Index

Detailed knowledge for each module lives in its own AGENTS.md:

| Module | File | Description |
|--------|------|-------------|
| Core & Services | [src/core/AGENTS.md](src/core/AGENTS.md) | ServiceLocator, DI, Buffer2, hooks, adding services |
| Controllers | [src/controllers/AGENTS.md](src/controllers/AGENTS.md) | Controller patterns, MVC rules for controllers |
| Models | [src/models/AGENTS.md](src/models/AGENTS.md) | Qt Model/View data representations |
| Views | [src/views/AGENTS.md](src/views/AGENTS.md) | View conventions, delegate patterns |
| Widgets | [src/widgets/AGENTS.md](src/widgets/AGENTS.md) | Widget conventions, ViewArea2 framework |
| GUI Services | [src/gui/AGENTS.md](src/gui/AGENTS.md) | Theme, ViewWindowFactory, GUI utilities |
| Utilities | [src/utils/AGENTS.md](src/utils/AGENTS.md) | PathUtils, HtmlUtils, FileUtils2 reference |
| Testing | [tests/AGENTS.md](tests/AGENTS.md) | Test infrastructure, test mode, coverage |
| vxcore (submodule) | [libs/vxcore/AGENTS.md](libs/vxcore/AGENTS.md) | C library: notebook/config/search backend |
| vxcore Sync | [libs/vxcore/src/sync/AGENTS.md](libs/vxcore/src/sync/AGENTS.md) | Pluggable sync backend interface (ISyncBackend, SyncManager) |
| vtextedit (submodule) | [libs/vtextedit/AGENTS.md](libs/vtextedit/AGENTS.md) | Qt editor widget library |
