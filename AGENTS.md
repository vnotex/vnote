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
│   ├── test_error.cpp      # Error class tests
│   └── test_exception.cpp  # Exception class tests
└── utils/
    ├── CMakeLists.txt
    ├── test_pathutils.cpp  # PathUtils tests
    └── test_htmlutils.cpp  # HtmlUtils tests
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
./build/tests/core/test_exception.exe
./build/tests/utils/test_pathutils.exe
./build/tests/utils/test_htmlutils.exe
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

### Writing Tests

Tests use Qt Test framework with single-file structure (no separate header):

```cpp
// tests/module/test_myclass.cpp
#include <QtTest>

#include <module/myclass.h>

using namespace vnotex;

namespace tests {

class TestMyClass : public QObject {
  Q_OBJECT

private slots:
  // Basic test
  void testBasicFeature();

  // Data-driven test pair
  void testWithData_data();
  void testWithData();
};

void TestMyClass::testBasicFeature() {
  QVERIFY(true);
  QCOMPARE(1 + 1, 2);
}

void TestMyClass::testWithData_data() {
  QTest::addColumn<QString>("input");
  QTest::addColumn<QString>("expected");

  QTest::newRow("case1") << "hello" << "HELLO";
  QTest::newRow("case2") << "world" << "WORLD";
}

void TestMyClass::testWithData() {
  QFETCH(QString, input);
  QFETCH(QString, expected);
  QCOMPARE(input.toUpper(), expected);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestMyClass)
#include "test_myclass.moc"
```

**Key points:**
- Use `QTEST_GUILESS_MAIN` for non-GUI tests (faster)
- Include `#include "test_myclass.moc"` at end of file
- All test classes in `tests` namespace
- Data-driven tests use `_data()` suffix pattern

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
| **Total** | | **157** |

## Project Structure

```
vnote/
├── src/
│   ├── core/           # Core logic (VNoteX, ConfigMgr, Buffer, etc.)
│   ├── widgets/        # UI components (MainWindow, dialogs, editors)
│   ├── utils/          # Utility classes (Utils, FileUtils, PathUtils)
│   ├── task/           # Task system (Task, TaskMgr)
│   ├── search/         # Search functionality
│   ├── snippet/        # Snippet management
│   ├── export/         # Export functionality
│   ├── imagehost/      # Image hosting
│   └── unitedentry/    # United entry system
├── libs/               # Third-party libraries (DO NOT format)
│   ├── vtextedit/      # Text editor component
│   └── QHotkey/        # Global hotkey support
├── tests/              # Unit tests
└── scripts/            # Build and dev scripts
```

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

## Common Patterns

### Singleton Pattern
```cpp
// src/core/vnotex.h
class VNoteX : public QObject, private Noncopyable {
  Q_OBJECT
public:
  static VNoteX &getInst() {
    static VNoteX inst;
    return inst;
  }

  ThemeMgr &getThemeMgr() const;
  TaskMgr &getTaskMgr() const;
  NotebookMgr &getNotebookMgr() const;
  BufferMgr &getBufferMgr() const;

private:
  explicit VNoteX(QObject *p_parent = nullptr);
  ThemeMgr *m_themeMgr = nullptr;
  TaskMgr *m_taskMgr = nullptr;
  // ...
};

// Usage
auto &themeMgr = VNoteX::getInst().getThemeMgr();
```

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

## Configuration System

Access via `ConfigMgr::getInst()`:

| Config Type | Access Method | Purpose |
|-------------|---------------|---------|
| MainConfig | `getConfig()` | Main application config |
| SessionConfig | `getSessionConfig()` | Session-specific settings |
| CoreConfig | `getCoreConfig()` | Core behavior settings |
| EditorConfig | `getEditorConfig()` | Editor settings |
| WidgetConfig | `getWidgetConfig()` | UI widget settings |

```cpp
// Usage
auto &config = ConfigMgr::getInst();
auto theme = config.getCoreConfig().getTheme();
auto iconSize = config.getCoreConfig().getToolBarIconSize();
```

## Architecture Notes

### Core Singleton (VNoteX)
- Central coordinator for all managers
- Manages: ThemeMgr, TaskMgr, NotebookMgr, BufferMgr
- Access: `VNoteX::getInst()`

### Manager Hierarchy
```
VNoteX (singleton)
├── ThemeMgr       # Theme and styling
├── TaskMgr        # External task execution
├── NotebookMgr    # Notebook management
└── BufferMgr      # File buffer management
```

### Key Directories
| Directory | Purpose |
|-----------|---------|
| `src/core/` | Core logic, singletons, base classes |
| `src/widgets/` | Qt widgets, dialogs, UI components |
| `src/utils/` | Utility functions (file, path, image) |
| `src/task/` | Task system for external commands |

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
