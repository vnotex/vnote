# VNote Agent Development Guide

Root routing document: prerequisites, build, repo-wide rules, and an index of the module docs.
Module-specific detail lives in the child `AGENTS.md` that owns the code — see
[Module Documentation Index](#module-documentation-index).

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
Never repair a stale build dir in place.

### Building the Tests
Tests are **not** built by default (`VNOTE_BUILD_TESTS=OFF`), so a plain `cmake ..` produces the
app only. CI enables them explicitly. To build them locally:
```bash
cmake .. -DVNOTE_BUILD_TESTS=ON
cmake --build .
```

Testing (two suites, two build dirs): [tests/AGENTS.md](tests/AGENTS.md).

---

## Submodule Push Discipline (CRITICAL — read before every push)

VNote pins git submodules (`libs/vxcore`, `libs/vtextedit`, `libs/QHotkey`, `libs/qwindowkit`)
to specific commits. **CI clones submodules from their own remotes**, so a commit that exists
only locally fails every CI job at "Init Submodules" (`upload-pack: not our ref <sha>`) before
any build runs.

### Rule: ALWAYS push the submodule FIRST, then the parent repo.

```bash
cd libs/vxcore
git push origin HEAD:main      # or the appropriate branch
cd ../..
git push                       # only now may the gitlink be pushed
```
To push both at once with verification, use `git push --recurse-submodules=on-demand`, or enforce
it once per clone:

```bash
git config push.recurseSubmodules check   # aborts the parent push if a submodule commit is unpushed
```

### Before pushing, verify no submodule commit is stranded

```bash
# For each submodule, confirm local HEAD is not ahead of its remote:
git submodule foreach 'git status -sb'
# A line like "## main...origin/main [ahead 1]" means an UNPUSHED submodule commit — push it before pushing vnote.

# Confirm the pinned SHA exists on the submodule remote:
cd libs/vxcore && git branch -r --contains $(git rev-parse HEAD) && cd ../..
# Empty output = the commit is NOT on any remote branch yet. DO NOT push vnote until it is.
```

If CI is already failing with "not our ref", the fix is to push the missing submodule commit (do
**not** roll back the parent pointer if newer parent commits depend on the new submodule API).

### cmark is vendored twice — bump both pins together

The same cmark fork (`https://github.com/vnotex/cmark.git`) is pinned by two different submodules:

| Parent submodule | Nested cmark path |
|---|---|
| `libs/vtextedit` | `libs/cmark` |
| `libs/vxcore` | `third_party/cmark` |

Only **one** is ever compiled in: `libs/CMakeLists.txt` adds `vtextedit` first, which defines the
`cmark` target unconditionally, so vxcore's `if(NOT TARGET cmark)` guard skips its own copy.
Diverged pins silently build vxcore against an untested cmark, with no error or warning.
**Bump both submodules to the same cmark commit in a single change.** [`tests/utils/test_cmark_pin_drift.cpp`](tests/utils/test_cmark_pin_drift.cpp) fails the
build when the two recorded pins differ.

---

## Architecture Overview

VNote uses a **clean architecture** with **Model-View-Controller (MVC)** pattern and dependency
injection. Models hold data, Views display it, Controllers handle logic, Services own domain
operations, and every layer receives a `ServiceLocator&` — there are no singletons.

| Layer | Location | Responsibility | Example |
|-------|----------|----------------|---------|
| **Model** | `src/models/` | Data representation, Qt Model/View integration | `NotebookNodeModel` exposes node hierarchy via `QAbstractItemModel` |
| **View** | `src/views/` | Display data, capture user input, emit signals | `NotebookNodeView` renders tree, emits `nodeActivated` signal |
| **Controller** | `src/controllers/` | Handle actions, orchestrate Model/View, business logic | `NotebookNodeController` handles new/delete/rename operations |
| **Service** | `src/core/services/` | Domain operations, data access via vxcore | `NotebookCoreService` wraps vxcore C API for notebook CRUD |

### MVC Rules (MUST FOLLOW)

| Rule | Rationale |
|------|-----------|
| **Models MUST NOT** contain UI logic | Models are reusable across different views |
| **Views MUST NOT** modify data directly | Views only display and emit signals |
| **Controllers MUST NOT** inherit from QWidget | Controllers are testable without GUI |
| **All layers receive `ServiceLocator&`** | Enables dependency injection and testing |
| **Use signals/slots between layers** | Loose coupling between M, V, C |

Full diagram, directory tree, design-decision rationale (including the ViewArea2 framework), and
source-wide Qt patterns: see [src/AGENTS.md](src/AGENTS.md).

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

### No Hardcoded Colors (enforced)

**Never write a literal color into a `setStyleSheet()` call.** VNote ships 12
themes, 6 of them dark; a hardcoded `#RRGGBB`, `rgb()/rgba()` literal, or CSS
color name is correct only in whichever theme its author was running, and it
cannot follow a runtime theme switch.

`tests/utils/test_hardcoded_color_drift.cpp` is a grep gate over `src/` that
fails the build on any **stylesheet string** literal containing both a CSS color
property and a literal color value (colors used as *data*, and `QColor` painted
in a `paintEvent`, are out of scope).

Use `InlineBanner`, the `SeverityText` / `MutedText` dynamic properties, a rule
in each theme's `interface.qss`, or `ThemeService::paletteColor()`. Do **not**
use `setEnabled(false)` to mute text. See
[src/widgets/AGENTS.md § No Hardcoded Colors in C++](src/widgets/AGENTS.md#no-hardcoded-colors-in-c)
for the decision table and the escape hatch.

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

### Signal/Slot Connections
```cpp
// Preferred: new Qt5 syntax
connect(m_taskMgr, &TaskMgr::taskOutputRequested,
        this, &VNoteX::showOutputRequested);

// With overloaded methods
connect(this, &VNoteX::openNodeRequested, m_bufferMgr,
        QOverload<Node *, const QSharedPointer<FileOpenParameters> &>::of(&BufferMgr::open));
```

Memory management, queued-connection metatype naming (a Qt 5 correctness rule), and the rest of
the source-wide patterns: [src/AGENTS.md § Source-Wide Qt Patterns](src/AGENTS.md#source-wide-qt-patterns).
Noncopyable, `VNOTEX_DEPRECATED` and exception handling: [src/core/AGENTS.md](src/core/AGENTS.md#core-c-facilities).

---

## Sync State Model

Notebook sync has 8 reachable states (S0-S7), defined by the tuple of on-disk JSON sync fields,
PAT presence in the OS keychain, and runtime registration in vxcore's `states_` map. **S5 is the
only "ready" state**; S1-S4 and S6 are partial/inconsistent, S0 is cleanly disabled, S7 is
in-flight. Every controller, widget, and service that touches sync must reason in these terms.

Full predicate table, recovery paths, reconcile semantics, disable cleanup, the S6 startup sweep,
and the Qt-side scheduling shape:
[src/core/services/AGENTS.md § Sync State Model](src/core/services/AGENTS.md#sync-state-model).
vxcore-side threading contract:
[libs/vxcore/src/sync/AGENTS.md](libs/vxcore/src/sync/AGENTS.md).
---

## Save Path Threading Contract

Buffer saves run on a worker via `BufferSaveQueue`; save and git-stage/commit work on the SAME
notebook are serialized by the per-notebook `NotebookIoGate` async mutex.

> **Forbidden Patterns (post-T7):**
> - Calling `vxcore_buffer_save` directly from the UI thread. Use [`BufferSaveQueue::enqueue`](src/core/services/buffersavequeue.h) instead.
> - Touching a notebook's working tree (save, stage, commit, checkout) without holding `NotebookIoGate::ScopedLock(notebookId)`.

Full rationale and the two-phase sync gate:
[src/core/services/AGENTS.md § Save Path Threading Contract](src/core/services/AGENTS.md#save-path-threading-contract).

---

## Search Threading Contract

Content search in vxcore owns NO thread pool: it enqueues one work item per file-chunk onto the
`"vxcore.search"` work queue, and the CALLER owns the drain policy (VNote's `SearchService` runs
the drain pool; the initiating thread help-drains, which is the single-threaded correctness
floor).

Full contract: [src/core/services/AGENTS.md § Search Threading Contract](src/core/services/AGENTS.md#search-threading-contract).

---

## Update Check

VNote checks a forge for a newer release and, when one exists, tells the user and offers the
**release page**. That is the whole feature.

> **VNote never modifies its own install directory, and never downloads anything.** There is
> no lease file, no staging tree, no journal, no swap, no restart-to-apply, no downloader,
> and nothing is ever extracted or executed. The only thing the check writes is the
> `lastUpdateCheckTime` / `skippedUpdateVersion` config values. This invariant is what makes
> a read-only install location (`/usr/bin`, Program Files, a read-only DMG) launchable
> (issue #2728) — do not reintroduce install-tree mutation, or a downloader, without
> replacing this section.

Repo-wide forbidden patterns (they constrain `.github/`, packaging, controllers and widgets
alike, none of which load the service doc):

- **Never** download, extract, execute or install a release artifact.
- **Never** write outside the configuration directory as part of an update check.
- **Never** read `assets[]`; the release page is the only affordance.
- **Never** give `UpdateService` a `ConfigMgr2` dependency — add the policy to the controller.

Release CI still publishes manifests, minisign signatures and delta ZIPs (see
`docs/update-signing.md`); they are the interface for a future *external* updater, not this
client. Endpoints, the GitHub/Gitee source table, redirect and allowlist rules, and threading:
[src/core/services/AGENTS.md § Update Check](src/core/services/AGENTS.md#update-check).

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

## Shared JSON Keys (SSOT)

Cross-boundary JSON keys (vxcore↔Qt) live in `<vxcore/notebook_json_keys.h>`.
See [`libs/vxcore/AGENTS.md` § JSON Conventions](libs/vxcore/AGENTS.md#json-conventions)
for the SSOT contract and the `test_json_key_drift` regression gate.

---

## Module Documentation Index

Detailed knowledge for each module lives in its own AGENTS.md.

**Where to write new documentation:** default to the child `AGENTS.md` that owns the code (create
one for the directory if it does not exist yet). The root doc is injected into *every* agent turn,
so anything added here costs context on turns that will never need it. Add to root only when the
knowledge is genuinely repo-wide — i.e. it constrains callers who will never load the owning
module's doc (as the MVC rules and the update-install invariant do), or it is a build/setup/style
rule that applies everywhere. Even then, keep root to a short normative summary plus a link, and
put the full detail in the module doc.

| Module | File | Read this when |
|--------|------|----------------|
| Source overview | [src/AGENTS.md](src/AGENTS.md) | You need the architecture diagram, directory tree, design-decision rationale, or a source-wide Qt pattern (memory, queued metatypes) |
| Core & Services | [src/core/AGENTS.md](src/core/AGENTS.md) | ServiceLocator, DI, Buffer2, hooks, config, themes, adding a service |
| Services (deep) | [src/core/services/AGENTS.md](src/core/services/AGENTS.md) | Sync state model, save/search threading, update check, notifications |
| Controllers | [src/controllers/AGENTS.md](src/controllers/AGENTS.md) | Adding or changing a controller; MVC rules for controllers |
| Models | [src/models/AGENTS.md](src/models/AGENTS.md) | Qt Model/View data representations |
| Views | [src/views/AGENTS.md](src/views/AGENTS.md) | View conventions, delegate patterns |
| Widgets | [src/widgets/AGENTS.md](src/widgets/AGENTS.md) | Widget conventions, ViewArea2 framework, styling, construction pattern |
| GUI Services | [src/gui/AGENTS.md](src/gui/AGENTS.md) | Theme, ViewWindowFactory, GUI utilities |
| Utilities | [src/utils/AGENTS.md](src/utils/AGENTS.md) | PathUtils, HtmlUtils, FileUtils2 reference |
| pdf.js assets | [src/data/extra/web/pdf.js/AGENTS.md](src/data/extra/web/pdf.js/AGENTS.md) | Touching the vendored PDF viewer, or triaging "the PDF viewer is broken on the Qt 5.15 / Windows 7 build" |
| Testing | [tests/AGENTS.md](tests/AGENTS.md) | Writing/running tests in either suite, test mode, fixtures, coverage |
| CI & Packaging | [.github/AGENTS.md](.github/AGENTS.md) | Workflows, `src/Packaging.cmake`, Windows 7 / Qt 5.15 variant, bundled OpenSSL |
| vxcore (submodule) | [libs/vxcore/AGENTS.md](libs/vxcore/AGENTS.md) | C library: notebook/config/search backend |
| vxcore Sync | [libs/vxcore/src/sync/AGENTS.md](libs/vxcore/src/sync/AGENTS.md) | Pluggable sync backend interface (ISyncBackend, SyncManager) |
| vtextedit (submodule) | [libs/vtextedit/AGENTS.md](libs/vtextedit/AGENTS.md) | Qt editor widget library |
