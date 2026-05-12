# Learnings
## [2026-05-13 08:13:00 UTC] T1+T2 (vxcore C API): vxcore_sync_set_credentials + vxcore_sync_enable_with_credentials

### Patterns established
- C ABI wrappers in `src/api/vxcore_sync_api.cpp` follow a uniform shape: null-check
  pointers -> reinterpret_cast context -> guard `ctx->sync_manager` -> JSON parse ->
  call `SyncManager::*` -> map exceptions to `VXCORE_ERR_JSON_PARSE` /
  `VXCORE_ERR_UNKNOWN`. Mirrored existing `vxcore_sync_enable`.
- Credentials JSON shape (per plan ADR): `{"pat":"...","authorName":"...","authorEmail":"..."}`.
  All fields optional; missing/non-string -> empty string in `SyncCredentials`.
  We do NOT parse SSH key fields per plan (PAT-only for v1).
- Credentials NEVER logged. The wrapper logs only call sites and outcomes via
  the underlying `SyncManager` log statements; PAT value never crosses a log macro.

### Test infrastructure
- `add_vxcore_test(name)` only links `vxcore` and adds `src/` + `third_party/`
  to includes. `include/` is picked up transitively from `vxcore`'s public
  include directories. So `#include "vxcore/vxcore.h"` works without an extra
  `target_include_directories` call.
- For libgit2-backed fixtures we link the `git_sync_test_helpers` STATIC library
  (defined in `tests/CMakeLists.txt`). The library has its OWN copies of
  `libgit2_init.cpp` so its libgit2 ref-count statics do NOT collide with
  `vxcore.dll`'s copies. This is a Windows DLL static-storage isolation pattern
  used throughout the test suite.
- `vxcore_test::create_bare_repo(path)` returns a `file://` URL, `commit_file`
  pushes a single commit. Together they produce a remote with one commit on
  `main` suitable for `GitSyncBackend::Initialize` clone-into-empty branch.

### Test dispatch convention adopted for this binary
- `test_sync_credentials_api` accepts `argv[1]` as a case name; with no args
  it runs all cases. CTest invokes with no args (runs all); the orchestrator
  invokes per-case for evidence capture. Each case prints `PASS <name>` /
  `FAIL <name>` and the binary exits non-zero on any failure.

### GitSyncBackend Initialize flow observed
- Bundled notebook root after `vxcore_notebook_create` contains only `vx_notebook/`.
  `GitSyncBackend::Initialize`'s `has_user_files` walk explicitly skips
  `vx_notebook` -> root counts as empty -> takes the T15 clone-into-empty branch.
- After clone the seeded file appears at the workdir root, exactly as expected.

