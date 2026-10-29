# Agent Guidelines for tests

Test infrastructure for VNote using the QtTest framework. Parent VNote Qt tests live under
`tests/` and are built via CMake's `add_qt_test()` helper; vxcore has a **separate** suite under
`libs/vxcore/tests/` with its own build dir and `add_vxcore_test()` helper. Tests exercise
services, models, controllers, and utilities against the vxcore C backend in isolated test mode.

## Two test locations — pick the right one

VNote has tests in **two separate locations** with different registration helpers and build
configurations. Pick the right one or your test will silently never run.

### Parent VNote tests (`tests/`)

Built with the parent project into `build-debug/tests/<category>/` **when `-DVNOTE_BUILD_TESTS=ON`
is set** (it defaults to OFF; CI turns it ON). Registered via
the `add_qt_test()` helper (details in [Adding New Tests](#adding-new-tests) below). Categories:
`controllers/`, `core/`, `gui/`, `integration/`, `models/`, `utils/`, `widgets/`.

Configure the test-enabled build dir from repo root (also re-run this if `tests/CMakeLists.txt`
itself changed): `cmake -B build-debug -DVNOTE_BUILD_TESTS=ON`.

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

Encryption fixtures created by `prepare_notebook` / `commit_notebook` are self-contained v2
notes. Reopen them with `vxcore_encryption_unlock_note`, using the notebook-relative path
captured before closing the buffer; `unlock_notebook` is only for explicit legacy-vault tests.

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

**macOS keychain tests:** core tests that exercise real QtKeychain jobs use
`VNOTE_KEYCHAIN_TEST_MAIN` from `test_helper.h`: a native GUI application on macOS,
`QCoreApplication` elsewhere. On macOS, run these and keychain-using widget tests with
`QT_QPA_PLATFORM=cocoa`, not `offscreen`: QtKeychain completes on the native main queue,
which the offscreen UNIX dispatcher does not drain. Do not set
`QT_EVENT_DISPATCHER_CORE_FOUNDATION`: it also replaces worker-thread dispatchers and
can stall queued work. Tests also need an unlocked default keychain; macOS CI creates
a disposable one after compilation, immediately before CTest, so the unlock lease covers
execution rather than the cold build. It restores the previous default after testing.

For asynchronous modal-error tests, wait for controller completion with `QSignalSpy::wait`.
Keep the dismissal timer active until completion and restrict it to the tested dialog's
message box. Do not stop on the first click or infer dialog readiness from
`QThread::loopLevel()`.

`resourceRemoteConsent` owns a parent-matched `QPointer<QMessageBox>` and an answer timer
through queued Open Link dispatch and destruction of its completed message box, not just
until the context menu returns. A Cocoa native alert's QWidget is a hidden
mirror (`WA_DontShowOnScreen`), not its interactive surface: click the real NSButton's
Yes/No tag on the active native alert, or the parent's attached sheet for WindowModal.
Never re-show or click that dummy QWidget during native modal startup. The QWidget fallback
requires the matching visible active modal and its visible expected button. Cocoa uses
an `NSTimer` in common/modal/tracking run-loop modes: a native alert nested inside manual
Qt event dispatch can suppress QTimer activation. Invalidate before answering and on scope
exit; the same native timer owns the eight-second failure/reject deadline.

**Native WebEngine raster tests:** on macOS, `test_mindmapexport`, `test_htmlpdfexporter`,
`test_markdownwebjobs` and `test_webviewservice` use `cocoa` and the matching Qt kit's
platform plugin, not `offscreen`, so hidden views have native graphics contexts.
Keep deployed-app WebEngine overrides cleared and non-macOS environments unchanged.

Native Markdown render waits observe the adapter's completed document generation and
actual DOM, not the independent web-job handshake. JavaScript observations pump events;
use one `QTest::qWaitFor` and recheck the generation after observation, rather than a
`QTRY` predicate that can become false again during its post-success evaluation.
If the next assertion requires native currentness, use `waitForCurrentRender`, not the
DOM-only `waitForRender`; protected/public handoffs retain all token and stale-work checks.

Typst context-menu fixtures select Paste by Qt's `edit-paste` action ID and retire
resources after action admission, not from a timer that can fire during Cocoa's menu
flash. Empty clipboard MIME data is nullable. Native drag fixtures must release input
without depending on GUI timers: Windows OLE and Cocoa tracking loops can starve them.
Use joined native-input workers, restoring the cursor and preventing late input from
reaching the next case; Cocoa retains app-local NSEvents rather than global HID input.
Windows drag motion uses non-coalesced `SendInput` records in normalized virtual-desktop
coordinates. After selecting text, acquire foreground, repaint, deliver posted `LayoutRequest`
events, repaint the resolved gutter layout and complete `DwmFlush` before measuring positions.
After source-pointer receipt, recheck mapped start/end and `widgetAt` before pressing: a
first-paint gutter update must not move the viewport underneath cached coordinates.
Failure diagnostics record native move/press/activation messages and GUI-thread state.
Query `GetGUIThreadInfo(guiThreadId)` from the worker, not its own GetFocus/GetCapture;
never consume or reroute an event just to make the fixture pass.
Windows workers acknowledge the press and threshold, then post a registered native message to
the source HWND. The GUI filter acknowledges it only while an owned `QDrag` exists. Send one
distinct bootstrap motion, await capture transfer from the fixture to a GUI-thread window,
then send the final destination; an already-posted capture never skips the bootstrap motion.
All waits share the original deadline. Qt timers or waiting for DragEnter before startup motion
can deadlock in Qt's filtered native pump. Destination arrival can be DragEnter or DragMove.
Release follows arrival or the bounded deadline; joined workers always attempt release and
restore the saved pointer. Cocoa
creates timestamped NSEvents at dispatch, without warping ahead of delivered source events.
Create native peers before measuring drag coordinates. On Cocoa, find the source viewport's
nearest already-native QWidget ancestor without creating a peer. Its flipped QNSView coordinates
match Qt's window coordinates: map locally to that ancestor, then use `convertPoint:toView:nil`
for the event and `convertRectToScreen:` plus the primary-screen Y flip for the Quartz pointer.
Do not mix Qt's cached global frame origin into native input or compensate with titlebar offsets.
Typst shortcut/focus tests use xcb under Xvfb on Linux, not offscreen. Keep the normal
QtTest crash handler. Build the host Typst module's `test_typstviewwindow_insertion` executable
once, then select `^test_typstviewwindow_(insertion|cleanup|resources)$` in CTest. These serial
groups share the executable and each retain a 240-second deadline. With `VNOTE_TYPST_TEST_GROUP`
unset, a direct executable launch still runs every case. Each group writes plaintext stdout and
its own `test_typstviewwindow_<group>.xml` JUnit file in the CTest working directory. CI
preserves original parent/native CTest logs and these reports before diagnostic reruns,
since hosted stdout can omit assertions.
Set Qt's active window explicitly at focus-sensitive fixture operations; `activateWindow()`
alone depends on a window manager under Xvfb. After dismissing a modal dialog, wait for
window activation and editor focus before sending shortcuts. Settle native exposure and
activation before measuring drag coordinates. Before inserting tab-replacement or presentation
drafts programmatically, wait for editor focus: that registers the active writer used by Save.
The lazy-view `showWindow()` helper establishes those exposure/activation/focus preconditions;
do not require editor focus for hidden/disabled editors or encrypted buffers already locking.

`test_viewarea_tab_reuse` retains only its unmodified, version-stamped bundled asset directories
between QtTest rows. It removes all local state and all non-bundle app entries before creating
a fresh context, config, notebook, services and WebEngine profile. Obtain cleanup paths from
ConfigMgr2, and prune only in the test class `init()`, never inside `Fixture::initialize()`:
intra-row fixture recreation intentionally tests persisted sessions. This suite must not edit
installed assets, including `web/css/user.css` or user-created templates; asset-customization
fixtures must retain the full test-directory reset. Keep the existing suite deadlines.

PDF.js text-item boundaries are not word boundaries: ligatures can split a word across
items. Reconstruct text by concatenating `item.str` and adding a newline only for
`item.hasEOL`, as the bundled viewer does; do not insert spaces between every item.

### Local pre-push flaky-test whitelist

On a **shared interactive Windows desktop only**, the following native-input failures
may be treated as non-gating for a local push. This is a closed list of failure sites,
not permission to skip an executable or any product-behavior assertion.

| Executable | Exact QtTest case / row | Eligible failure site |
|---|---|---|
| `test_viewarea_tab_reuse` | `previewReaderNavigationAndNativeHost` | First source-reader `B` link's `clickNativeSurface` hit-test precondition |
| `test_viewarea_tab_reuse` | `previewOpenInNewTabs(main-source)` | Open Link button's `clickNativeSurface` hit-test precondition |
| `test_viewarea_tab_reuse` | `previewOpenInNewTabsDuplicateConsent(other-tab-save)` | Incoming editor's `hasFocus()` precondition before consent/promotion |

**All** of these conditions are required for each use:

1. Build the affected targets and attempt the **full executable** at the revision being
   pushed. Preserve the first failure report. Windows GUI executables may omit QtTest
   assertions from CTest's captured stdout; use explicit QtTest `-o <path>,txt` and
   `-o <path>,junitxml` outputs for diagnosis with the registered runtime environment.
2. Match both the exact row and failure site above. For a click failure, diagnostics must
   show a visible, in-bounds target, an inactive native window, and null/foreign
   `QApplication::widgetAt()` at the click position. For the editor-focus failure, also
   establish that the native window was inactive. A bare `clickNativeSurface`/`hasFocus`
   failure or a passing retry alone is **not** enough to classify the failure.
3. The affected case must pass in isolation at the same revision, and all remaining rows
   and other affected suites must run and pass. Prefer an exclusive desktop run when
   available. Do not retry unchanged runs merely until one passes; reruns must investigate
   a cause or follow an environment correction.
4. The unpushed changes must not modify the affected native preview input, focus, geometry,
   promotion, hit-test helpers, fixture activation or relevant platform configuration.
   Those changes require a clean native verification run or a separate explicit user exception.
5. Report the exact case, native-state evidence and local waived gate as a **skip**, not
   a pass. Any other failure, including later content, routing, save/discard/cancel,
   persistence assertions, crashes or timeouts, remains blocking even in a listed case.

CI and packaging gates are never waived. Do not add `QSKIP`, exclude the executable or
rows from CTest, weaken assertions, or change deadlines to implement this policy.
Reassess/remove an entry when its native activation cause is fixed.

The 2026-10-29 investigation observed a visible/in-bounds preview button with an inactive
native window and null `widgetAt`, while failing rows varied across runs; the unmodified
full suite subsequently passed all 73 cases. The fixture requests activation without
waiting for native activation. The original reader-click rejection and editor-focus
failure did not identify a covering process or independently prove their native-window
state, so those assertion messages alone do not qualify for the exception.

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

With Typst enabled, `VTextEdit.dll` directly depends on `vtextedit_typst.dll`. The common
`add_qt_test()` supplies Windows CTest lookup paths for Qt, VTextEdit and the native backend
without linking pure-core tests to them. Tests replacing ENVIRONMENT must restore PATH
afterward; append it separately to each test instead of copying an escaped PATH list.

**When adding tests to a NEW subdirectory under `tests/` whose targets link `VTextEdit`**, that subdir's `CMakeLists.txt` MUST include this `POST_BUILD` block anchored on any one such test target. Without it, `ctest` reports the test as failed with exit code `0xc0000135` (Windows "DLL not found") and the test binary never reaches its `main()`. One copy per subdir is enough; `copy_if_different` makes incremental rebuilds free.

### Running execs that depend on VTextEdit.dll (CRITICAL — avoid false-positive smoke tests)

`build-debug/src/vnote.exe` and widget/editor tests load `VTextEdit.dll` and its native
`vtextedit_typst.dll` dependency, plus the matching Qt 6 runtime. The editor/runtime DLLs
are staged in `build-debug/bin` and beside VNote; Typst test targets also refresh their
local copies. Pure-core tests linked only to `core_services` + `vxcore` remain independent
of VTextEdit. Direct test launches and disposable app copies still need the configured
Qt/runtime lookup paths. A missing DLL can show a loader modal before `WinMain`.

**This is a verification trap**: when the loader dialog blocks the process, the OS reports the process as alive (it has a PID, is technically running), so naive checks like `Start-Process … -PassThru` + `Sleep` + `HasExited` return `$false` → you falsely conclude the binary started. The process is actually frozen in pre-WinMain limbo waiting for someone to click the dialog. `Stop-Process -Force` afterwards silently dismisses the dialog and the lie is preserved.

### Agent GUI smoke testing (Windows)

Use `--no-single-instance` to test a separate VNote process without closing the user's running
instance. **It skips locking and IPC, not data isolation.** Never launch the agent's GUI smoke test
against the normal profile or real notebooks. Create a fresh portable copy for each run; its
existing `config/` folder redirects config, session and WebEngine data to that copy.

Before using this recipe, check the OS Documents folder's `my_notebook` path. If it is missing or
an empty directory, first-run setup can create/open a bundled notebook there, **outside the portable
copy**; use a disposable OS account instead. Do not edit, empty, or delete the user's directory to
satisfy the collision guard. Continue testing only the scratch file in the disposable copy.

Run from the repository root after building `vnote`. Set `$build` to the build you just verified;
the example derives the matching Qt runtime from its CMake cache, rather than mixing Qt versions.
Run both blocks in the same persistent PowerShell terminal, keeping it open during GUI inspection
and cleanup; avoid a one-shot wrapper that captures inherited process pipes.

```powershell
$ErrorActionPreference = "Stop"
$build = "build-debug" # Replace with the existing build you just compiled
$qtDir = (Select-String -Path "$build/CMakeCache.txt" -Pattern '^Qt6_DIR:PATH=' |
          Where-Object { $_.Line -notmatch 'NOTFOUND$' } | Select-Object -First 1).Line
if (-not $qtDir) { throw "No configured Qt directory in $build/CMakeCache.txt" }
$qtBin = [IO.Path]::GetFullPath((Join-Path ($qtDir -split '=', 2)[1] "../../../bin"))
$env:PATH = "$qtBin;" + (Resolve-Path "$build/bin").Path + ";" + $env:PATH
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $qtBin "../plugins/platforms"
$env:QTWEBENGINEPROCESS_PATH = Join-Path $qtBin "QtWebEngineProcess.exe"

$smoke = Join-Path $build ("agent-smoke-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path "$smoke/config" | Out-Null
Copy-Item "$build/src/vnote.exe", "$build/src/vnote_extra.rcc", "$build/src/qt.conf" $smoke
if (Test-Path "$build/src/translations") {
  Copy-Item "$build/src/translations" "$smoke/translations" -Recurse
}
Set-Content -Path "$smoke/smoke.txt" -Value "Disposable agent smoke-test note" -Encoding UTF8
$proc = Start-Process -FilePath "$smoke/vnote.exe" -PassThru `
  -ArgumentList @("--no-single-instance", "--log-stderr", "`"$smoke/smoke.txt`"") `
  -RedirectStandardError "$smoke/stderr.log" `
  -RedirectStandardOutput "$smoke/stdout.log"
Start-Sleep -Seconds 5
$proc.Refresh()
if ($proc.HasExited) { throw "VNote exited with $($proc.ExitCode); inspect $smoke/stderr.log" }
foreach ($module in @("VTextEdit.dll", "vtextedit_typst.dll")) {
  if (-not ($proc.Modules | Where-Object { $_.ModuleName -ieq $module })) {
    throw "$module not loaded: check runtime DLLs, not just process liveness"
  }
}
Write-Output "Inspect PID $($proc.Id): its own window must display smoke.txt and the note text"
```

The runtime check is only a prerequisite: inspect the **actual window and requested file**, and
exercise the feature under test. The main window title can remain `VNote`; do not require the file
name in that title. Startup/file-open diagnostics are in `$smoke/stderr.log`. Add `--detached-view`
when testing detached windows. Use only scratch notes/notebooks and no production sync credentials.

Quit through VNote's UI (answer **No** if asked to minimize to the system tray), allow save/sync
workers to drain, then clean up only this run's copy:

```powershell
$proc.WaitForExit()
Remove-Item -LiteralPath $smoke -Recurse -Force
```

Do not reuse the generated profile for the next run: loading its saved session also performs
normal Windows launch-on-login reconciliation. Do not enable launch-on-login in a disposable copy.
In-app restart drops the flag and uses the normal guard; start another fresh copy explicitly instead.
Changing `TEMP`/`TMP` does not isolate the fixed IPC name. For guard/forwarding tests use the unique
server/lock overrides in `test_singleinstanceguard`, never the user's production pipe or lock.
Behavior contract: [Agent / manual testing bypass](../src/core/AGENTS.md#agent--manual-testing-bypass).

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

### First-run Windows theme reports

The Windows CTest registration for `test_themeservice` writes
`tests/gui/test_themeservice.txt` and `tests/gui/test_themeservice.xml` in the build tree,
while retaining console output. CI includes both in `native-typst-test-report` before
any diagnostic rerun can overwrite the local files. Inspect these original reports first;
a passing rerun does not establish that the first-run failure was repaired. File reports
capture QtTest diagnostics, not failures occurring before QtTest initializes.

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
