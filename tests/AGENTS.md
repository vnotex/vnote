# Agent Guidelines for tests

Test infrastructure for VNote using the QtTest framework. All tests live under `tests/` and are built via CMake's `add_qt_test()` helper. Tests exercise services, models, controllers, and utilities against the vxcore C backend in isolated test mode.

## Test Structure

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
│   ├── test_notebookservice.cpp
│   ├── test_bufferservice.cpp   # BufferCoreService tests
│   └── test_buffer.cpp          # Buffer2 + BufferService integration tests (29 cases)
└── utils/
    ├── CMakeLists.txt
    ├── test_pathutils.cpp
    └── test_htmlutils.cpp
```

## CRITICAL: Test Mode for vxcore

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

## Writing Service Tests

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

## Enable Tests
Uncomment in root `CMakeLists.txt`:
```cmake
add_subdirectory(tests)
```

## Build Tests
```bash
cmake --build build --config Release --target test_error test_exception test_pathutils test_htmlutils
```

## Run Tests

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

## Adding New Tests

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

## Qt Test Macros

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

## Current Test Coverage

| Test | Class | Test Cases |
|------|-------|------------|
| test_error | `Error`, `ErrorCode` | 23 |
| test_exception | `Exception` | 20 |
| test_pathutils | `PathUtils` | 68 |
| test_htmlutils | `HtmlUtils` | 31 |
| test_fileutils2 | `FileUtils2` | 15 |
| test_configservice | `ConfigCoreService` | 10 |
| test_notebookservice | `NotebookCoreService` | 33 |
| test_bufferservice | `BufferCoreService` | - |
| test_buffer | `Buffer2` + `BufferService` | 29 |
| test_searchservice | `SearchCoreService` | - |
| test_servicelocator | `ServiceLocator` | - |
| test_configmgr2 | `ConfigMgr2` | - |
| test_hookmanager | `HookManager` | 24 |
| test_hookintegration | Hook integration | 10 |
| **Total** | | **260+** |

## Related Modules

- [`../src/core/AGENTS.md`](../src/core/AGENTS.md) — Services and hooks being tested
- [`../AGENTS.md`](../AGENTS.md) — Code style, architecture overview
