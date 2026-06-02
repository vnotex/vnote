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

## Testing

VNote has tests in **two separate locations** with different registration helpers and build configurations. Pick the right one or your test will silently never run.

### Parent VNote tests (`tests/`)

Built automatically with the parent project into `build-debug/tests/<category>/`. Registered via the `add_qt_test()` helper.

**Add a new test:**
1. Create `tests/<category>/test_yourthing.cpp` (categories: `controllers/`, `core/`, `gui/`, `integration/`, `models/`, `utils/`, `widgets/`).
2. Register in the same directory's `CMakeLists.txt`:
   ```cmake
   add_qt_test(test_yourthing
     SOURCES
       test_yourthing.cpp
       ${CMAKE_SOURCE_DIR}/src/path/to/code_under_test.cpp
     LINKS
       core_services
       vxcore
     GUILESS  # omit if test needs QApplication / widgets
   )
   ```
3. Reconfigure from repo root if `tests/CMakeLists.txt` itself changed: `cmake -B build-debug`.

**Build + run:**
```powershell
cmake --build build-debug --config Debug --target test_yourthing
ctest --test-dir build-debug -C Debug -R "^test_yourthing$" --output-on-failure
```

### vxcore submodule tests (`libs/vxcore/tests/`)

**Separate build dir required** — the parent `build-debug/` does NOT compile vxcore tests. Use a dedicated build dir configured with `-DVXCORE_BUILD_TESTS=ON`. Each test is a standalone executable (file basename = test target name) registered via `add_vxcore_test()`.

**Add a new test:**
1. Create `libs/vxcore/tests/test_yourthing.cpp` with its own `main()`. Subtests are functions that return `int` (0 = pass); call them via `RUN_TEST(...)` from `test_utils.h`. Always start `main()` with `vxcore_set_test_mode(1)` so the test uses `%TEMP%\vxcore_test*` instead of real AppData.
2. Register in `libs/vxcore/tests/CMakeLists.txt`:
   ```cmake
   add_vxcore_test(test_yourthing)
   ```
   The helper auto-links `vxcore` and adds `${CMAKE_SOURCE_DIR}/src` + `third_party` include dirs.
3. Test executables CANNOT call vxcore-internal symbols that lack `VXCORE_API` — they live outside the DLL. If you need an internal helper (e.g., `GetCurrentTimestampMillis()`), re-derive it locally in the test file rather than exporting it.

**Configure (once per machine, or after CMake version upgrade):**
```powershell
cmake -S libs/vxcore -B libs/vxcore/build_test -G "Visual Studio 17 2022" -A x64 -DVXCORE_BUILD_TESTS=ON
```

**Build + run:**
```powershell
cmake --build libs/vxcore/build_test --config Debug --target test_yourthing
ctest --test-dir libs/vxcore/build_test -C Debug -R "^test_yourthing$" --output-on-failure
```

### Universal rules

