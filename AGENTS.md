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

VNote uses a **clean architecture** with dependency injection for testability and future plugin support.

### Layer Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                        main2.cpp                            │
│  (Entry point: creates context, wires dependencies)         │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     ServiceLocator                          │
│  (DI container - NOT a singleton, passed by reference)      │
│                                                             │
│  ┌─────────────┐  ┌─────────────────┐  ┌─────────────────┐  │
│  │ConfigService│  │ NotebookService │  │  SearchService  │  │
│  └─────────────┘  └─────────────────┘  └─────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                         vxcore                              │
│  (C library: notebook/config/search backend in libs/vxcore) │
└─────────────────────────────────────────────────────────────┘
```

### Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| ServiceLocator is NOT a singleton | Enables testing with mock services; explicit dependencies |
| Services wrap vxcore C API | Qt-friendly interface; encapsulates C interop |
| Widgets receive `ServiceLocator&` | Constructor injection; no global state |
| New files use `2` suffix | Coexist with legacy code during migration |

### Directory Structure (New Architecture)

```
src/
├── main2.cpp              # New entry point with DI wiring
├── core/
│   ├── servicelocator.h   # DI container
│   ├── configservice.h    # Config operations (wraps vxcore)
│   ├── notebookservice.h  # Notebook/folder/file ops (wraps vxcore)
│   ├── searchservice.h    # Search operations (wraps vxcore)
│   ├── configmgr2.h       # High-level config manager using DI
│   └── iconfigmgr.h       # Interface for config managers
├── widgets/
│   ├── mainwindow.h       # Legacy main window
│   ├── mainwindow2.h      # New main window shell (receives ServiceLocator&)
│   ├── mainwindow2.cpp
│   └── utils/             # GUI utilities (e.g., imageutils)
└── ... (legacy code remains for reference)
```

---

## ServiceLocator Pattern

### Registration (in main2.cpp)

```cpp
#include <core/servicelocator.h>
#include <core/configservice.h>
#include <core/notebookservice.h>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  // Create vxcore context
  vxcore_context *ctx = vxcore_context_create();

  // Create and populate ServiceLocator
  vnotex::ServiceLocator services;
  services.registerService<vnotex::ConfigService>(
      std::make_unique<vnotex::ConfigService>(ctx));
  services.registerService<vnotex::NotebookService>(
      std::make_unique<vnotex::NotebookService>(ctx));

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
// src/ui/mywidget.h
class MyWidget : public QWidget {
  Q_OBJECT
public:
  explicit MyWidget(ServiceLocator &p_services, QWidget *p_parent = nullptr);

private:
  ServiceLocator &m_services;
};

