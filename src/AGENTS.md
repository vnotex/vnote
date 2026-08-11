# src/ — Architecture & Source-Wide Patterns

This file is inherited by every module under `src/`. It holds the full VNote architecture
(MVC layering, design-decision rationale, directory tree) and the Qt/C++ patterns that apply
to all source code regardless of module.

The normative MVC rules table and the repo-wide build/style rules live in the root
[AGENTS.md](../AGENTS.md).

Packaging and CI rules — including `src/Packaging.cmake`, the Windows 7 / Qt 5.15 variant and
the bundled OpenSSL gates — live in [.github/AGENTS.md](../.github/AGENTS.md). **An edit to
`src/Packaging.cmake` does not auto-load that file; open it explicitly.**

---

## Architecture

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

The normative rules table lives at
[root AGENTS.md § MVC Rules (MUST FOLLOW)](../AGENTS.md#mvc-rules-must-follow) — that is the
single source of truth, and the anchor `controllers/`, `models/`, `views/` and `widgets/` all
link to. Do not copy the table here.

### Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| ServiceLocator is NOT a singleton | Enables testing with mock services; explicit dependencies |
| Services wrap vxcore C API | Qt-friendly interface; encapsulates C interop |
| Controllers are QObject, not QWidget | Testable business logic without GUI dependencies |
| Widgets receive `ServiceLocator&` | Constructor injection; no global state |
| Some files carry a `2` suffix (`MainWindow2`, `Buffer2`, …) | Historical artifact of the now-complete migration off the legacy singleton architecture. The pre-migration counterparts have been removed; the suffix is retained on those existing classes only to avoid a churny rename. The migration is finished, so **new code uses the plain, unsuffixed name unless it genuinely conflicts with an existing type** (e.g. a brand-new `EncodingButton` gets no suffix). Never introduce a `3` suffix. |
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
| Concrete ViewWindows live alongside the framework | `MarkdownViewWindow2`, `TextViewWindow2`, `PdfViewWindow2`, `MindMapViewWindow2`, and `WidgetViewWindow2` are registered with `ViewWindowFactory` per file type |

### Directory Structure

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
│   │   ├── historyservice.h/.cpp   # Aggregate per-notebook history across notebooks
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
│   ├── mainwindow2.h/.cpp  # Main window shell
│   ├── notebookexplorer2.h/.cpp
│   ├── notebookselector2.h/.cpp
│   ├── toolbarhelper2.h/.cpp
│   ├── viewwindow2.h/.cpp  # Abstract base for file viewer windows
│   ├── markdownviewwindow2.h/.cpp  # Markdown editor/preview window
│   ├── textviewwindow2.h/.cpp      # Plain text editor window
│   ├── pdfviewwindow2.h/.cpp       # PDF viewer window
│   ├── mindmapviewwindow2.h/.cpp   # Mind map viewer window
│   ├── widgetviewwindow2.h/.cpp    # Generic widget-hosting window
│   ├── viewsplit2.h/.cpp   # QTabWidget-based split pane (one vxcore workspace)
│   ├── viewarea2.h/.cpp    # Splitter tree view (manages ViewSplit2 layout)
│   └── dialogs/            # Dialog widgets
│       ├── newnotedialog2.h/.cpp
│       ├── newfolderdialog2.h/.cpp
│       ├── newnotebookdialog2.h/.cpp
│       ├── managenotebooksdialog2.h/.cpp
│       └── importfolderdialog2.h/.cpp
├── net/
│   └── networkutils.h/.cpp # core_net: Qt Core/Network-only HTTP helpers
│                           # (vnotex::NetworkUtils / NetworkReply / NetworkAccess)
├── utils/
│   └── fileutils2.h/.cpp   # File utilities
└── ...
```

---

## Source-Wide Qt Patterns

### Memory Management
```cpp
// Use Qt smart pointers
QScopedPointer<MainConfig> m_config;
QSharedPointer<Task> task;

// QObject parent-child for automatic cleanup
m_themeMgr = new ThemeMgr(this);  // 'this' takes ownership
```

### Queued-Connection Metatype Names (Qt 5 resolves them by NAME)

A type used as a **queued-connection signal parameter** or in **`Q_ARG`** must be registered
under the exact name moc recorded / `Q_ARG` stringified. For a type declared inside
`namespace vnotex` and spelled **unqualified** in the signal, that is the UNQUALIFIED name —
while `Q_DECLARE_METATYPE(vnotex::X)` registers `"vnotex::X"`.

Qt 5's `queued_activate()` calls `queuedConnectionTypes()` on moc's parameter-name strings and
does `QMetaType::type("X")`. If only the qualified alias exists the lookup returns 0, Qt prints
`QObject::connect: Cannot queue arguments of type 'X'`, and **drops the call**. Qt 6 obtains the
`QMetaType` via `QMetaMethod::parameterMetaType()` from moc's generated metatype data and never
does the name lookup, so this defect is invisible on Qt 6 and fatal on the Qt 5 /
`win64-windows7` variant.

Fix shape: register the unqualified **alias** alongside the existing registration
(`qRegisterMetaType<X>("X");`). Registering the same type under a second name is an alias, not
a duplicate. Precedents: `qRegisterMetaType<BufferState>("BufferState")`
(`src/widgets/viewwindow2.cpp:87`) and `qRegisterMetaType<NotificationMessage>("NotificationMessage")`
(`src/core/services/notificationservice.cpp:8`).

Known name-resolved queued sites:

| Site | Required alias |
|---|---|
| `SearchWorker::finished`, `SearchWorker::batch` (`src/core/services/searchservice.cpp`) | `"SearchResult"` |
| `SearchWorker::failed` (`src/core/services/searchservice.cpp`) | `"Error"` |
| `Q_ARG(ImageHostWorkItem, ...)` (`src/core/services/imagehostservice.cpp`) | `"ImageHostWorkItem"` |
| `ImageHostWorker::uploadCompleted`, `::removeCompleted` (`src/core/services/imagehostworker.h`) | `"ImageHostAsyncResult"` |

Coverage: `testQueuedMetatypeNamesAreRegistered` in `tests/core/test_searchservice.cpp` and
`tests/core/test_imagehostservice.cpp`.

**This is NOT a blanket requirement for every `Q_DECLARE_METATYPE(vnotex::X)`.** The required
runtime name is whatever moc recorded or `Q_ARG` stringified: `UpdateService::checkFinished`
spells its parameter `vnotex::UpdateInfo` explicitly (`src/core/services/updateservice.h`), and
`NodeIdentifier` signals are GUI-thread-local and never queue. Do not sweep them in.

---

## Related Modules

| Module | File | Read this when |
|--------|------|----------------|
| Core & Services | [core/AGENTS.md](core/AGENTS.md) | ServiceLocator, DI, Buffer2, hooks, config, adding a service |
| Services (deep) | [core/services/AGENTS.md](core/services/AGENTS.md) | Sync, save/search threading, update check, service internals |
| Controllers | [controllers/AGENTS.md](controllers/AGENTS.md) | Adding or changing a controller |
| Models | [models/AGENTS.md](models/AGENTS.md) | Qt Model/View data representations |
| Views | [views/AGENTS.md](views/AGENTS.md) | View conventions, delegate patterns |
| Widgets | [widgets/AGENTS.md](widgets/AGENTS.md) | Widget conventions, ViewArea2 framework, styling |
| GUI Services | [gui/AGENTS.md](gui/AGENTS.md) | Theme, ViewWindowFactory, GUI utilities |
| Utilities | [utils/AGENTS.md](utils/AGENTS.md) | PathUtils, HtmlUtils, FileUtils2 |
| CI & Packaging | [../.github/AGENTS.md](../.github/AGENTS.md) | `src/Packaging.cmake`, Windows 7 / Qt 5.15, OpenSSL |