## [2026-05-13 08:16:49] T3: QtKeychain CMake integration
- Upstream tag naming: `0.14.x` releases use NO `v` prefix (only `v0.14.0` had the prefix). Use `GIT_TAG 0.14.3`, NOT `v0.14.3`.
- System `find_package(Qt6Keychain QUIET)` resolution name is `Qt6Keychain` (matches upstream's CMake config package name on Qt6 builds).
- FetchContent path was taken on this Windows machine (no system QtKeychain installed). Build downloads, configures with `BUILD_WITH_QT6=ON`, `BUILD_TEST_APPLICATION=OFF`, `BUILD_TRANSLATIONS=OFF`, and produces target `Qt6Keychain`. Verified in build log.
- `VNOTE_KEYCHAIN_AVAILABLE` (CMake variable) reflects success of either the system-find OR the FetchContent path. T4 will read this variable to gate `target_compile_definitions(... PRIVATE VNOTE_KEYCHAIN_AVAILABLE=1)`.

## [2026-05-13 08:30:27 UTC] T4: SyncCredentialsStore (QtKeychain wrapper)

### QtKeychain integration findings (CRITICAL for T7+ consumers)
- **Header include**: `#include <keychain.h>` (NOT `<qt6keychain/keychain.h>`).
  When built via FetchContent, headers live at `\/keychain.h` and
  `\/qkeychain_export.h`. The QtKeychain CMakeLists only sets
  `INSTALL_INTERFACE` includes (not `BUILD_INTERFACE`), so consumers must add the source
  + binary dirs explicitly. The system-find_package path resolves the same `<keychain.h>`
  via `<install-prefix>/include/qt6keychain/` but the bare include name still works because
  CMake adds the right dir.
- **CMake link target**: prefer `Qt6Keychain::Qt6Keychain` (alias added by both system-find
  and FetchContent paths if CMake >= 3.18). Fallback to raw `qt6keychain` for older CMake.
  Pattern used in src/core/services/CMakeLists.txt:
  `\\\cmake
  if(TARGET Qt6Keychain::Qt6Keychain)
      target_link_libraries(... PRIVATE Qt6Keychain::Qt6Keychain)
  elseif(TARGET qt6keychain)
      target_link_libraries(... PRIVATE qt6keychain)
  endif()
  \\\`
- **FetchContent include propagation workaround** (in src/core/services/CMakeLists.txt):
  `\\\cmake
  if(qtkeychain_SOURCE_DIR)
      target_include_directories(core_services PRIVATE
          \
          \
      )
  endif()
  \\\`
  T7+ consumers that link `core_services` PRIVATEly do NOT need to repeat this; the
  `#include <keychain.h>` is already wrapped behind `#ifdef VNOTE_KEYCHAIN_AVAILABLE`
  inside synccredentialsstore.cpp. Other consumers that include `synccredentialsstore.h`
  do NOT need keychain headers because the public header has zero QtKeychain references.

### Async pattern
- All public methods return immediately. `QKeychain::Job` runs the OS keychain call
  internally and emits `finished` on the calling thread. We use `setAutoDelete(false)` +
  `deleteLater()` in our finished handler so the lambda can outlive the job's normal lifetime.
- For the keychain-unavailable path, `QMetaObject::invokeMethod(this, lambda, Qt::QueuedConnection)`
  defers the `credentialsError` emission to the next event-loop tick, matching the async contract.

### Test discoveries
- QtTest's stdout/stderr are tricky on Windows: by default the QTest result lines go to
  stderr OR are buffered such that they don't appear in plain `2>&1 | Tee-Object` capture.
  Use `-v2 -o <file>,txt` to write QTest output to a dedicated file.
- The Windows Credential Store backend works in QtTest GUI-less runs (QTEST_GUILESS_MAIN with
  QCoreApplication is sufficient). Cleanup in storeRetrieve uses a best-effort pre-test delete.

### Files added/modified
- `src/core/services/synccredentialsstore.h` (NEW, 67 lines)
- `src/core/services/synccredentialsstore.cpp` (NEW, 138 lines)
- `src/core/services/CMakeLists.txt` (MODIFIED: added .h/.cpp, gated QtKeychain link + include)
- `tests/core/test_synccredentialsstore.cpp` (NEW, 257 lines, 7 test cases)
- `tests/core/CMakeLists.txt` (MODIFIED: `add_qt_test(test_synccredentialsstore ...)` with VNOTE_TESTING + VNOTE_KEYCHAIN_AVAILABLE compile defs)

### Test result
`\\\
Totals: 7 passed, 0 failed, 1 skipped, 0 blacklisted, 169ms
\\\`
keychainUnavailableEmitsError SKIPs when VNOTE_KEYCHAIN_AVAILABLE is set (current build);
needs a separate `-DVNOTE_USE_KEYCHAIN=OFF` build to exercise the fallback path.

## [2026-05-13 08:42:00 UTC] T5: NotebookCoreService sync wrappers

### vxcore behavior observations
- `disableSync` on a never-enabled notebook returns `VXCORE_OK` (NOT `VXCORE_ERR_SYNC_NOT_ENABLED`). vxcore's `SyncManager::DisableSync` is fully idempotent and logs `Sync disabled for notebook: <id>` even when there was no active backend. This means T7 SyncService can call `disableSync` unconditionally on shutdown / config change without checking enable state first.
- `enable_with_credentials` against a `file://` URL with one seeded commit succeeds and the seed file appears at the workdir root after the clone-into-empty branch fires (consistent with T1+T2 finding about `vx_notebook` being skipped by the `has_user_files` walk).
- `setSyncCredentials` on an unknown notebook ID returns `VXCORE_ERR_NOT_FOUND` (4) — exactly as documented in vxcore.h.
- `enableSync` on a raw notebook returns `VXCORE_ERR_UNSUPPORTED` (14). Bundled-only sync constraint is enforced at the C API layer; UI must not offer the sync action for raw notebooks.

### Wrapper conventions adopted
- All 8 wrappers hold `QByteArray` locals from `QString::toUtf8()` to keep `constData()` valid through the C call. UTF-8 is mandatory per `libs/vxcore/AGENTS.md` Windows safety rules.
- Wrappers use `VXCORE_ERR_NOT_INITIALIZED` for the null-context guard. `VXCORE_ERR_INVALID_HANDLE` does NOT exist in `vxcore_types.h`.
- Wrappers do NOT log credentials JSON. `getSyncStatus` and `getSyncConflicts` write `QString::fromUtf8` then `vxcore_string_free` the C buffer.
- All 5 test cases pass in 3.19s. PAT-leak grep on full ctest output: NO MATCHES.

## [2026-05-13 08:55:00 UTC] T6: SyncWorker (worker-thread vxcore dispatcher)

### Pattern: avoiding duplicate-symbol when test seams need to be VNOTE_TESTING-gated
- The plan asked for `#ifdef VNOTE_TESTING`-gated seams + dual-compile (`add_qt_test SOURCES ...syncworker.cpp LINKS core_services`). That combination causes link-time duplicate symbols on Windows: core_services.lib already provides syncworker.obj (built without VNOTE_TESTING), and the test target would add a second copy (built with VNOTE_TESTING).
- Resolution adopted: keep test seams UNCONDITIONAL public API. The seam state is two `std::atomic<int>` members with sentinel `-1` ("disabled"), consumed-and-cleared via `exchange(-1, acq_rel)` in the next slot. Runtime cost in production: one relaxed atomic load per slot invocation.
- VNOTE_TESTING remains RESERVED on the test target (mirrors T4). Future code that genuinely needs different production-vs-test behaviour MUST either (a) put the gated code in the test .cpp itself (not the SUT), or (b) introduce a test-only sibling .cpp that the test target alone compiles.

### std::atomic seam pattern
```cpp
std::atomic<int> m_testHangMs{-1};   // -1 = no hang armed
std::atomic<int> m_testForceError{-1}; // -1 = no forced error

// In each slot, FIRST thing after Q_ASSERT thread check:
//   maybeHang();  // exchanges m_testHangMs with -1, sleeps if previous > 0
//   if (consumeForcedError(code)) { emit *_finished(... code ...); return; }
```
Consume-and-clear semantics mean tests must re-arm before each slot they want to affect.

### QueuedConnection meta-type registration
`qRegisterMetaType<VxCoreError>("VxCoreError")` is required ONCE before any signal carrying the type is delivered cross-thread. Done in SyncWorker constructor (idempotent — Qt deduplicates).

### Thread-identity test pattern
To prove a slot ran on the worker thread (not GUI), the test CAN legitimately use `Qt::DirectConnection` on a side-channel signal whose lambda calls `QThread::currentThreadId()` and stores it in an `std::atomic`. The lambda then runs on the *emitter's* thread (i.e., the worker thread), so the captured TID is the worker's. This is the one place where DirectConnection is correct — for measurement, not for handling.

### Test file must be force-added (T5 gotcha applies)
`tests/core/test_syncworker.cpp` is matched by `.gitignore` line 32 (`test_*`). Used `git add -f` to track.

### Test results
6 passed, 0 failed, 0 skipped, 780ms total.
- testHangNextOperationSeam measured 506ms ≥ 500ms target — confirms the maybeHang() seam path executes BEFORE the vxcore call.
## [2026-05-13 09:00 UTC] T9: NewNotebookInput.syncMethod injects flat ADR-8 keys

### Edits
- src/controllers/newnotebookcontroller.h:23-25: added `QString syncMethod = QStringLiteral("none");` to `struct NewNotebookInput` (matches existing in-class default style; default is "none").
- src/controllers/newnotebookcontroller.cpp (in `buildConfigJson`): when `input.syncMethod == "git"` we insert the FLAT vxcore notebook config keys per ADR-8: `configObj["syncEnabled"] = true; configObj["syncBackend"] = "git";`. `syncRemoteUrl` is intentionally NOT set — it stays empty until T14 bootstrap. When `syncMethod == "none"` neither key is added (cleaner-default semantics).
- `validateRootFolder()` semantics were NOT modified (verified by reading the diff). Empty-root rule remains for both `"none"` and `"git"` per ADR-7 (bootstrap is create-then-enable; no `allowNonEmptyRoot` bypass introduced).

### Tests
`tests/controllers/test_newnotebookcontroller.cpp` already existed (created in commit 1ac2506d). T9 added 3 new cases following the existing style:
- `syncMarkerInJsonForGit` — asserts `syncEnabled=true` + `syncBackend="git"` and that `"sync"`/`"syncRemoteUrl"` are absent.
- `noSyncMarkerInJsonForNone` — asserts `syncEnabled`/`syncBackend`/`sync` keys are all absent.
- `emptyRootStillEnforcedForGit` — proves the empty-root rule still rejects a non-empty bundled root when `syncMethod=="git"` (uses `validateAll` so future regressions in either `validateName` or `validateRootFolder` are caught).

All 17 tests PASS in 85 ms.

### Runtime gotchas worth recording for downstream tasks
- `test_newnotebookcontroller.exe` (and other tests linking `core_services`) needs **VTextEdit.dll** on PATH at run time, in addition to the Qt6 `bin` directory. Otherwise the exe exits immediately with `0xC0000135` (DLL not found) and no output. CTest masks this as a *Timeout* (no progress within 60s). Use:
  `C:\Program Files\PowerShell\7;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\bin\HostX64\x64;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\VC\VCPackages;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\TestWindow;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\bin\Roslyn;C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8 Tools\x64\;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\FSharp\Tools;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Team Tools\DiagnosticsHub\Collector;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\Extensions\Microsoft\CodeCoverage.Console;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\\x64;C:\Program Files (x86)\Windows Kits\10\bin\\x64;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\\MSBuild\Current\Bin\amd64;C:\Windows\Microsoft.NET\Framework64\v4.0.30319;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\;C:\Program Files\PowerShell\7;C:\Program Files (x86)\Common Files\Oracle\Java\java8path;C:\Program Files (x86)\Common Files\Oracle\Java\javapath;C:\Program Files\Microsoft SDKs\Azure\CLI2\wbin;C:\Windows\system32;C:\Windows;C:\Windows\System32\Wbem;C:\Windows\System32\WindowsPowerShell\v1.0\;C:\Windows\System32\OpenSSH\;C:\Program Files\Azure Dev CLI;C:\Program Files\GitHub CLI\;C:\Users\vmadmin\AppData\Local\Microsoft\WindowsApps;C:\Program Files\Microsoft SQL Server\Client SDK\ODBC\170\Tools\Binn\;C:\Program Files\Microsoft SQL Server\150\Tools\Binn\;Q:\.tools\dotnet;C:\ProgramData\chocolatey\bin;C:\Program Files (x86)\scala\bin;C:\Program Files\Go\bin;Q:\.tools\.npm-global;Q:\.tools\QuickBuild;C:\Windows\system32\config\systemprofile\AppData\Local\Microsoft\WindowsApps;C:\Program Files\Microsoft Dev Box Agent\Scripts;C:\WINDOWS\system32;C:\WINDOWS;C:\WINDOWS\System32\Wbem;C:\WINDOWS\System32\WindowsPowerShell\v1.0\;C:\WINDOWS\System32\OpenSSH\;C:\Program Files\nodejs\;C:\Program Files\dotnet\;C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\;C:\Qt\6.9.3\msvc2022_64\bin;C:\Program Files\PowerShell\7\;C:\Program Files\Git\cmd;C:\Program Files\Docker\Docker\resources\bin;C:\Users\tanle\scoop\apps\vscode\current\bin;C:\Users\tanle\scoop\apps\openssl\current\bin;C:\Users\tanle\scoop\apps\llvm\current\bin;C:\Users\tanle\scoop\apps\composer\current\home\vendor\bin;C:\Users\tanle\scoop\apps\python\current\Scripts;C:\Users\tanle\scoop\apps\python\current;C:\Users\tanle\AppData\Local\nvim-data\mason\bin;C:\Users\tanle\AppData\Local\nvim-data\mason\bin;C:\Users\tanle\AppData\Local\nvim-data\mason\bin;C:\Users\tanle\AppData\Local\nvim-data\mason\bin;C:\Users\tanle\scoop\shims;C:\Users\tanle\AppData\Local\Microsoft\WindowsApps;C:\Users\tanle\AppData\Local\cf;C:\Users\tanle\AppData\Roaming\npm;C:\php;Q:\work\xpay\devops\CLI\pkg\windows\amd64;C:\Users\tanle\AppData\Local\Google\Cloud SDK\google-cloud-sdk\bin;C:\Program Files\Git;C:\Users\tanle\AppData\Local\Android\Sdk\platform-tools;C:\Users\tanle\.dotnet\tools;C:\Users\tanle\.bun\bin;C:\Users\tanle\AppData\Local\nvim-data\mason\bin;C:\Qt\6.9.3\msvc2022_64\bin;C:\Program Files\Git\bin;C:\Users\tanle\.local\bin;C:\Users\tanle\.mai-agents\bin;C:\Users\tanle\.dotnet\tools;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\VC\Linux\bin\ConnectionManagerExe;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\vcpkg = "C:\Qt\6.9.3\msvc2022_64\bin;C:\Users\tanle\study\vnote\build\libs\vtextedit\src;" + C:\Program Files\PowerShell\7;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\bin\HostX64\x64;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\VC\VCPackages;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\TestWindow;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\bin\Roslyn;C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8 Tools\x64\;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\FSharp\Tools;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Team Tools\DiagnosticsHub\Collector;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\Extensions\Microsoft\CodeCoverage.Console;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\\x64;C:\Program Files (x86)\Windows Kits\10\bin\\x64;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\\MSBuild\Current\Bin\amd64;C:\Windows\Microsoft.NET\Framework64\v4.0.30319;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\;C:\Program Files\PowerShell\7;C:\Program Files (x86)\Common Files\Oracle\Java\java8path;C:\Program Files (x86)\Common Files\Oracle\Java\javapath;C:\Program Files\Microsoft SDKs\Azure\CLI2\wbin;C:\Windows\system32;C:\Windows;C:\Windows\System32\Wbem;C:\Windows\System32\WindowsPowerShell\v1.0\;C:\Windows\System32\OpenSSH\;C:\Program Files\Azure Dev CLI;C:\Program Files\GitHub CLI\;C:\Users\vmadmin\AppData\Local\Microsoft\WindowsApps;C:\Program Files\Microsoft SQL Server\Client SDK\ODBC\170\Tools\Binn\;C:\Program Files\Microsoft SQL Server\150\Tools\Binn\;Q:\.tools\dotnet;C:\ProgramData\chocolatey\bin;C:\Program Files (x86)\scala\bin;C:\Program Files\Go\bin;Q:\.tools\.npm-global;Q:\.tools\QuickBuild;C:\Windows\system32\config\systemprofile\AppData\Local\Microsoft\WindowsApps;C:\Program Files\Microsoft Dev Box Agent\Scripts;C:\WINDOWS\system32;C:\WINDOWS;C:\WINDOWS\System32\Wbem;C:\WINDOWS\System32\WindowsPowerShell\v1.0\;C:\WINDOWS\System32\OpenSSH\;C:\Program Files\nodejs\;C:\Program Files\dotnet\;C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\;C:\Qt\6.9.3\msvc2022_64\bin;C:\Program Files\PowerShell\7\;C:\Program Files\Git\cmd;C:\Program Files\Docker\Docker\resources\bin;C:\Users\tanle\scoop\apps\vscode\current\bin;C:\Users\tanle\scoop\apps\openssl\current\bin;C:\Users\tanle\scoop\apps\llvm\current\bin;C:\Users\tanle\scoop\apps\composer\current\home\vendor\bin;C:\Users\tanle\scoop\apps\python\current\Scripts;C:\Users\tanle\scoop\apps\python\current;C:\Users\tanle\AppData\Local\nvim-data\mason\bin;C:\Users\tanle\AppData\Local\nvim-data\mason\bin;C:\Users\tanle\AppData\Local\nvim-data\mason\bin;C:\Users\tanle\AppData\Local\nvim-data\mason\bin;C:\Users\tanle\scoop\shims;C:\Users\tanle\AppData\Local\Microsoft\WindowsApps;C:\Users\tanle\AppData\Local\cf;C:\Users\tanle\AppData\Roaming\npm;C:\php;Q:\work\xpay\devops\CLI\pkg\windows\amd64;C:\Users\tanle\AppData\Local\Google\Cloud SDK\google-cloud-sdk\bin;C:\Program Files\Git;C:\Users\tanle\AppData\Local\Android\Sdk\platform-tools;C:\Users\tanle\.dotnet\tools;C:\Users\tanle\.bun\bin;C:\Users\tanle\AppData\Local\nvim-data\mason\bin;C:\Qt\6.9.3\msvc2022_64\bin;C:\Program Files\Git\bin;C:\Users\tanle\.local\bin;C:\Users\tanle\.mai-agents\bin;C:\Users\tanle\.dotnet\tools;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\VC\Linux\bin\ConnectionManagerExe;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\vcpkg`
- For `ctest` runs, this PATH must already be in the user environment OR set via a CTest fixture. The orchestrator should consider adding `set_tests_properties(... PROPERTIES ENVIRONMENT "PATH=...")` in the root tests CMakeLists.

### Files touched
- modified: `src/controllers/newnotebookcontroller.h` (+5)
- modified: `src/controllers/newnotebookcontroller.cpp` (+8)
- modified: `tests/controllers/test_newnotebookcontroller.cpp` (+72)
- evidence: `.sisyphus/evidence/task-9-marker.txt`, `.sisyphus/evidence/task-9-empty-root.txt`

## [2026-05-13 09:13 UTC] T7: SyncService (GUI-thread facade owning a SyncWorker on a private QThread)

### Pattern: avoiding caching PATs in service members
- enableSyncForNotebook + updateCredentials use **lambda-captured one-shot connections** to handle the (store-credentials, then call worker) sequence. The PAT and credentials JSON live ONLY inside the lambda's closure between the call to `SyncCredentialsStore::storeCredentials` and the firing of `credentialsStored` (or `credentialsError`). After the worker is invoked, the lambda destructs and the capture is released. NO `QHash<QString,QString>` member is used to cache credentials.
- The one-shot pattern uses a heap-allocated `QMetaObject::Connection` for both the success (`credentialsStored`) and error (`credentialsError`) branches, with a shared `cleanup` lambda that disconnects both and frees the handles. Either branch fires exactly once per call.

### Worker -> service signal wiring
- Every `connect(m_worker, ..., this, ..., Qt::QueuedConnection)` MUST use QueuedConnection explicitly. The default AutoConnection would resolve to QueuedConnection for cross-thread signals anyway, but being explicit documents intent and protects against future refactors that move SyncService to another thread.
- `QThread::finished` -> `m_worker->deleteLater` ensures the worker is destroyed on its OWN thread (not the GUI thread that constructed it), avoiding QObject thread-affinity warnings during shutdown.

### In-progress flag thread-safety
- `QHash<QString, bool>` + `QMutex` (NOT `QHash<QString, std::atomic_bool>` because `std::atomic_bool` is not copyable and cannot live inside QHash). All writes happen on the GUI thread (from queued worker callbacks). Mutex protects reads from any thread that may call `isSyncInProgress`.

### Test seam pattern (T6 deviation continued)
- `testSetInProgress` is unconditional public API (NOT `#ifdef VNOTE_TESTING`-gated), mirroring the T6 SyncWorker decision. Reason: dual-compiling syncservice.cpp into both core_services and the test target produces duplicate-symbol link errors on Windows. `VNOTE_TESTING` remains reserved on the test target via `target_compile_definitions`.

### inProgressFlagToggles test - race condition observed
- triggerSync runs synchronously in the worker: emit syncStarted -> call vxcore (fails fast for unknown notebook) -> emit syncFinished. By the time the GUI thread processes the queued `syncStarted` event, `syncFinished` may already be in the queue and the in-progress flag will already be cleared after `processEvents`.
- Resolution: arm `syncService.worker()->testHangNextOperation(500)` BEFORE calling `triggerSyncNow`. SyncWorker::triggerSync emits `syncStarted` BEFORE calling `maybeHang()`, so syncStarted reaches the GUI thread immediately, then the worker sleeps 500ms before queueing syncFinished -- giving the test a clean window to assert the in-progress flag is true.
- This validates that `worker()` accessor is the right test seam access pattern per ADR-6.

### Files added/modified
- NEW: src/core/services/syncservice.h (~150 lines)
- NEW: src/core/services/syncservice.cpp (~290 lines)
- NEW: tests/core/test_syncservice.cpp (3 cases: roundtripViaServiceLocator, inProgressFlagToggles, testSetInProgressSeam)
- MODIFIED: src/core/services/CMakeLists.txt (added syncservice.{h,cpp})
- MODIFIED: tests/core/CMakeLists.txt (add_qt_test test_syncservice + VNOTE_TESTING)
- MODIFIED: src/main.cpp (registered SyncCredentialsStore + SyncService)

### Test results (ctest with VTextEdit DLL on PATH)
`Totals: 5 passed, 0 failed, 0 skipped, 0 blacklisted, 1784ms`
- roundtripViaServiceLocator: enable PAT against seeded bare repo via real ServiceLocator; enableFinished arrives with VXCORE_OK.
- inProgressFlagToggles: hang-seam-assisted observation of the per-notebook flag transition.
- testSetInProgressSeam: direct mutation seam used by T17.

## [2026-05-13 09:25 UTC] T10: NotebookSyncInfoDialog2 (view-only, controller stub deferred to T11)

### Pattern: dialog with deferred controller wiring
- Forward-declared `class NotebookSyncInfoController;` in BOTH the header (proper Qt forward-decl) AND inside the .cpp namespace block. The .cpp does not `#include` the controller header (does not exist yet — T11). Member `m_controller` stays nullptr in T10; every call site is wrapped with `if (m_controller) { /* T11 contract: ... */ }`.
- T11 needs only to: (a) `#include "controllers/notebooksyncinfocontroller.h"` in the dialog .cpp, (b) instantiate `m_controller = new NotebookSyncInfoController(m_services, m_notebookId, this);` in the constructor, (c) replace the stub comments inside the `if (m_controller)` blocks with actual `loadInitialData()` / `applyChanges()` / `disableSync()` calls. No header changes required.
- This pattern keeps the View buildable in isolation while preserving the MVC contract (dialog never calls vxcore directly; all persistence flows through controller → SyncService → vxcore).

### Danger button styling
- `WidgetsFactory` does NOT expose a `createDangerPushButton` helper today. Used the documented fallback: plain `new QPushButton` + `setProperty(PropertyDefs::c_dangerButton, true)` (the `DangerButton` Qt property already used elsewhere for QSS styling — see `src/widgets/propertydefs.cpp:9`).

### Standard button objectNames for test discoverability
- `setDialogButtonBox` creates the QDialogButtonBox lazily; standard button names must be set AFTER calling `setDialogButtonBox`. Pattern: iterate `box->button(QDialogButtonBox::Ok/Apply/Reset/Cancel)` and `setObjectName` with `okButton`/`applyButton`/`resetButton`/`cancelButton`. Reused this pattern for bootstrap-mode hide/show too — `box->button(StandardButton)->setVisible(...)`.

### Bootstrap mode dual signal (member + Qt property)
- Stored `bool m_bootstrapMode` AND `setProperty("bootstrapMode", value)` so tests can use either `findChild<NotebookSyncInfoDialog2*>()->property("bootstrapMode")` or the public method (none exposed; getter omitted per "no virtual" guideline + "match VNote density" — the property is the read API for tests).
- In bootstrap mode: hide Apply, Reset, and Disable Sync; relabel Ok button to `"Bootstrap"`. `refreshDirtyButtons` short-circuits when `m_bootstrapMode` is true so the (hidden) Apply/Reset don't get re-enabled.

### PAT field never prefilled (ADR-9 enforcement)
- `QLineEdit::Password` echo mode + placeholder text `"Leave blank to keep existing"`. NEVER call `setText` with retrieved PAT. Verified via `Select-String -Pattern "EchoMode|setText.*pat|prefill|stored|retrieve"` returning only the legitimate matches (the comment, the setEchoMode call, and an unrelated "stored credentials" string in the disable-confirm message — no PAT-leak path).

### SyncService signal subscription
- Direct connection from dialog (GUI thread) to SyncService (also GUI thread re-emits worker events). Default `Qt::AutoConnection` resolves to DirectConnection — no need to specify. Filter by notebookId in each slot. QObject auto-disconnects on destruction; no manual cleanup needed.

### Color-coding via inline setStyleSheet
- Used inline `setStyleSheet("color: #cf222e;")` (red), `#1f6feb` (blue), `#d29922` (orange) for state communication. Themes can override via QSS by matching on `objectName == "currentStateLabel"`. Cleared via `setStyleSheet(QString())` on Idle. Avoided `QPalette` to keep the rendering deterministic across the platform-specific palette quirks.

### Files added
- `src/widgets/dialogs/notebooksyncinfodialog2.h` (~130 lines)
- `src/widgets/dialogs/notebooksyncinfodialog2.cpp` (~290 lines)
- `src/widgets/CMakeLists.txt` MODIFIED: added `dialogs/notebooksyncinfodialog2.{cpp,h}` between `notebookinfowidget` and `notepropertiesdialog` (alphabetical order, mirrors the existing convention).

### Build verification
- `cmake --build build --target vnote --config Release` exits 0; vnote.exe links cleanly. No new warnings observed in the dialog's compilation unit.

### Notes for T11
- Controller class lives in `src/controllers/notebooksyncinfocontroller.{h,cpp}` per the plan's directory layout convention. Wire as: dialog ctor instantiates → controller emits `initialDataLoaded(QString name, QString remoteUrl, QString lastSyncIso)` connected to a dialog slot that populates the labels and snapshots `m_lastAppliedRemoteUrl`. Then T11 just uncomments the controller calls in the existing T10 stubs.


## [2026-05-13 09:55 UTC] T14: bootstrap orchestration in NewNotebookController (ADR-7)

### Bootstrap flow shape
- `bootstrapSync(notebookId, remoteUrl, pat, dialogParent)` is called AFTER `createNotebook` succeeded; it ONLY enables sync via SyncService and persists `syncRemoteUrl` (flat ADR-8 key) on success. It does NOT itself create the notebook.
- One-shot connection pattern: `auto conn = std::make_shared<QMetaObject::Connection>(); *conn = connect(...)`. The lambda filters by notebookId (since enableFinished can fire for any) and calls `QObject::disconnect(*conn)` before handling.
- The `rootPath` is captured in the lambda's closure BEFORE invoking `enableSyncForNotebook`, by reading from `listNotebooks()`. After `closeNotebook` the rootPath is unrecoverable from vxcore — so capturing first is essential for cleanup.

### Cleanup (ADR-2)
- Failure path: `closeNotebook` then retry-loop `QDir(rootPath).removeRecursively()` (up to 20 attempts, 100ms each) since SQLite/libgit2 can hold transient handles on Windows just after close. The successful happy path test cloned a remote and the failure test cleaned the dir on first attempt.
- Per-notebook cleanup verified by listNotebooks NOT containing the id and `QDir(rootPath).exists() == false`.

### KEY GOTCHA: vxcore `listNotebooks` JSON keys are camelCase
- The notebook id is exposed as `"id"` and the root folder as `"rootFolder"` (camelCase per vxcore JSON convention) — NOT `"root_path"`. `validateRootFolder` in NewNotebookController reads `"root_path"` (likely a bug, but never exercised by any test since no duplicate-path scenario triggers it). T14 uses the correct `"rootFolder"` key.
- Verified by reading `NotebookManager::ToNotebookConfig` in libs/vxcore/src/core/notebook_manager.cpp:242: `json["rootFolder"] = notebook.GetRootFolder(); json["type"] = notebook.GetTypeStr();` plus `NotebookConfig::ToJson` providing `id`.

### Modal contract (createBootstrapModal static helper)
- `setRange(0, 0)` for indeterminate spinner.
- `setCancelButton(nullptr)` REMOVES the cancel button entirely (verified by `findChild<QPushButton*>()` returning nullptr or no cancel-text). `setCancelButtonText("")` is the wrong API.
- `setMinimumDuration(0)` makes it appear immediately (default 4s would hide it during the typical short bootstrap).
- `WA_DeleteOnClose=false`; bootstrap callback owns lifetime via `modal->deleteLater()`.

### KEY GOTCHA: QTEST_MAIN + QApplication hangs under ctest pipe capture (Windows)
- The test_bootstrap binary runs to completion in ~2s when invoked directly (PowerShell pipe to file/Out-Null). Under ctest's stdout pipe driver on Windows, QtTest's default crash-handler hooks deadlock and the test appears to hang at the 60s ctest timeout.
- Workaround: pass `-nocrashhandler` to the test binary. tests/controllers/CMakeLists.txt overrides the auto-registered ctest command via `set_property(TEST test_bootstrap PROPERTY COMMAND $<TARGET_FILE:test_bootstrap> -nocrashhandler)`.
- This same workaround should be applied to any future GUI-mode (QApplication) tests that exhibit the same ctest hang.

### KEY GOTCHA: trailing async sync after bootstrap
- `bootstrapSync` calls `triggerSyncNow` after the success path emits `bootstrapSucceeded`. That trigger runs on the worker thread and may still be in flight when the test tears down vxcore — destroying the SyncManager mid-operation crashes inside libgit2.
- Test pattern: spy on `SyncService::syncFinished` and `wait` for it before destroying the vxcore context. Add a generous timeout (15s).

### Test layout
- `tests/controllers/test_bootstrap.cpp` uses `QTEST_MAIN` (needs QApplication for QProgressDialog construction) and links `Qt6::Widgets`.
- 3 test cases: `noCancelButton` (modal contract), `happyPath` (real bare-repo seed + clone-into-empty), `authFailureCleanup` (bad URL → cleanup verified).
- Force-added per the T5 gotcha: `git add -f tests/controllers/test_bootstrap.cpp`.

## [2026-05-13 10:20 UTC] T12: SyncConflictDialog2 (per-file resolution view)

### Layout choice: plain QDialog vs ScrollDialog
- Plan explicitly mandated : public QDialog (NOT ScrollDialog) so we built our own
  QScrollArea inside a QVBoxLayout root rather than reusing the project's ScrollDialog
  base. The advantage is we own the inner content widget directly and can set the
  required conflictScrollArea objectName on the QScrollArea itself for test discovery.

### ObjectName scheme (test discoverability)
- conflictScrollArea (QScrollArea), conflictRow_<i> (per-row QWidget),
  adio_<i>_local|remote|both (per-row QRadioButton triple),
  okButton / cancelButton (QDialogButtonBox standard buttons).
- Row indices align lock-step with the m_conflictFiles QStringList passed to the
  constructor; T13 controller and tests can findChild() rows + radios by index alone.

### Default selection
- localRb->setChecked(true) on every row at construction. QButtonGroup is
  setExclusive(true) (default but stated explicitly), so one (and only one) radio
  is always selected per row. resolutions() falls back to continue if a group
  unexpectedly has no checked button (defensive: won't insert an empty value).

### QButtonGroup parenting nuance
- The three QRadioButton instances are parented to the row QWidget (so they get
  destroyed when the row is destroyed by content -> scrollArea ownership).
  The QButtonGroup is parented to 	his (the dialog) so its lifetime tracks the
  dialog. This avoids the order-of-destruction pitfall where a row is destroyed
  before its QButtonGroup tries to disconnect from its (now-dangling) buttons.

### Cancel semantics (per plan ADR)
- Cancel directly invokes QDialog::reject() without going through onAccepted,
  guaranteeing esolutionsChosen is NEVER emitted on Cancel. The contract is
  documented in both the .h class comment and .cpp connect() comment.

### Files added
- src/widgets/dialogs/syncconflictdialog2.h (~78 lines)
- src/widgets/dialogs/syncconflictdialog2.cpp (~196 lines)
- src/widgets/CMakeLists.txt MODIFIED: added the two source entries between
  dialogs/sortdialog.{cpp,h} and the next entry, alphabetical-ish.

### Build
- Incremental build via cmake --build build --target vnote --config Release exits 0.
  Hit two transient Windows file-lock errors during back-to-back link attempts
  (LNK1104 on git2.lib, then permission-denied on pdfviewwindow.cpp.obj). These
  cleared after waiting for stale parallel ninja processes to drain - they were
  NOT caused by the new files. The dialog itself compiles cleanly with zero warnings:
  [357/435] Building CXX object src\CMakeFiles\vnote.dir\widgets\dialogs\syncconflictdialog2.cpp.obj.

## [2026-05-13 10:25 UTC] T11: NotebookSyncInfoController + dialog wire-up

### Pattern: dialog ↔ controller wiring without QObject re-parent dance
- Dialog constructor creates the controller with itself as parent, then connects \dataLoaded(name, url, lastSyncIso)\ to a lambda that populates the labels and snapshots \m_lastAppliedRemoteUrl\. \controller->loadInitialData()\ is called LAST in the constructor so the lambda is already wired before the synchronous emit fires.
- The lambda updates \m_remoteUrlEdit\ via \setText\, which triggers \	extChanged → onFieldEdited → refreshDirtyButtons\ - but the lambda then sets \m_lastAppliedRemoteUrl\ and calls \efreshDirtyButtons()\ AGAIN to clear the false-dirty state. Without this second refresh, the Apply button would light up immediately after \loadInitialData()\ even though no user edits happened.

### Apply path: URL-write outcome combined with PAT-update result
- \pplyChanges(newUrl, newPat)\ does the URL write SYNCHRONOUSLY (read JSON → mutate flat \syncRemoteUrl\ → updateNotebookConfig), captures the success bool in \m_pendingUrlWriteOk\, then either fires \SyncService::updateCredentials\ (async) or emits \pplyComplete\ directly when no PAT was provided.
- \m_pendingPatUpdate\ flag prevents the controller's connected \onCredentialsSetFinished\ slot from emitting \pplyComplete\ for PAT updates initiated by SyncService consumers OTHER than this controller (e.g., another dialog instance for a different notebook). The slot also filters by \m_notebookId\ for the same reason.

### CMakeLists deviation from plan spec (T6/T7 deviation continued)
- The plan spec asked for compiling \syncservice.cpp\ + \syncworker.cpp\ + \synccredentialsstore.cpp\ directly into the test target. Doing that:
  1. Triggers the QtKeychain compile gate (synccredentialsstore.cpp uses \#ifdef VNOTE_KEYCHAIN_AVAILABLE\ and falls into the \#else\ branch where \qWarning() << ...\ requires \<QDebug>\ which the file doesn't include - because in the production library build the macro is always set).
  2. Produces duplicate-symbol link errors (per T6/T7 learnings).
- Resolution: the test ONLY compiles \
otebooksyncinfocontroller.cpp\ directly (it lives in vnote target, not core_services, so it must be compiled into the test). All sync services come from \core_services\ library link. \VNOTE_TESTING\ remains reserved on the test target.

### Test pattern: pre-enable sync via SyncService before exercising controller
- Both test cases first call \syncService.enableSyncForNotebook(...)\ against a real seeded bare repo so the notebook is in a sync-enabled state. The controller assumes it operates on an already-enabled notebook (per the dialog flow, which only opens for sync-enabled notebooks). Tests skip via QSKIP if \VXCORE_ERR_UNKNOWN\ from the keychain backend (CI-friendly, mirrors test_syncservice.cpp's skip pattern).
- The disable test verifies that SyncCredentialsStore::credentialsDeleted fires AFTER SyncService::disableFinished (T7's wired follow-up). The controller emits its own \disableComplete(notebookId)\ for the dialog to react to (e.g., close the dialog).

### Files touched
- NEW: \src/controllers/notebooksyncinfocontroller.h\ (~115 lines)
- NEW: \src/controllers/notebooksyncinfocontroller.cpp\ (~150 lines)
- NEW: \	ests/controllers/test_notebooksyncinfocontroller.cpp\ (~325 lines, 2 cases)
- MODIFIED: \src/widgets/dialogs/notebooksyncinfodialog2.cpp\ - replaced T11 stubs (3 sites: ctor, Disable, Apply/OK)
- MODIFIED: \src/controllers/CMakeLists.txt\ - added controller sources alphabetically (after managenotebookscontroller)
- MODIFIED: \	ests/controllers/CMakeLists.txt\ - registered test target with VNOTE_TESTING
## [2026-05-13 10:50 UTC] T13: SyncConflictController (dialog ↔ SyncService orchestrator)

### Pattern: non-blocking dialog with one-shot syncFinished follow-up
- presentConflicts() spawns the dialog with `dlg->setAttribute(Qt::WA_DeleteOnClose); dlg->open();`. `open()` shows the dialog as window-modal and returns IMMEDIATELY (unlike `exec()` which spins a nested event loop). This is critical: SyncService progress signals must keep flowing on the GUI thread while the user picks resolutions.
- The OK→syncFinished bridge is installed INSIDE the resolutionsChosen lambda (NOT at presentConflicts time). Reasoning: if the user clicks Cancel, the OK lambda never fires, so the syncFinished one-shot is never registered → guarantees `conflictsAbandoned` ≠ `conflictsResolved` paths can never accidentally interleave even if a stray syncFinished arrives from another notebook.
- One-shot disconnection uses the standard `auto conn = std::make_shared<QMetaObject::Connection>(); *conn = connect(...)` pattern, with the lambda calling `QObject::disconnect(*conn)` on its first matching firing. The shared_ptr keeps the Connection handle alive until the lambda is invoked.

### KEY GOTCHA: QSignalSpy::wait() returns FALSE for already-fired signals
- `QDialog::rejected` is emitted SYNCHRONOUSLY during `cancelBtn->click()` → `QDialog::reject()` → `done(Rejected)` → `emit rejected()`. The controller's lambda is connected DirectConnection (default for same-thread), so `conflictsAbandoned` is also emitted SYNCHRONOUSLY before `click()` returns.
- This means by the time the test calls `abandonSpy.wait(5000)`, the signal has ALREADY arrived. `QSignalSpy::wait()` waits for the NEXT emission and returns FALSE if no NEW signal arrives within the timeout. Even though `abandonSpy.count() == 1`, `wait()` returns FALSE.
- Resolution: for synchronous emission paths, do NOT call `wait()`. Just `processEvents` to drain any queued events and then assert `count == 1` directly. For asynchronous paths (e.g., the OK route which goes through the worker thread before syncFinished fires), `wait()` is correct.
- Diagnostic technique that confirmed this: connect a side-channel `bool rejectedFired = true` lambda to `&QDialog::rejected` and qDebug both `rejectedFired` and `abandonSpy.count()` AFTER the click. If both are true/1 but the test still fails, the bug is in `wait()` not the controller.

### KEY GOTCHA: stale dialogs in topLevelWidgets() between tests
- `WA_DeleteOnClose` schedules deletion via `deleteLater()` which runs on the next event loop tick. If a test ends with a dialog accepted/rejected and the next test starts before the deferred-delete event fires, `QApplication::topLevelWidgets()` STILL contains the stale dialog. `findChild<QPushButton*>("cancelButton")` on the stale dialog returns a button whose connections point to the prior test's (now destroyed) controller — clicks become silent no-ops.
- Two-layer defense:
  1. `findOpenDialog()` filters by `isVisible()` (accepted/rejected dialogs are hidden by `done()`).
  2. After dialog discovery, `QCOMPARE(dlg->result(), 0)` asserts the dialog has NOT been finalized (a stale dialog has `result() == Accepted` (1) or `Rejected` (0 too — wait, Rejected is 0, but Accepted is 1, so a Rejected stale would slip through; isVisible() catches it).
  3. `drainPendingEvents()` helper at end of every test forces deferred-delete events to run via `sendPostedEvents(nullptr, QEvent::DeferredDelete)`.
- `drainPendingEvents()` pattern (reusable):
  ```cpp
  for (int i = 0; i < 20; ++i) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  }
  ```

### CMakeLists deviation continues (T6/T7/T11)
- Test compiles ONLY `syncconflictcontroller.cpp` + `syncconflictdialog2.cpp` directly into the test target. Sync services come via `core_services` link. Dual-compiling `syncservice.cpp`/`syncworker.cpp`/`synccredentialsstore.cpp` into the test target produces duplicate-symbol link errors AND breaks the `VNOTE_KEYCHAIN_AVAILABLE` compile gate (per T11 learnings).
- `VNOTE_TESTING` remains reserved on the test target via `target_compile_definitions` per ADR-6.

### Test layout
- `tests/controllers/test_syncconflictcontroller.cpp` uses `QTEST_MAIN` (NOT `QTEST_GUILESS_MAIN`) because `QDialog::open()` requires `QApplication` (not just `QCoreApplication`). Same pattern as `test_bootstrap.cpp`.
- 2 test cases: `okPathRoutesResolutions` (mouseClick OK → resolveConflicts → wait for syncFinished → conflictsResolved) and `cancelPath` (click cancel → conflictsAbandoned synchronously, NO sync triggered).
- `-nocrashhandler` ctest property applied (per T14 learnings) to avoid the QtTest+ctest pipe-driver deadlock on Windows.
- Force-added per the T5 gotcha: `git add -f tests/controllers/test_syncconflictcontroller.cpp`.

### Files added/modified
- NEW: `src/controllers/syncconflictcontroller.h` (~75 lines)
- NEW: `src/controllers/syncconflictcontroller.cpp` (~85 lines)
- NEW: `tests/controllers/test_syncconflictcontroller.cpp` (~210 lines, 2 cases)
- MODIFIED: `src/controllers/CMakeLists.txt` — added syncconflictcontroller.{h,cpp} after notebooksyncinfocontroller (alphabetical-ish placement, mirrors the T11 convention)
- MODIFIED: `tests/controllers/CMakeLists.txt` — registered `test_syncconflictcontroller` with `Qt6::Widgets` link, `-nocrashhandler` override, `VNOTE_TESTING` macro

### Test results
- ctest exit 0; 100% tests passed; 0.73 sec.
- Direct binary: `Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 587ms`.
- vnote target builds clean with new syncconflictcontroller.cpp included.
## [2026-05-13 11:10 UTC] T15: NotebookExplorer2 Sync title-bar button + menu entry + auto-open after Git creation

### Files modified
- `src/widgets/notebookexplorer2.h` (+27 lines): forward decls (`QToolButton`, `QAction`, `NotebookSyncInfoDialog2`, `SyncService`); members `m_syncButton`/`m_syncInfoAction`; private slots `onSyncButtonClicked`/`onSyncInfoActionTriggered`/`updateSyncButtonState`; gated `testTriggerNewNotebookCreated` test seam.
- `src/widgets/notebookexplorer2.cpp` (+~120 lines): include `core/services/syncservice.h` + `widgets/dialogs/notebooksyncinfodialog2.h`; ctor wires SyncService::sync*/enable/disable signals + own `currentNotebookChanged` to `updateSyncButtonState`; setupTitleBar adds Sync button (objectName=`syncButton`); setupTitleBarMenu adds `Notebook Sync Info...` action (objectName=`syncInfoAction`); `newNotebook()` reads `dialog.getSelectedSyncMethod()` and pops `NotebookSyncInfoDialog2` in bootstrap mode for `git`; new slot bodies + test seam.

### Icon caveat
- `git-sync.svg` does NOT exist under `src/data/core/icons/`. Used **`apply_editor.svg`** as a placeholder for the Sync title-bar button until a dedicated SVG ships. TODO logged inline as a code comment near the `addActionButton` call.

### Pattern: signal-only state derivation
- `updateSyncButtonState()` is the single source of truth. It derives enabled/tooltip from: `currentNotebookId()` empty → "No notebook selected"; `BufferService::isNotebookBundled` false → "Sync requires a Bundled notebook with sync enabled"; `SyncService::isSyncEnabled` false → same; `isSyncInProgress` true → "Sync in progress…"; otherwise → "Sync now". The menu entry mirrors the sync-enabled gate but ignores in-progress (the dialog is safe to open during sync).
- All five SyncService lifecycle signals (`syncStarted`/`syncFinished`/`syncFailed`/`enableFinished`/`disableFinished`) re-trigger `updateSyncButtonState`. The slot is cheap (a few QHash/JSON lookups) so polling-style refresh is fine.

### Pattern: post-creation auto-open without re-using exec()
- After `newNotebook()`'s exec returns Accepted, the code allocates `NotebookSyncInfoDialog2` heap, sets `WA_DeleteOnClose`, `setBootstrapMode(true)`, `open()` (window-modal, non-blocking). This matches T13's conflict-dialog pattern and avoids nested-event-loop traps. The dialog's Apply handler (T11) calls `NewNotebookController::bootstrapSync` because of the bootstrap-mode flag.

### Test seam (ADR-6)
- `testTriggerNewNotebookCreated(notebookId, syncMethod)` is gated by `#ifdef VNOTE_TESTING` and lives inside the `.cpp`. It exercises the EXACT code path used by `newNotebook()` after Accept, factored out to allow a future test to drive the auto-open without mocking `NewNotebookDialog2`.

### Test file deferred (per plan IF clause)
- `tests/widgets/test_notebookexplorer2_sync.cpp` was NOT created. Rationale: NotebookExplorer2 instantiation requires a fully-populated ServiceLocator (ConfigMgr2 + ConfigCoreService + NotebookCoreService + BufferService + ThemeService + NavigationModeService + HookManager + SyncService + SyncCredentialsStore + WidgetConfig + SessionConfig) plus QApplication + a parent QWidget context resembling MainWindow. Constructing all of this in isolation is substantial test infrastructure work that the plan itself flags as optional ("IF the test setup is infeasible … substitute with manual review"). Manual QA via F3 (launch app + observe button states across notebook types) is the agreed substitute. See issues.md.

### Build evidence
- `cmake --build build --target vnote --config Release` exits 0; vnote.exe links cleanly. No new warnings observed in the modified compilation units. Saved to `.sisyphus/evidence/task-15-sync-state.txt`.

### Stash dance for clean build verification
- Discovered uncommitted T17 partial work in working tree (`src/main.cpp` adding broken `QObject::connect(&app, signal, lambda, conn)` call without context object; `src/core/services/syncservice.{h,cpp}` adding `shutdown()`; `tests/widgets/CMakeLists.txt` adding `test_syncservice_lifecycle` target). The main.cpp connect call fails to compile because Qt5/6 requires a context object when passing a connection-type with a free lambda.
- T15 verification stashed only the three T17-related source files via `git stash push -- src/main.cpp src/core/services/syncservice.{h,cpp}` (NOT the CMakeLists which is harmless additive), built, then `git stash pop` to restore for the next agent.

### Files NOT touched (per scope)
- Did not touch `src/main.cpp`, `src/core/services/syncservice.{h,cpp}`, or `tests/widgets/CMakeLists.txt` — those are T17 territory. T15 commit only includes `src/widgets/notebookexplorer2.{h,cpp}`.
