# Agent Guidelines for tests

Test infrastructure for VNote using the QtTest framework. Parent VNote Qt tests live under
`tests/` and are built via CMake's `add_qt_test()` helper; vxcore has a **separate** suite under
`libs/vxcore/tests/` with its own build dir and `add_vxcore_test()` helper. Tests exercise
services, models, controllers, and utilities against the vxcore C backend in isolated test mode.

## Two test locations — pick the right one

VNote has tests in **two separate locations** with different registration helpers and build
configurations. Pick the right one or your test will silently never run.

### Parent VNote tests (`tests/`)

Built automatically with the parent project into `build-debug/tests/<category>/`. Registered via
the `add_qt_test()` helper (details in [Adding New Tests](#adding-new-tests) below). Categories:
`controllers/`, `core/`, `gui/`, `integration/`, `models/`, `utils/`, `widgets/`.

Reconfigure from repo root if `tests/CMakeLists.txt` itself changed: `cmake -B build-debug`.

**Build + run:**
```powershell
cmake --build build-debug --config Debug --target test_yourthing
ctest --test-dir build-debug -C Debug -R "^test_yourthing$" --output-on-failure
```

### vxcore submodule tests (`libs/vxcore/tests/`)

**Separate build dir required** — the parent `build-debug/` does NOT compile vxcore tests. Use a
dedicated build dir configured with `-DVXCORE_BUILD_TESTS=ON`. Each test is a standalone
executable (file basename = test target name) registered via `add_vxcore_test()`.

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

## Test Structure

```
tests/
├── CMakeLists.txt          # Root test config with add_qt_test() helper
├── helpers/
│   ├── CMakeLists.txt
│   ├── test_helper.h       # Common includes
│   └── temp_dir_fixture.h  # QTemporaryDir wrapper (+ copyFrom() for fixtures)
├── data/                   # On-disk test fixtures (see "Test Data Fixtures" below)
│   └── vnote3_notebooks/
│       └── database_notebook/  # Real VNote3 notebook with 17 files + subfolder
├── core/
│   ├── CMakeLists.txt
│   ├── test_error.cpp
│   ├── test_exception.cpp
│   ├── test_configservice.cpp
│   ├── test_notebookservice.cpp
│   ├── test_bufferservice.cpp   # BufferCoreService tests
│   ├── test_buffer.cpp          # Buffer2 + BufferService integration tests (29 cases)
│   └── test_vnote3migrationservice.cpp  # VNote3 migration tests (46 cases)
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

### Test Libraries

| Library | Purpose | Links |
|---------|---------|-------|
| `core_services` | Service layer (ConfigCoreService, NotebookCoreService, etc.) | Qt6::Core, Qt6::Gui, Qt6::Network, Qt6::Concurrent, vxcore, core_net |
| `core_net` | Qt-Core/Network-only HTTP helpers (`vnotex::NetworkAccess`) | Qt6::Core, Qt6::Network |
| `core_configs` | Config classes (ConfigMgr2, MainConfig, SessionConfig, etc.) | core_services, VTextEdit |

`core_services` links **neither `Qt::Widgets` nor `VTextEdit`** — see [`../src/core/services/AGENTS.md`](../src/core/services/AGENTS.md#core_services-is-qt-widgets-free-and-vtextedit-free-contract). Practical consequence for tests: a target that needs QtWidgets or vtextedit headers must say so in its own `LINKS`; nothing arrives transitively through `core_services` any more.

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

### VTextEdit.dll runtime copy

Tests that link `VTextEdit` (directly, or transitively via `core_configs`) load `VTextEdit.dll` at runtime. **`core_services` alone no longer pulls it in**, so a pure-core test that links only `core_services` + `vxcore` runs with no `VTextEdit.dll` anywhere near it. The build copies the DLL next to each subdirectory's test exes via a `POST_BUILD` step anchored on one test target per subdir (the canonical reference is `tests/utils/CMakeLists.txt:78-83`):

```cmake
# Copy VTextEdit DLL next to test executable so CTest can find it at runtime.
add_custom_command(TARGET test_clipboard_image POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:VTextEdit>
    $<TARGET_FILE_DIR:test_clipboard_image>
)
```

**When adding tests to a NEW subdirectory under `tests/` whose targets link `VTextEdit`**, that subdir's `CMakeLists.txt` MUST include this `POST_BUILD` block anchored on any one such test target. Without it, `ctest` reports the test as failed with exit code `0xc0000135` (Windows "DLL not found") and the test binary never reaches its `main()`. One copy per subdir is enough; `copy_if_different` makes incremental rebuilds free.

### Running execs that depend on VTextEdit.dll (CRITICAL — avoid false-positive smoke tests)

`build-debug/src/vnote.exe` and the widget/editor-level `build-debug/tests/<category>/test_*.exe` execs link against `VTextEdit.dll` (built into `build-debug/libs/vtextedit/src/`) plus the Qt 6 runtime DLLs. Pure-core tests that link only `core_services` + `vxcore` do NOT (see [`../src/core/services/AGENTS.md`](../src/core/services/AGENTS.md#core_services-is-qt-widgets-free-and-vtextedit-free-contract)). Neither DLL set is automatically copied next to `vnote.exe` in this development build (only test-exec dirs get VTextEdit.dll copied via CMake target propagation). Launching `vnote.exe` without setting PATH first causes the Windows loader to pop a "VTextEdit.dll was not found" modal dialog BEFORE the process reaches `WinMain`.

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

The same PATH prepend is required before running parent `test_*.exe` directly. `ctest --test-dir build-debug` inherits the caller's PATH, so set it once at the start of the session.

vxcore submodule tests (`libs/vxcore/build_test/bin/Debug/test_*.exe`) do NOT depend on Qt or VTextEdit and need no PATH setup.

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
  GUILESS     # Optional: do NOT link Qt6::Widgets
)
```

### The `GUILESS` flag is load-bearing (linking only)

`GUILESS` controls **one thing**: whether `add_qt_test` adds `Qt6::Gui` + `Qt6::Widgets` to the target. Non-GUILESS targets get them; GUILESS targets do not. This became load-bearing when `core_services` stopped linking `VTextEdit` (which used to leak `Qt::Widgets` in transitively) — see [`../src/core/services/AGENTS.md`](../src/core/services/AGENTS.md#core_services-is-qt-widgets-free-and-vtextedit-free-contract).

It does **not** — and cannot — choose between `QTEST_MAIN` and `QTEST_GUILESS_MAIN`. That is a source-level macro decision made at the bottom of the test `.cpp`. The two are related but independent:

- A test using `QTEST_GUILESS_MAIN` that merely **includes** a QtWidgets header (without ever constructing a `QApplication`) is correctly labelled `GUILESS` and should list `Qt6::Widgets` explicitly in `LINKS`. `test_clipboard_image`, `test_exportcontroller`, `test_findunitedentry`, `test_missing_nodes_qt`, `test_duplicate_open_guard`, `test_newnotebookcontroller`, and `test_openvnote3notebookcontroller` all do exactly this.
- Never "fix" a missing-QtWidgets compile error by relaxing `core_services`. Add the module the failing target actually needs to that target's `LINKS`.

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

## Test Data Fixtures

Tests that need realistic directory structures (e.g., legacy notebook migration) use on-disk fixtures under `tests/data/` instead of hardcoding JSON and file creation in C++.

### Pattern

1. **Create fixture directory** under `tests/data/` with real files on disk
2. **Locate at runtime** using `QFINDTESTDATA` (works out of the box with Qt6+CMake)
3. **Copy to temp dir** using `TempDirFixture::copyFrom()` for test isolation

### Example

```cpp
// In your test class
QString findFixture(const QString &p_relPath) {
  // Path is relative to the test source file
  QString path = QFINDTESTDATA(p_relPath);
  QVERIFY2_RETURN(!path.isEmpty(),
    qPrintable(QStringLiteral("Fixture not found: %1").arg(p_relPath)),
    QString());
  return path;
}

void TestMyService::testWithFixture() {
  // Locate fixture (relative to this .cpp file's directory)
  QString fixturePath = findFixture(
    QStringLiteral("../data/vnote3_notebooks/database_notebook"));
  QVERIFY2(!fixturePath.isEmpty(), "Fixture not found");

  // Copy to isolated temp dir
  TempDirFixture workDir;
  QString sourceDir = workDir.copyFrom(fixturePath, QStringLiteral("source"));
  QVERIFY2(!sourceDir.isEmpty(), "Failed to copy fixture");

  // Test against the copy
  // ...
}
```

### Adding New Fixtures

1. Create directory under `tests/data/` with descriptive name
2. Add all files the test needs (stubs are fine — content doesn't have to be real)
3. For Chinese/Unicode filenames, the files must exist on disk with those names
4. No CMake changes needed — `QFINDTESTDATA` resolves paths automatically via `QT_TESTCASE_SOURCEDIR`

### Existing Fixtures

| Fixture | Path | Description |
|---------|------|-------------|
| database_notebook | `tests/data/vnote3_notebooks/database_notebook/` | VNote3 notebook with 17 files (Chinese + ASCII), 1 subfolder, 3 attachment dirs |

## Real Keychain Usage

All **new or modified tests** that instantiate the real `SyncCredentialsStore` (not a `MockCredentialsStore` / in-memory fake) MUST register a `tests::KeychainGuard` to track and clean up written PAT entries.

### Why

Without deterministic cleanup, tests leak PAT entries into the host OS keychain. Fixed-ID tests overwrite themselves harmlessly, but UUID-based writes accumulate forever. QtKeychain has no enumerate API (see `src/core/services/synccredentialsstore.cpp:71`), so cleanup cannot be done by sweeping the keychain at startup; it must be driven by tracking each write.

### How

```cpp
#include "../helpers/keychain_guard.h"

// Class-level member:
tests::KeychainGuard *m_keychainGuard = nullptr;

// In initTestCase(), after registering SyncCredentialsStore:
auto *credStore = m_services.get<vnotex::SyncCredentialsStore>();
m_keychainGuard = new tests::KeychainGuard(credStore, this);

// In cleanupTestCase(), BEFORE destroying the vxcore context:
if (m_keychainGuard) {
  m_keychainGuard->cleanup();
  delete m_keychainGuard;
  m_keychainGuard = nullptr;
}

// For indirect writes (e.g. via SyncService::enableSyncForNotebook),
// also call track() defensively in case the credentialsStored signal races:
m_keychainGuard->track(notebookId);
```

### Ordering

`cleanup()` MUST run before the vxcore context is destroyed. The guard talks to `SyncCredentialsStore`, which depends on `ServiceLocator` and the live vxcore context; tearing the context down first leaves the guard with dangling references.

### Cross-reference

For the prod-side cleanup contract (5 sites in `SyncService`), see [`../src/core/services/AGENTS.md` § Credential Cleanup Invariants](../src/core/services/AGENTS.md#credential-cleanup-invariants).

### Mandatory `KeychainGuard` rollout (post-fix-failing-tests plan)

All sync tests that instantiate `SyncCredentialsStore` (whether through `ServiceLocator` or by local construction) MUST register a `tests::KeychainGuard`. This is non-negotiable: missing guards cause cumulative `notebook_sync_pat_<uuid>` accumulation in the Windows Credential Manager, eventually tripping Win32 error 8 ("Not enough storage") which cascades every sync test into failure on subsequent runs.

As of 2026-06-05, the following tests are compliant:

- `test_synccredentialsstore` (canonical reference)
- `test_sync_signal_auto_baseline`, `test_sync_signal_baseline`
- `test_sync_hooks`, `test_sync_ops`, `test_sync_auto_route`, `test_sync_close_block`
- `test_bootstrap_and_persist`
- `test_syncservice`, `test_syncservice_lifecycle`

(10 tests total.) New sync tests MUST follow the template in the "Real Keychain Usage → How" section above. Tests that do NOT touch the keychain (`test_eventbridge_sync`, `test_notebookcoreservice_sync`) are exempt.

**Operator note (Windows):** the keychain entry surfaces in `cmdkey /list` as `LegacyGeneric:target=notebook_sync_pat_<uuid>` (CRED_TYPE_GENERIC). `KeychainGuard::cleanup()` delegates to `SyncCredentialsStore::deleteCredentials()`, which handles the full target name internally; operators do NOT need to manage the `LegacyGeneric:target=` prefix manually when invoking the guard.

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
| test_vnote3migrationservice | `VNote3MigrationService` | 46 |
| test_searchservice | `SearchCoreService` | - |
| test_servicelocator | `ServiceLocator` | - |
| test_configmgr2 | `ConfigMgr2` | 7 |
| test_hookmanager | `HookManager` | 24 |
| test_hookintegration | Hook integration | 10 |
| **Total** | | **267+** |

## Debugging Tips

### Windows exit code `0xC0000409` is NOT a stack overrun

`0xC0000409` (`STATUS_STACK_BUFFER_OVERRUN`) is misleading. On Windows, Qt's `qFatal()` calls `__fastfail(FAST_FAIL_FATAL_APP_EXIT)`, which the OS reports as `0xC0000409`. The actual cause is almost always a `qFatal()` deep inside Qt, e.g., `QFontDatabase: Must construct a QGuiApplication before accessing QFontDatabase` raised when a `QTEST_GUILESS_MAIN` test transitively touches font-database code.

When a test exits with this code, **re-run it with file-based output capture** to surface the real Qt diagnostic:

```powershell
$env:PATH = "C:/Qt/6.9.3/msvc2022_64/bin;" + $env:PATH
& "build/.../tests/<category>/test_name.exe" -o "output.txt,txt"
Get-Content output.txt | Select-String "QFATAL|FAIL"
```

The `QFATAL : ...` message line names the real cause. Do NOT chase the test for recursion, oversized stack allocations, or fixture loops; that is a false trail. Fix the underlying Qt precondition (switch to `QTEST_MAIN`, guard the GUI-dependent code path, or short-circuit before the offending Qt subsystem is touched).

## Related Modules

- [`../src/core/AGENTS.md`](../src/core/AGENTS.md) — Services and hooks being tested
- [`../AGENTS.md`](../AGENTS.md) — Code style, architecture overview