// src/ui/mywidget.cpp
MyWidget::MyWidget(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {
  // Access services when needed
  auto &config = m_services.get<ConfigService>();
  auto theme = config.getTheme();
}
```

### Available Services

| Service | Purpose | Key Methods |
|---------|---------|-------------|
| `ConfigService` | App configuration | `getTheme()`, `setTheme()`, `getLocale()`, etc. |
| `NotebookService` | Notebook management | `createNotebook()`, `openNotebook()`, `createFile()`, etc. |
| `SearchService` | Content search | `searchContent()`, `searchFiles()` |

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
    auto &config = m_services.get<ConfigService>();
    auto &notebooks = m_services.get<NotebookService>();
  }
};
```

### Migration Checklist

- [ ] Create new file with `2` suffix (e.g., `mywidget2.h`)
- [ ] Add `ServiceLocator &p_services` to constructor
- [ ] Store reference: `ServiceLocator &m_services`
- [ ] Replace `ConfigMgr::getInst()` → `m_services.get<ConfigService>()`
- [ ] Replace `VNoteX::getInst().getNotebookMgr()` → `m_services.get<NotebookService>()`
- [ ] Update parent widget to pass `ServiceLocator&`
- [ ] Add to CMakeLists.txt
- [ ] Write unit test with mock services

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

2. **Register in main2.cpp**:
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

---

## Testing

### Test Structure

```
tests/
├── CMakeLists.txt          # Root test config with add_qt_test() helper
├── helpers/
│   ├── CMakeLists.txt
│   ├── test_helper.h       # Common includes
│   └── temp_dir_fixture.h  # QTemporaryDir wrapper
├── core/
│   ├── CMakeLists.txt
│   ├── test_error.cpp
│   ├── test_exception.cpp
│   ├── test_configservice.cpp
│   └── test_notebookservice.cpp
└── utils/
    ├── CMakeLists.txt
    ├── test_pathutils.cpp
    └── test_htmlutils.cpp
```

### CRITICAL: Test Mode for vxcore

**Always enable test mode BEFORE creating vxcore context in tests:**

```cpp
void TestMyService::initTestCase() {
  // CRITICAL: Must call BEFORE vxcore_context_create()
  // Prevents tests from corrupting real user data
  vxcore_set_test_mode(1);

  m_ctx = vxcore_context_create();
  // ...
}
```

**Why this matters:**
- Without test mode, vxcore uses real `AppData/Local` paths
- Tests will corrupt actual user configuration
- Test mode redirects to isolated temp directories

### Writing Service Tests

```cpp
// tests/core/test_myservice.cpp
#include <QtTest>

#include <vxcore/vxcore.h>

#include <core/myservice.h>

namespace tests {

class TestMyService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void testBasicOperation();

private:
  vxcore_context *m_ctx = nullptr;
};

void TestMyService::initTestCase() {
  vxcore_set_test_mode(1);  // CRITICAL!
  m_ctx = vxcore_context_create();
}

void TestMyService::cleanupTestCase() {
  vxcore_context_destroy(m_ctx);
  m_ctx = nullptr;
}

void TestMyService::testBasicOperation() {
  vnotex::MyService service(m_ctx);
  QVERIFY(!service.doSomething().isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestMyService)
#include "test_myservice.moc"
```

### Enable Tests
Uncomment in root `CMakeLists.txt`:
```cmake
add_subdirectory(tests)
```

### Build Tests
```bash
cmake --build build --config Release --target test_error test_exception test_pathutils test_htmlutils
```

### Run Tests

**Windows (Qt DLLs must be in PATH):**
```powershell
$env:PATH = "C:/Qt/6.9.3/msvc2022_64/bin;" + $env:PATH
./build/tests/core/test_error.exe
./build/tests/core/test_configservice.exe
./build/tests/core/test_notebookservice.exe
```

**Using CTest (requires Qt in system PATH):**
```bash
ctest --test-dir build                    # Run all tests
ctest --test-dir build -R test_error      # Run single test (pattern match)
ctest --test-dir build --output-on-failure  # Show output on failure
```

### Adding New Tests

Use the `add_qt_test()` helper function in CMakeLists.txt:

```cmake
# tests/module/CMakeLists.txt
add_qt_test(test_myclass
  SOURCES
    test_myclass.cpp
    ${CMAKE_SOURCE_DIR}/src/module/myclass.cpp
  LINKS
    Qt6::Gui  # Optional: extra Qt modules
  GUILESS     # Optional: use QCoreApplication instead of QApplication
)
```

### Qt Test Macros

| Macro | Purpose |
|-------|---------|
| `QTEST_MAIN(Class)` | Creates main(), runs tests with QApplication |
| `QTEST_GUILESS_MAIN(Class)` | Headless test runner (preferred) |
| `QVERIFY(condition)` | Assert condition is true |
| `QCOMPARE(actual, expected)` | Assert equality |
| `QFETCH(type, name)` | Fetch data-driven test value |
| `QTest::addColumn<T>("name")` | Declare data column |
| `QTest::newRow("name")` | Add data row |
| `QSKIP("reason")` | Skip test |
| `QTest::ignoreMessage(type, msg)` | Suppress expected qDebug/qWarning/qCritical |

### Current Test Coverage

| Test | Class | Test Cases |
|------|-------|------------|
| test_error | `Error`, `ErrorCode` | 23 |
| test_exception | `Exception` | 20 |
| test_pathutils | `PathUtils` | 68 |
| test_htmlutils | `HtmlUtils` | 31 |
| test_fileutils2 | `FileUtils2` | 15 |
| test_configservice | `ConfigService` | 10 |
| test_notebookservice | `NotebookService` | 33 |
| **Total** | | **200+** |

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
- Use `vnotex` namespace for all core classes
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
// LEGACY - Use ConfigService via ServiceLocator instead
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