- **Anchor the ctest regex** with `^...$` to avoid false matches (e.g., `-R "^test_sync$"` won't sweep in `test_session_persistence`).
- After a CMake version upgrade, stale build dirs fail to reconfigure with errors like `CMakeSystem.cmake.in does not exist`. Delete the offending build dir and re-run the configure command from scratch — do NOT try to repair in place.
- After modifying a touched module, run the FULL test target for that module (not just your new subtest) to catch regressions; vxcore tests print each subtest name to stdout so you can verify the new one ran.

### Running execs that depend on VTextEdit.dll (CRITICAL — avoid false-positive smoke tests)

`build-debug/src/vnote.exe` and most `build-debug/tests/<category>/test_*.exe` execs link against `VTextEdit.dll` (built into `build-debug/libs/vtextedit/src/`) plus the Qt 6 runtime DLLs. Neither is automatically copied next to `vnote.exe` in this development build (only test-exec dirs get VTextEdit.dll copied via CMake target propagation). Launching `vnote.exe` without setting PATH first causes the Windows loader to pop a "VTextEdit.dll was not found" modal dialog BEFORE the process reaches `WinMain`.

**This is a verification trap**: when the loader dialog blocks the process, the OS reports the process as alive (it has a PID, is technically running), so naive checks like `Start-Process … -PassThru` + `Sleep` + `HasExited` return `$false` → you falsely conclude the binary started. The process is actually frozen in pre-WinMain limbo waiting for someone to click the dialog. `Stop-Process -Force` afterwards silently dismisses the dialog and the lie is preserved.

**Correct smoke-test pattern (PowerShell):**

```powershell
# 1. Prepend Qt bin + VTextEdit dir to PATH so the loader resolves all DLLs.
$env:PATH = "C:/Qt/6.9.3/msvc2022_64/bin;" +
            "$PWD/build-debug/libs/vtextedit/src;" +
            $env:PATH

# 2. Launch vnote.exe.
$proc = Start-Process -FilePath "build-debug/src/vnote.exe" -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 5

# 3. Verify the process actually loaded VTextEdit.dll — NOT just HasExited.
#    If the loader dialog blocked the process, $proc.Modules will throw or be empty.
$loaded = $false
try {
  $proc.Refresh()
  $loaded = ($proc.Modules | Where-Object { $_.ModuleName -ieq "VTextEdit.dll" }).Count -gt 0
} catch {}
if ($proc.HasExited) {
  Write-Output "FAIL: vnote.exe exited with code $($proc.ExitCode) within 5s"
} elseif (-not $loaded) {
  Write-Output "FAIL: vnote.exe alive but VTextEdit.dll not loaded — likely a loader dialog"
} else {
  Write-Output "PASS: vnote.exe alive with VTextEdit.dll loaded after 5s"
}
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
```

The same PATH prepend is required before running parent `test_*.exe` directly (per `tests/AGENTS.md`). `ctest --test-dir build-debug` inherits the caller's PATH, so set it once at the start of the session.

vxcore submodule tests (`libs/vxcore/build_test/bin/Debug/test_*.exe`) do NOT depend on Qt or VTextEdit and need no PATH setup.

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

## Sync State Model

Threading rules: see `libs/vxcore/src/sync/AGENTS.md` § Threading & Callback Contract.
Qt-side dispatch (single queue via `SyncWorkQueueManager` + `SyncOps`, coalescing, cancellation, auto-sync routing through `EventBridge::syncShouldRun`): see `src/core/services/AGENTS.md` § SyncService. vxcore's internal debounce remains as a hint for non-Qt embedders; Qt no longer consults it as a gate.

Notebook sync has 8 reachable states (S0-S7). Every controller, widget, and service that touches sync must reason in terms of these states. The state is the tuple of: on-disk JSON sync fields, PAT presence in the OS keychain, and runtime registration in vxcore's `states_` map.

### Canonical State Predicates

| State | syncEnabled (JSON) | syncBackend (JSON) | syncRemoteUrl (JSON) | PAT in keychain | states_ entry |
|---|---|---|---|---|---|
| S0 | false / absent | absent | absent | absent | absent |
| S1 | true | "git" | empty | maybe | absent |
| S2 | true | "git" | set | **absent** | absent |
| S3 | true | empty | maybe | maybe | absent |
| S4 | true | "git" | set | present | **absent** |
| S5 | true | "git" | set | present | present |
| S6 | false | absent | absent | **present** | absent |
| S7 | true | "git" | set | present | present + active sync |

S5 is the only "ready" state. S1-S4 and S6 are partial/inconsistent; S0 is cleanly disabled; S7 is in-flight.

F3.5 in-flight sub-states (fetching/resolving/pushing) are NOT modeled as separate SyncState values, they remain runtime properties exposed by SyncService progress signals while the notebook is in S7.

### Recovery Paths: bootstrapApply vs applyChanges

| Path | Use when | Behavior |
|---|---|---|
| `NotebookSyncInfoController::bootstrapApply(url, pat)` | Notebook is in S1/S2/S3/S4 (any partial state). Atomic enable for an existing notebook. | Calls `SyncService::enableSyncForNotebook` directly; on success persists `syncRemoteUrl` and triggers initial sync; on failure keeps notebook in current state (NO delete, unlike `NewNotebookController::bootstrapSync`). |
| `NotebookSyncInfoController::applyChanges(url, pat)` | Notebook is in S5 (registered). PAT refresh or URL change. | PAT-only update routes through `SyncService::updateCredentials`. URL change triggers `confirmUrlChangeRequested` signal and, on confirm, runs atomic disable+wipe `vx_notebook/vx_sync/`+re-enable. |

The dialog (`NotebookSyncInfoDialog2`) auto-routes to `bootstrapApply` when `m_bootstrapMode == true` OR when `SyncService::isSyncRegistered(id) == false`. This is defense in depth: even when a caller bypasses the bootstrap entry point, partial-state notebooks still get the atomic path.

### Reconcile Semantics

`SyncService::reconcileSyncForNotebook` is called by `MainWindowAfterStart` and on notebook open to lift S4 notebooks (disk-complete, runtime-absent) into S5.

Key invariants (`src/core/services/syncservice.cpp:858-970`):
- `m_reconcileAttempted.insert(id)` happens **after** all precondition checks pass (line 893), not before. The disk-enabled check (line 869), idempotence guard (line 875), and complete-config check (line 884) all run first; any of them returning early leaves the attempted set untouched. Precondition failures therefore do NOT block future retries. Concrete consequence: a notebook in S1/S3 (enabled but no backend/url, or no backend) hits the `incomplete config` branch at line 885, emits `reconcileFinished(VXCORE_ERR_INVALID_PARAM)`, and is NOT marked attempted. When the user later supplies the missing URL via `bootstrapApply` and the notebook reaches S4, the very next reconcile trigger (notebook open or app start) will pass the precondition check and proceed.
- `m_reconcileAttempted.remove(id)` fires on transient PAT fetch failure (line 945) so the next reconcile call retries. The same key is re-cleared by `updateCredentials` (line 969) before manually re-driving reconcile, so a user re-entering a fresh PAT never hits the "already attempted" guard.
- No remove on success (notebook is registered; no retry needed).
- Idempotence check at line 875 prevents duplicate in-flight reconciles when `MainWindowAfterStart` and `NotebookAfterOpen` race.

### bootstrapAndPersist Rollback × Reconcile

`SyncService::bootstrapAndPersist` (`syncservice.cpp:409-508`) is the atomic enable+persist path used by `NewNotebookController` (W13.4, F1.6). On persist failure AFTER vxcore enable already succeeded, it issues a rollback by calling `disableSyncForNotebook`, which per "Disable Cleanup" below clears the three flat sync JSON keys then deletes the keychain entry.

Interaction with reconcile:
- **Rollback succeeds** (the common case): the notebook returns to clean S0. The disk-enabled check at `reconcileSyncForNotebook` line 869 fails immediately, the function early-returns, `m_reconcileAttempted` is never touched, and reconcile is correctly a no-op. The notebook needs a fresh user-initiated bootstrap, not silent re-registration. This is the intended recovery story.
- **Rollback fails** (rare; both persist AND disable failed, logged at `qCritical` line 497): the notebook is left in a partial state. If JSON still has all three keys set (vxcore enable succeeded, JSON write succeeded for some keys, then disable failed leaving keys intact), the notebook is effectively in S4. On the next reconcile trigger, all precondition checks pass, `m_reconcileAttempted` gets set, and reconcile will attempt to register the notebook. This is the expected recovery path for that edge case; the loud `qCritical` log gives operators a chance to investigate.

The key property: rollback NEVER causes reconcile to silently resurrect a notebook the user wanted disabled. Either rollback succeeded (so disk is clean and reconcile bails) or rollback failed (so disk truthfully says "enabled" and reconcile correctly tries to complete the job).

### Disable Cleanup

`SyncService::disableSyncForNotebook` on `VXCORE_OK` clears all three flat sync keys (`syncEnabled`, `syncBackend`, `syncRemoteUrl`) from notebook JSON BEFORE deleting the keychain entry (`src/core/services/syncservice.cpp:246-290`). On failure, JSON is preserved for retry. This closes the "resurrection trap" where a disabled notebook would reappear as S6 (orphan PAT) or S1 (orphan disk fields) on next app start.

For the full table of all five credential cleanup sites (bootstrap rollback, `bootstrapAndPersist` rollback, notebook removal, sync disable, S6 startup sweep) and the "when fires / when does NOT fire" matrix, see [src/core/services/AGENTS.md § Credential Cleanup Invariants](src/core/services/AGENTS.md#credential-cleanup-invariants).

### Startup S6 Sweep

`SyncService::onMainWindowAfterStart` (`syncservice.cpp:828-854`) sweeps S6 orphans before reconciling. For each notebook it iterates, if `!isSyncEnabled(id) && m_credentialsStore->hasCredentials(id)` (the S6 predicate: disk says disabled but a PAT is still in the keychain), it calls `m_credentialsStore->deleteCredentials(id)` to drop the orphan PAT. This handles the scenario where a previous session's disable succeeded inside vxcore but the app crashed (or was killed) before the keychain delete completed, leaving an orphan PAT that the new "disable clears JSON then keychain" ordering would otherwise not catch on its own.

The sweep runs BEFORE `reconcileSyncForNotebook(nbId)` on each notebook, so by the time reconcile examines the notebook the keychain state is consistent with disk truth.

### Re-enable UI Affordance

S0 notebooks expose a re-enable surface via the same Sync button and Sync Info menu used for S5 (`src/widgets/notebookexplorer2.cpp:1512-1635`). For S0:
- Button label: "Enable Sync" (distinct from "Sync Now" for S5)
- On click: opens `NotebookSyncInfoDialog2` with `setBootstrapMode(true)` and all fields empty
- Sync Info menu item enabled regardless of `syncEnabled` (dialog opens in bootstrap mode with disable button hidden)

Without this affordance, users who disable sync cannot re-enable without recreating the notebook.

### Sync Architecture Layers

VNote consumes vxcore as an embedded library following the contract documented in `libs/vxcore/AGENTS.md` § Library Integration Contract. Vxcore emits facts (events, dirty marks); VNote owns sync scheduling policy via `SyncService` + `SyncWorkQueueManager` (see `src/core/services/AGENTS.md` § SyncService). Vxcore must NOT contain Qt-side concerns (no `QTimer`, no `QObject`, no scheduling policy); VNote must NOT bypass the contract by reaching into vxcore internals (no direct backend calls, no touching libgit2, no `states_` mutation). The 4-layer ownership table lives in the vxcore doc to avoid duplication.

**Qt-side scheduling shape (post May 2026 audit).** `SyncService::onSyncShouldRun` enqueues immediately via `SyncWorkQueueManager` with `coalesceKey="trigger"`; concurrent triggers per notebook collapse into one in-flight sync. There is intentionally NO Qt-side debounce timer today — vxcore's `SyncConfig::interval_seconds` is a hint, not a schedule, and the coalesce key is sufficient to dedupe bursts. A future enhancement may introduce an optional debounce window in `SyncService` (seeded from `interval_seconds`, clamped to a small range) but it stays one layer above `SyncWorkQueueManager` so queue semantics are unchanged.

---

## Save Path Threading Contract

The `Buffer2` / [`BufferService`](src/core/services/bufferservice.h) auto-save path used to call `vxcore_buffer_save` inline on the UI thread, so any slow filesystem operation (large file flush, virus scanner, network drive, antivirus quarantine) froze the editor. That synchronous call now runs on a worker via [`BufferSaveQueue`](src/core/services/buffersavequeue.h). The UI thread's job is reduced to: snapshot the current content plus a monotonically increasing revision, call `BufferSaveQueue::enqueue(...)`, and return. No disk I/O on the UI thread.

Save work and any git-stage / git-commit work on the SAME notebook are serialized by [`NotebookIoGate`](src/core/services/notebookiogate.h), a per-notebook async mutex. Both `BufferSaveQueue` workers and [`SyncOps::triggerSync`](src/core/services/syncops.cpp) acquire `NotebookIoGate::ScopedLock(notebookId)` around their notebook-touching critical section. This prevents a sync round-trip from reading a half-written file, and it prevents a save from racing into the working tree mid-commit.

vxcore's `mark_dirty` → `MaybeEnqueueSync` → `Emit("sync.should_run")` chain remains synchronous on the caller thread BY DESIGN. In steady state it is microseconds, and pushing it onto another thread would buy nothing while costing event-ordering guarantees. **This contract does NOT change vxcore.** The threading discipline is consumer-side only: keep `vxcore_buffer_save` off the UI thread, and the `mark_dirty` tail it triggers stays off the UI thread for free.

The legacy `Buffer` (1-second timer in `src/core/buffer/buffer.cpp`) bypasses vxcore entirely and does not participate in this contract. It is out of scope here and will be retired alongside the rest of the legacy code path.

> **Forbidden Patterns (post-T7):**
> - Calling `vxcore_buffer_save` directly from the UI thread. Use [`BufferSaveQueue::enqueue`](src/core/services/buffersavequeue.h) instead.
> - Touching a notebook's working tree (save, stage, commit, checkout) without holding `NotebookIoGate::ScopedLock(notebookId)`.

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
