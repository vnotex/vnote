# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

VNote is a Qt-based, cross-platform (Windows/Linux/macOS) note-taking application focused on Markdown. It uses C++14, Qt 6, and CMake 3.20+. The project is undergoing a major architectural migration from singletons to dependency injection via `ServiceLocator`.

## Build Commands

```bash
# Setup (first time after clone)
bash scripts/init.sh          # Linux/macOS
scripts\init.cmd              # Windows

# Configure + build (Release)
mkdir build && cd build
cmake .. -GNinja              # Windows with MSVC: run from VS Developer Command Prompt
cmake --build . --config Release

# Debug build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug

# Clean rebuild
rm -rf build && mkdir build && cd build && cmake .. && cmake --build .
```

## Testing

Tests use Qt Test framework. **Always call `vxcore_set_test_mode(1)` before `vxcore_context_create()` in tests** to prevent corrupting real user data.

```bash
# Build all tests
cmake --build build --config Release

# Run all tests via CTest
ctest --test-dir build --output-on-failure

# Run a single test (pattern match)
ctest --test-dir build -R test_error

# Run test executable directly (Windows: Qt DLLs must be in PATH)
./build/tests/core/test_error.exe
```

### Adding a New Test

Use the `add_qt_test()` helper in `tests/<module>/CMakeLists.txt`:

```cmake
add_qt_test(test_myclass
  SOURCES test_myclass.cpp ${CMAKE_SOURCE_DIR}/src/module/myclass.cpp
  LINKS core_services vxcore    # Optional extra link libraries
  GUILESS                       # Optional: headless (QCoreApplication)
)
```

Test files use `QTEST_GUILESS_MAIN(tests::TestClassName)` and must end with `#include "test_filename.moc"`.

## Architecture

### Dual Architecture (Legacy + New)

The codebase has two coexisting architectures:

- **Legacy:** Singleton-based (`VNoteX::getInst()`, `ConfigMgr::getInst()`) — do NOT use for new code
- **New (files with `2` suffix):** Dependency injection via `ServiceLocator` passed through constructors

New files coexist with legacy via the `2` suffix convention (e.g., `MainWindow2`, `ConfigMgr2`, `Buffer2`).

### MVC + Service Layer

```
Controllers (src/controllers/)    — business logic, QObject (not QWidget), testable
    ↕ signals/slots
Models (src/models/)              — QAbstractItemModel subclasses, no UI logic
Views (src/views/)                — QTreeView/delegates, display only, emit signals
    ↕
ServiceLocator                    — DI container (non-owning pointers, NOT a singleton)
    ↕
Services (src/core/services/)     — wrap vxcore C API with Qt-friendly interface
    ↕
vxcore (libs/vxcore/)             — C library: notebook/config/search backend
```

**MVC rules:** Models must not contain UI logic. Views must not modify data directly. Controllers must not inherit QWidget. All layers receive `ServiceLocator&` via constructor.

### Key Types

| Type | Role |
|------|------|
| `ServiceLocator` | DI container; stores non-owning `void*` pointers keyed by `type_index` |
| `NodeIdentifier` | Lightweight value type: `notebookId` (GUID) + `relativePath` |
| `Buffer2` | Lightweight copyable handle (like `QModelIndex`), returned by `BufferService::openBuffer()` |
| `XXXCoreService` | Low-level services wrapping vxcore C API (hold `VxCoreContextHandle`) |
| `BufferService` | Hook-aware wrapper over `BufferCoreService`; fires `vnote.file.*` hooks |
| `HookManager` | WordPress-style hook system: actions (cancellable events) + filters (data transforms) |

### Service Registration (main.cpp)

Services are stack-allocated in `main()` within a scoped block, registered as non-owning pointers in `ServiceLocator`, and destroyed before `vxcore_context_destroy()`. Order matters.

### Hook System

Plugins use `HookManager::addAction()` / `addFilter()` with priority ordering. Hook names are constants in `src/core/hooknames.h` (e.g., `vnote.notebook.before_open`, `vnote.file.before_save`). Actions can cancel operations via `HookContext::cancel()`. When adding hooks to existing code, use the WRAP pattern: fire before-hook → original signal → after-hook.

### Git Submodules

Three submodules in `libs/`:
- **vtextedit** — Rich text/Markdown editor widget
- **QHotkey** — Cross-platform global hotkey support
- **vxcore** — C library backend (built as static lib, tests/CLI disabled)

## Code Style

Enforced by `.clang-format` via pre-commit hook. Key conventions:

- **C++14**, 2-space indent, 100-char line limit
- **Naming:** `CamelCase` classes, `camelCase` methods, `p_` params, `m_` members, `c_` constants
- **Pointer alignment:** right (`int *ptr`)
- **Include order:** own header → Qt → local/project → `using namespace vnotex;` in `.cpp` only
- **Header guards:** `#ifndef CLASSNAME_H` / `#define CLASSNAME_H`
- **Namespace:** single `vnotex` namespace; never `using namespace` in headers
- `libs/` directory is excluded from formatting (third-party code)

### Manual formatting

```bash
clang-format -i src/core/myfile.cpp
```

## Directory Layout (New Architecture)

| Directory | Contents |
|-----------|----------|
| `src/core/services/` | Service layer (`core_services` static library, links `vxcore`) |
| `src/core/` | Core types, legacy code, config, hook system |
| `src/controllers/` | MVC controllers |
| `src/models/` | MVC models |
| `src/views/` | MVC views and delegates |
| `src/widgets/` | UI widgets (receive `ServiceLocator&`), including `dialogs/` |
| `src/gui/services/` | GUI-aware services (`ThemeService`, `ViewWindowFactory`) |
| `src/gui/utils/` | GUI utility helpers |
| `tests/` | Qt Test suites; `helpers/` has `TempDirFixture` and common includes |
| `libs/` | Git submodules (vtextedit, QHotkey, vxcore) |

## Migration Patterns

When migrating legacy code to new architecture:
1. Create new file with `2` suffix
2. Replace `ConfigMgr::getInst()` → `m_services.get<ConfigCoreService>()`
3. Replace `VNoteX::getInst().getNotebookMgr()` → `m_services.get<NotebookCoreService>()`
4. Add `ServiceLocator &p_services` constructor parameter, store as `m_services`
5. Add to appropriate `CMakeLists.txt`

## CI

GitHub Actions workflows in `.github/workflows/`: `ci-win.yml`, `ci-linux.yml`, `ci-macos.yml`. Trigger on push/PR to `master`. Windows builds use Ninja + MSVC 2022 with Qt 6.8.3.
