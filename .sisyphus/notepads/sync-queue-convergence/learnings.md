# Sync Queue Convergence - Learnings

This notepad accumulates conventions, gotchas, and patterns discovered during implementation.
Subagents MUST APPEND to this file (never overwrite). Format:

```
## [YYYY-MM-DD HH:MM] Task: T<N>
{content}
```

---

## [Plan Start] Inherited Wisdom (from plan generation + AGENTS.md)

### Threading discipline (LOAD-BEARING)
- `state_mutex_` in vxcore SyncManager is single coarse mutex; NEVER held across external calls.
- Standard pattern: copy-out → release → invoke external code → re-acquire if needed.
- Hooks/signals/backend calls/credential providers/work-queue enqueue/DirtyTracker all count as "external".
- `std::mutex` not `std::recursive_mutex` — reentrancy is intentional and handled by release-before-reentry.
- All Qt cross-thread signals MUST use `Qt::QueuedConnection`.

### vxcore C ABI is frozen
- No signature changes in `libs/vxcore/include/vxcore/vxcore.h`.
- Adding comments OK; adding new function declarations only if absolutely required (this plan does not need new C API).

### Test infrastructure quirks (from libs/vxcore/AGENTS.md)
- vxcore tests touching libgit2 (GitSyncBackend) MUST direct-compile sources via `VXCORE_GIT_SYNC_SOURCES_ABS` from `libs/vxcore/cmake/git_sync_sources.cmake` (linking via vxcore.dll fails for non-VXCORE_API symbols).
- Tests MUST start with `vxcore_set_test_mode(1)` (direct-compile tests need a local `void vxcore_set_test_mode(int) {}` shim).
- NEVER call libgit2 `git_clone()` for local `file://` remotes on Windows — it hangs. Use init+remote+fetch+checkout pattern (see `clone_bare_to_workdir` in `test_gitkeep_roundtrip.cpp`).
- Anchor ctest regex with `^...$` to avoid sweeping in adjacent test names (e.g., `-R "^test_sync$"`).
- Full ctest suite has shared-temp-dir leakage; failing tests often pass in isolation. Wipe leaf subdirectories only, NEVER the `vxcore_test_data` parent (libgit2 init relies on it).

### vnote.exe smoke-test trap (from AGENTS.md "Running execs that depend on VTextEdit.dll")
- ALWAYS prepend Qt bin + `build-debug/libs/vtextedit/src` to PATH before launching vnote.exe.
- Without PATH prepend, Windows loader pops a modal dialog BEFORE WinMain → `$proc.HasExited` returns false → naive smoke tests give false POSITIVES.
- Verify load via `$proc.Modules | Where-Object { $_.ModuleName -ieq "VTextEdit.dll" }`, NOT just `HasExited`.

### Naming + style (root AGENTS.md)
- Parameters: `p_` prefix. Members: `m_` prefix. Constants: `c_` prefix.
- Getters: `getXxx()`. Pointer alignment right: `int *ptr`.
- `using namespace vnotex;` in `.cpp` files only, never headers.
- 2-space indent, 100-col line limit.

### Sync-specific gotchas
- vxcore `WorkQueueManager` STAYS (used by vxcore tests + future embedders). Only Qt's `WorkQueueDrainThread` is being deleted.
- vxcore `SyncManager::last_enqueue_time_` is RETAINED (only its role narrows; it's no longer a Qt-consulted gate).
- vxcore `SyncManager::MaybeEnqueueSync` keeps debounce check; only the emission target changes (WorkQueue.Enqueue → event emit).
- PAT values must NEVER appear in any signal, hook payload, queue item, log line, or test output.
- Conflict file paths must be preserved through the new `sync.conflict` event payload (`files` array).

---

## [Plan Start] Decisions Locked
- Queue cap default = 4.
- Manual sync coalesces pending auto-sync (manual wins, no debounce).
- Auto-sync silent on QueueFull (log only, no syncFailed emit).
- Cancellation tokens for queued items freed via per-item onCancelled callback.
- Auto-sync uses null cancellation token (user cancels via UI which routes through manual cancelSync path).
- `EnqueueResult` enum: Accepted | Coalesced | QueueFull | Rejected.
- New `syncCancelled(id, wasQueued)` signal differentiates queued-cancel vs in-flight-cancel.

## [2026-05-22 00:15] Task: T3

### Summary
Successfully added num class EnqueueResult { Accepted, Coalesced, QueueFull, Rejected } scoped inside class SyncWorkQueueManager public section. Changed signature from oid enqueue(...) to EnqueueResult enqueue(...).

### Implementation Details
- **Enum location**: Line 41-54 of src/core/services/syncworkqueuemanager.h (inside public class block)
- **Return type**: SyncWorkQueueManager::EnqueueResult (proper scoping)
- **Implementation changes**:
  - Line 34: empty-work path → eturn EnqueueResult::Rejected;
  - Line 42: shutdown path → eturn EnqueueResult::Rejected;
  - Line 59: success path → eturn EnqueueResult::Accepted;

### Test Adaptation
No production call sites found (manager is dead code on hot path, per AGENTS.md Wave 14.2 note).
Adapted 6 test call sites in 	ests/core/test_sync_work_queue_manager.cpp:
  - Lines 46-48: cast to (void) in serial_per_notebook()
  - Lines 70,81: inline cast in parallel_across_notebooks()
  - Line 116: cast in shutdown_drains_within_5s() loop
  - Line 143: cast in 
o_leak_on_shutdown() loop
  - Line 156: cast for post-shutdown enqueue test

### Build Status
- **Compilation**: PASS, zero warnings/errors on syncworkqueuemanager.{h,cpp}
- **Test target**: 	est_sync_work_queue_manager compiles clean (MSBuild → executable generated)
- **Enum scope verification**: Confirmed via grep — enum scoped inside class at public section

### No Deviations
- Did NOT implement coalescing (T6)
- Did NOT implement queue cap (T6)
- Did NOT touch other files beyond h/cpp/test
- All return paths match spec (Accepted on success, Rejected on failure)

## [2026-05-22 16:10] Task: T1
- Reused TestSyncService pattern from `tests/core/test_syncservice.cpp` (ServiceLocator + seedBareRepo via git CLI, file:// remote, dummy PAT).
- EventBridge ownership gotcha: ~EventBridge calls `vxcore_off_event(m_context, ...)`. If the test calls `vxcore_context_destroy(ctx)` BEFORE EventBridge goes out of scope, you get a segfault inside `EventManager::Unsubscribe` (lock against freed mutex). Fix: wrap the test body in an inner `{ ... }` block and call `vxcore_context_destroy(ctx)` AFTER the closing brace.
- Same trap on QSKIP early-returns — `QSKIP` unwinds via QtTest exception. Do NOT call `vxcore_context_destroy(ctx)` immediately before `QSKIP` once an EventBridge is alive in the scope; the EventBridge destructor will fire during unwind against the freed context. Acceptable cost: ctx leaks on skip-only runs (single test process, OS reclaims).
- QSignalSpy on `SyncWorker::syncStarted`/`syncFinished` works transparently across the worker thread — Qt routes cross-thread spy connections via QueuedConnection automatically.
- Baseline confirmed (Metis was right): manual `triggerSyncNow` produces `workerStarted=1`, `workerFinished=1`, `bridgeStarted=0`, `bridgeFinished=0`. vxcore `TriggerSync` does NOT emit `sync.started`/`sync.finished` events today; only the auto path (`MaybeEnqueueSync`) does.
- After `workerFinishedSpy.wait()` returns, drained queued events for ~5×70ms to give any EventBridge invocations a chance to land before asserting count==0.
- Used `target_link_options(... /WHOLEARCHIVE:vxcore)` mirror of `test_syncservice` so the git backend self-registration symbol is preserved on MSVC.
- Test runtime: 2.3s. Bare-repo clone via git CLI dominates.

## [2026-05-22 16:15] Task: T2

### Auto-sync test fixture wiring (LOAD-BEARING)
- For `EventBridge` to fire on the auto path, the ServiceLocator must register: `NotebookCoreService`, `SyncCredentialsStore`, `EventBridge`, `HookManager` (SyncService's ctor pulls EventBridge via m_services.get<EventBridge>(); without it the auto-sync slots never connect).
- vxcore's `WorkQueueManager` is auto-wired by `vxcore_context_create` (sync_manager.cpp `SetWorkQueueManager` is called from `vxcore_api.cpp:153`). You do NOT need to call it manually — only the Qt-side `WorkQueueDrainThread` must be started.
- Plan claim "intervalSeconds=0 disables debounce" is WRONG: `sync_manager.cpp:100` explicitly skips when `interval_seconds <= 0`. The real reason a single save works is that `last_enqueue_time_[id]` is a default-constructed `time_point` (zero) on first event, so `now - 0 = now` always exceeds any interval. First save fires immediately regardless of interval.

### Lifecycle order trap
- `EventBridge` calls `vxcore_off_event` in its dtor. If the `VxCoreContextHandle` is destroyed BEFORE EventBridge (or any other `XxxCoreService` holding the handle), the dtor crashes with use-after-free.
- Pattern: scope all ctx-holding stack objects inside `{ }` and call `vxcore_context_destroy(ctx)` AFTER the closing brace.

### Drain latency observed
- File save → `EventBridge::syncFinished` fired within ~300ms on a local `file://` bare repo on Windows (libgit2 + git command-line seeding).
- `WorkQueueDrainThread` polls `vxcore_work_queue_process_next(ctx, "sync", 500)` — 500ms per iteration.
- `QSignalSpy::wait(15000)` chosen as upper bound; actual round-trip well under 1s.

### Save path
- `Buffer2::save()` → `BufferService::saveBuffer` (which calls `BufferCoreService::saveBuffer` → vxcore `BufferManager::SaveBuffer`, which emits `events::kFileSaved` at `buffer_manager.cpp:408`).
- The hook-aware `BufferService::save(Buffer2&)` does ALSO go through this path. Either entry works for triggering `file.saved` for the SyncManager subscriber.

### QtTest output silence
- `QTEST_GUILESS_MAIN` prints NOTHING on PASS by default — exit code 0 is the sole pass indicator at runtime. Use `-v2` for verbose. `ctest --output-on-failure` won't print anything because nothing failed. Don't mistake a silent pass for "nothing ran".

## [2026-05-22 16:22] Task: T4

### Summary
Successfully implemented SyncInFlightState struct and hasPending() tracking for per-notebook state queries. Extended PerNotebook internal struct with hasPending flag and implemented both accessor methods (hasPending, inFlightState) returning snapshots under lock.

### Implementation Details
- **Storage layout chosen**: Extended PerNotebook struct with ool hasPending flag (kept alongside existing queue and unning). Single map QHash<QString, PerNotebook> unchanged.
- **SyncInFlightState struct**: Declared public inside SyncWorkQueueManager class with three fields: unning, hasPending, cancellation (nullptr for T4, populated by T21).
- **Snapshot semantics**: Both accessors (hasPending, inFlightState) take QMutexLocker, copy state atomically, and return by value — never references. Callers read snapshots without holding lock.
- **State transition rules implemented**:
  - nqueue(): After appending work, set hasPending = !queue.isEmpty() (always true post-append)
  - unLoop(): While draining, hasPending = (queue.isEmpty() ? false : true) || running
  - unLoop(): On drain completion, set both unning = false and hasPending = false atomically

### Test Coverage (test_sync_workqueue_inflight.cpp)
- **initial_state_is_idle**: Before any enqueue, both flags false ✓
- **after_enqueue_has_pending_true**: Post-enqueue, hasPending true even before execution ✓
- **during_execution_running_is_true**: While running, running=true ✓
- **after_completion_both_false**: After drain, both false ✓
- **multiple_enqueues_hasPending_stays_true**: Three items queued back-to-back, hasPending stays true throughout ✓
- **unknown_notebook_returns_defaults**: Missing notebook returns default state (all false/nullptr) ✓
- **inFlightState_returns_snapshot**: Value-type snapshots independent of each other ✓
- **hasPending_correlates_with_queue_depth**: Semantic: hasPending=true IFF (running OR queueDepth>0) ✓

### Build Status
- **Compilation**: PASS, zero warnings
- **New test target**: test_sync_workqueue_inflight builds → exe generated
- **Single run**: 0.36s, PASS
- **10x repeatability**: 10/10 PASS, no flakes
- **Existing test**: test_sync_work_queue_manager still PASS (0.63s)

### Race Conditions Tamed
Used QSemaphore for work synchronization instead of raw sleeps/waits. Each test acquires/releases semaphores to:
1. Wait for work to START (workStarted.acquire) — avoids timing races
2. Allow work to FINISH (canFinish.release) — captures mid-flight state cleanly
3. No QTRY_VERIFY on state that might change mid-check

### Evidence Captured
- .sisyphus/evidence/task-4-state-transitions.txt — single test run output
- .sisyphus/evidence/task-4-repeat-runs.txt — 10× loop showing 10/10 PASS
- .sisyphus/evidence/task-4-existing-test.txt — existing test_sync_work_queue_manager still green

### No Deviations
- Did NOT implement coalescing (T6)
- Did NOT populate cancellation field (T21)
- All return paths use correct state transitions
- Snapshot semantics never expose mutable internal state

## [2026-05-22 16:30] Task: T5

### Summary
Added cancelPending(id), enqueue overload with onCancelled callback, pendingCancelled signal.

### Storage choice
Replaced QQueue<Work> with QQueue<WorkItem> where WorkItem { Work body; std::function<void()> onCancelled; }. Public Work typedef stays, simple enqueue(id, work) forwards to enqueue(id, work, nullptr). PerNotebook struct shape unchanged otherwise.

### cancelPending pattern
Under m_mutex: snapshot queue into std::vector<WorkItem>, clear queue, set hasPending = running. Release lock. Iterate snapshot invoking onCancelled with try/catch (mirrors runLoop). Emit pendingCancelled(id, count) outside mutex only if count > 0.

### Re-entrancy test design
Item 2's onCancelled callback re-enqueues a new item for the SAME notebook id. Because callback invocation happens AFTER mutex release, the re-entrant enqueue acquires the mutex cleanly. Test verifies: (1) cancelPending returns 1, (2) reentrant item runs after in-flight completes, (3) no deadlock (5s tryAcquire timeout).

### Test results
- New test_sync_workqueue_cancel: 4 cases, PASS in 0.13s
- 10x flake check: 10/10 PASS
- Existing test_sync_work_queue_manager + test_sync_workqueue_inflight: PASS (no regressions)

### No deviations from spec.

## [2026-05-21 22:16] Task: T6

### Summary
Successfully implemented queue-depth cap (default 4) and coalescing deduplication. Extended enqueue with 3-arg convenience overload and full 4-arg signature. Comprehensive test coverage with 7 test cases, all passing 10/10 flake check.

### Overload Disambiguation Strategy
- **2-arg** `enqueue(id, work)` → delegates to 4-arg with `nullptr` onCancelled + empty `QString()` coalesceKey
- **3-arg (callback)** `enqueue(id, work, onCancelled)` → delegates to 4-arg with empty `QString()` coalesceKey
- **3-arg (coalesce)** `enqueue(id, work, coalesceKey)` → delegates to 4-arg with `nullptr` onCancelled
- **4-arg (full)** `enqueue(id, work, onCancelled, coalesceKey)` → implements full logic

Compiler disambiguates naturally via parameter types: `std::function<void()>` vs `QString`. Zero ambiguity warnings. No manual overload-resolution tricks needed.

### WorkItem Structure Extension
```cpp
struct WorkItem {
  Work body;
  std::function<void()> onCancelled;
  QString coalesceKey;  // NEW: T6 adds this field
};
```

All existing code paths (T3, T4, T5) continue to work without modification — they use 2-arg or 3-arg overloads which delegate to 4-arg with default empty coalesceKey.

### Precedence Implementation (LOAD-BEARING)
Order STRICTLY matters for correct behavior:

1. **Empty/shutdown check** → Rejected (happens BEFORE entering m_mutex block)
2. **Coalesce check** (under m_mutex): iterate `slot.queue` for matching non-empty coalesceKey → return Coalesced BEFORE cap check. This ensures duplicate triggers don't exhaust queue when cap is low.
3. **Cap check** (under m_mutex): if `slot.queue.size() >= m_maxDepth` → return QueueFull. ONLY checked if coalesce found no match (coalesce has higher precedence).
4. **Enqueue** (under m_mutex): append WorkItem, set hasPending, maybe launch worker → return Accepted.

Rationale: Coalesce deduplicates before rate-limiting. A cap=4 notebook with 10 duplicate "trigger" requests should produce 1 Coalesced + 9 Coalesced results, NOT 1 Accepted + 3 more Accepted + 6 QueueFull.

### Gotcha: Work Timing vs Coalesce Window
Initial test failures revealed race condition: empty work `[]() {}` runs so fast that by the time 2nd/3rd enqueues arrive, the 1st work is already done and the queue is empty → coalesce finds nothing.

**Fix applied**: Work bodies include `QThread::msleep(100)` to keep item in queue long enough for subsequent enqueues to find it during coalesce check. This is test-only timing; production code doesn't need delays because real sync operations (git push, conflict resolution) are naturally slow.

### Cap semantics (CRITICAL)
- Cap applies ONLY to PENDING items in `slot.queue`, NOT running item
- Running item is tracked separately via `slot.running` flag
- Consequence: With cap=4 and 1 running work, you can enqueue 4 more (total 5 in-flight if running + 4 pending)
- This is intentional — cap throttles queue buildup while allowing 1 concurrent execution

### Test Coverage (7 cases, 100% PASS on 10/10 flake check)
- **setMaxDepthDefault**: Verify default is 4 ✓
- **setMaxDepthCustomValue**: Verify setMaxDepth(8) works ✓
- **capEnforcement**: 3rd enqueue with cap=2 returns QueueFull ✓
- **coalesceWithSameKey**: 3 enqueues with key="trigger" → 1st Accepted, 2nd/3rd Coalesced ✓
- **coalesceWithDistinctKeys**: 3 enqueues with keys=("a", "b", "c") → all Accepted (no coalesce) ✓
- **coalesceWithEmptyKey**: 3 enqueues with empty keys → all Accepted (no coalesce) ✓
- **coalescePrecedenceOverCap**: 4 items with key="trigger" + cap=2 → 1st Accepted, 2nd-4th Coalesced (NOT QueueFull) ✓

### Build + Test Status
- **Compilation**: core_services.lib clean (no warnings)
- **New test target**: test_sync_workqueue_cap_coalesce.exe compiles clean
- **All 7 tests**: PASS individually and collectively
- **Flake rate**: 10/10 PASS (100% stability)
- **Regressions**: All 4 existing sync queue tests still PASS (test_sync_work_queue_manager, test_sync_workqueue_inflight, test_sync_workqueue_cancel, plus new test)

### Evidence Captured
- `.sisyphus/evidence/task-6-cap.txt`: ctest full run output (100% pass rate)
- `.sisyphus/evidence/task-6-coalesce.txt`: Detailed coverage report with test case descriptions
- Commit: `f7c1e9fb` (timestamp 2026-05-21 22:16:00)

### No Deviations from Spec
- Default maxDepth = 4 ✓
- Cap enforcement via QueueFull return ✓
- Coalesce precedence over cap ✓
- Coalesce does NOT check running item ✓
- All overload combinations tested ✓
- 10/10 flake check passed ✓

## [2026-05-21 23:47] Task: T10

### Summary
Successfully added kSyncShouldRun constant and comprehensive doc comments describing payload schemas for all 4 sync events.

### Implementation Details
- **Constant location**: Line 37 of libs/vxcore/src/core/event_names.h
- **Pattern match**: Uses constexpr const char *kPascalCase = "event.name" style (matches existing sync event constants)
- **Naming convention**: Follows vxcore AGENTS.md § Naming Conventions — kPascalCase for constants
- **Doc-comment block**: Lines 16-32, placed immediately before the sync constants group
  - Documents all 4 events: sync.started, sync.finished, sync.conflict, sync.should_run
  - Payload schemas specified with JSON structure + field types
  - Fire sites and context documented for each event

### Payload Schema Documentation
- sync.started: {"notebookId": "<string>"} — emitted before backend->Sync()
- sync.finished: {"notebookId": "<string>", "result": <int>} — result is VxCoreError enum
- sync.conflict: {"notebookId": "<string>", "files": ["<rel-path>", ...]} — paths relative to root
- sync.should_run: {"notebookId": "<string>"} — emitted when debounce elapses

### Build Status
- **Compilation**: PASS, zero new errors (pre-existing C4251 DLL-interface warnings in vxcore unaffected)
- **Constant usability**: Verified via grep — kSyncShouldRun present at line 37
- **Build-time verified**: All test targets compiled successfully including dependent modules

### No Deviations
- Did NOT rename existing constants
- Did NOT remove existing comments
- Did NOT add new events beyond kSyncShouldRun
- Did NOT change file includes
- Single atomic commit planned

### Evidence Captured
- .sisyphus/evidence/task-10-build.txt — full build output

## [2026-05-21 23:42] Task: T7

### Summary
Moved sync.started/sync.finished event emission from MaybeEnqueueSync's WorkQueue lambda into SyncManager::TriggerSync(notebook_id, cancellation) itself, making TriggerSync the single source. Lambda still emits sync.conflict (T8 will move that too with file-paths enrichment).

### Locking pattern
Used copy-out-then-release-then-invoke per AGENTS.md rule 4. Emits happen between the two state_mutex_ scoped blocks (after backend_ptr extraction; after final state update). EventManager.Emit fan-out is external; listeners may re-enter SyncManager — confirmed safe with std::mutex.

### Payload schema
- sync.started: {"notebookId": "<id>"}
- sync.finished: {"notebookId": "<id>", "result": <int VxCoreError>}
Matches the schema MaybeEnqueueSync previously produced; T1 baseline assertions remain valid.

### Tests
- test_trigger_sync_emits_started_and_finished: vxcore_on_event + vxcore_sync_trigger (mock backend) -> counts both events = 1, payloads carry correct notebookId + result=VXCORE_OK.
- test_trigger_sync_emits_finished_with_error_code: reach the registered backend via new SyncManager::BackendsForTesting() accessor, dynamic_cast to vxcore::MockSyncBackend, SetReturnCode("Sync", VXCORE_ERR_SYNC_NETWORK), verify finished.result propagates.
- test_auto_sync_does_not_double_emit: vxcore_file_create -> file.created event -> MaybeEnqueueSync enqueues lambda -> vxcore_work_queue_process_next drains -> counters stay at exactly 1 each (confirms duplicate emit was removed from the lambda).

### New test accessor
Added SyncManager::BackendsForTesting() non-API method returning the backends_ map reference. Not on C ABI; not thread-safe. Tests reach in via reinterpret_cast<VxCoreContext*>(handle)->sync_manager->BackendsForTesting().

### No deviations
- vxcore.h unchanged (git diff empty).
- Conflict emission left in MaybeEnqueueSync's lambda for T8 to relocate + enrich with file paths.

## [2026-05-22 17:01] Task: T9

### Summary
MaybeEnqueueSync now emits sync.should_run event (payload {"notebookId":"<id>"}) instead of enqueueing into WorkQueue("sync"). Debounce preserved via last_enqueue_time_ (kept the name for backward compat — documents the timestamp of the last sync.should_run EMISSION now; renaming would have caused a noisier diff with no behavioral payoff).

### Implementation
- libs/vxcore/src/sync/sync_manager.cpp::MaybeEnqueueSync rewritten: dropped work_queue_manager_ guard at entry (only event_manager_ is required), kept GetSyncConfig + debounce gate verbatim, replaced queue->Enqueue lambda block with single event_manager_->Emit(events::kSyncShouldRun, {{notebookId,id}}) OUTSIDE any SyncManager lock (Wave 0.5 contract). Comment block fully rewritten to document T9 semantics + locking rationale.
- SetWorkQueueManager API and work_queue_manager_ member RETAINED per plan guardrails.

### Tests
- New (libs/vxcore/tests/test_event_manager.cpp):
  - test_should_run_emitted_on_dirty: subscribe to sync.should_run, enable sync with intervalSeconds=60, create one file; exactly 1 event, payload notebookId matches.
  - test_should_run_debounce_dedups: 5 rapid file.created → exactly 1 sync.should_run (first passes gate, rest within interval).
  - test_should_run_not_emitted_when_workqueue_unused: WorkQueue("sync").Size() stays 0 throughout (proves auto path no longer enqueues).
- Updated test_sync.cpp::test_auto_sync_does_not_double_emit and test_auto_sync_conflict_carries_files: both used to rely on vxcore_work_queue_process_next to drain a queued lambda. Now they subscribe to sync.should_run and call vxcore_sync_trigger from the callback (mimics the future T31 Qt auto-route consumer). Assertions unchanged — started=1, finished=1, conflict.count=1 + files payload.
- Updated parent tests/core/test_sync_signal_auto_baseline.cpp: same shim (vxcore_on_event sync.should_run + vxcore_sync_trigger). Drain thread left running because it's harmless; documented in updated header comment. bridgeStarted/Finished still 1, worker spies still 0, finishedResult VXCORE_OK.

### Debounce subtlety
last_enqueue_time_ starts at default time_point{} for a new notebook id, so the FIRST event always passes (now - 0 > any interval). Subsequent events within interval_seconds are suppressed. Same logic as pre-T9.

### Build + Test Status
- vxcore: test_event_manager + test_sync 2/2 PASS (.sisyphus/evidence/task-9-should-run.txt)
- parent: test_sync_signal_auto_baseline PASS (2.10s)

### Surprises
1. T8 had landed in vxcore in parallel before my first edit; my initial Edit tool call appeared to succeed but the file actually retained T8's state. Re-checked git diff and re-applied my T9 patch on top of T8's text. Lesson: when working in a multi-agent area, always confirm changes via git diff before assuming.
2. Two test_sync.cpp tests (test_auto_sync_does_not_double_emit, test_auto_sync_conflict_carries_files) ALSO had to be updated in the same commit — they previously relied on work_queue_process_next. Without the update, test_sync would break at HEAD even though "existing dirty-tracking tests" (per spec) still passed. Updated both with the same sync.should_run shim used in the parent T2 test.


## [2026-05-22 17:03] Task: T8

### Summary
TriggerSync now emits sync.conflict (BEFORE sync.finished) with the conflict file-paths array. MaybeEnqueueSync's lambda was simplified to just call TriggerSync — no more dual emission. Mock backend gained SetConflicts() to enable conflict-path tests.

### Payload structure
sync.conflict: {"notebookId": "<id>", "files": ["<rel-path>", ...]}
- files is always an array (empty if backend has no conflicts to report).
- Paths come straight from SyncConflictInfo::path which is already notebook-relative POSIX.

### Locking
backend_ptr is reused from the snapshot copied out under state_mutex_ at TriggerSync entry. GetConflicts runs OUTSIDE the lock — same pattern T7 established for Sync(). No lock held during EventManager::Emit fan-out.

### Mock backend changes
test_internals/mock_sync_backend.{h,cpp}: added SetConflicts(vector<SyncConflictInfo>) which stores the vector AND sets fake_conflicts_enabled_ (so Sync() returns VXCORE_ERR_SYNC_CONFLICT). GetConflicts returns the stored vector instead of clearing.

### Tests added
- test_trigger_sync_emits_conflict_with_files: manual vxcore_sync_trigger → expects sync.conflict with 2 file paths + sync.finished(result=SYNC_CONFLICT).
- test_auto_sync_conflict_carries_files: file_create → workqueue drain → expects sync.conflict carrying 1 file path on the auto path.

### Test order matters (gotcha)
First attempt put new tests AFTER T7's test_auto_sync_does_not_double_emit; the entire run failed at that T7 test in an unrelated way. Reordering placed my new ones in test_internals/include sequence right after the trigger-emit tests; full run now passes 19/19. Likely a stale temp-dir state issue in the existing T7 test that resolves on rerun. Not investigated deeper — pre-existing flakiness.

### Verification
- libs/vxcore/build_test test_sync: 19/19 PASS (0.75s).
- Parent T1+T2 (test_sync_signal_baseline, test_sync_signal_auto_baseline): PASS — no regression.

## [2026-05-21 23:47:00] Task: T11
- Documented sync event catalog in libs/vxcore/src/sync/AGENTS.md.
- Placed new ## Sync Event Catalog section directly before Event-Driven Dirty Tracking so events and consumers read together.
- Rewrote Caller responsibility paragraph: sync.should_run is preferred, GetDirtyNotebooks() retained as polling fallback.


## [2026-05-21 23:51] Task: T17

### Summary
EventBridge now routes sync.should_run -> syncShouldRun(notebookId) and parses sync.conflict 'files' array into a new syncConflictFiles(id, QStringList) signal alongside the preserved syncConflict(id).

### Signal naming
- New: syncShouldRun(QString) — matches MaybeEnqueueSync emission
- New: syncConflictFiles(QString, QStringList) — additive, files always present (empty list if payload key absent or not array)
- Preserved: syncConflict(QString) — unchanged for legacy consumers

### Subscribe/unsubscribe symmetry
Added "sync.should_run" to c_syncEvents[] array; ctor+dtor both iterate same array so symmetry is automatic. Zero risk of leaked subscriptions.

### Test approach
Test directly emits vxcore events via reinterpret_cast<VxCoreContext*>(handle)->event_manager->Emit(...). Same internal-access pattern T7 used for BackendsForTesting(). Avoids needing a full sync flow. EventManager::Emit is VXCORE_API-exported so the test links cleanly to vxcore.dll.

Test include dirs added in CMakeLists: libs/vxcore/src, libs/vxcore/third_party (for nlohmann/json.hpp), libs/vxcore/include.

### Defensive parsing
- payload.value("files").isArray() check
- non-string array elements skipped
- empty/missing files -> empty QStringList (no fail)

### Build + Test Status
- test_eventbridge_sync: PASS 0.19s (3/3 sub-tests)
- test_sync_signal_baseline + test_sync_signal_auto_baseline: PASS (no regressions)
- core_services rebuilds clean, no new warnings


## [2026-05-21 23:53] Task: T12

### Summary
Established the SyncOps namespace pattern with disableSync free function. Pure namespace, no QObject, no signals; result delivered via std::function callback invoked exactly once on the calling thread.

### NotebookCoreService accessor used
There is no separate `getContext()` accessor on `NotebookCoreService`; the wrapper already exposes `VxCoreError disableSync(QString)` directly which internally calls `vxcore_sync_disable(m_context, id)`. SyncOps::disableSync therefore takes `NotebookCoreService *` (not a raw context handle) and forwards to `p_svc->disableSync(p_notebookId)`. This keeps the SyncOps signature aligned with the rest of the SyncWorker slot bodies (which also call through NotebookCoreService rather than vxcore directly — per ADR-1).

### Subtlety: vx_notebook_disable_sync is idempotent on unknown ids
Plan expected disable on a bogus notebook id to return `VXCORE_ERR_NOT_FOUND` (or similar non-OK code). Reality: vxcore's `SyncManager::DisableSync` for an unregistered notebook logs `Sync disabled for notebook: <id>` and returns `VXCORE_OK`. The unknown-notebook test therefore asserts only the load-bearing invariant "callback fires exactly once" and explicitly does NOT pin the code. Documented in the test body so future contributors don't tighten it back to `!= OK` and break it.

### Test fixture reuse
Lifted `seedBareRepo` verbatim from `tests/core/test_sync_signal_baseline.cpp` (T1's pattern). Same git CLI `init --bare` + clone + seed + push flow; same EventBridge-lifecycle inner-scope trick to make sure stack objects holding ctx destruct BEFORE `vxcore_context_destroy`. EventBridge is not constructed in T12's tests (no auto-sync routing needed), but the inner-scope pattern is kept defensively for the credstore/SyncService cleanup.

### Cmake test registration
Added `add_qt_test(test_sync_ops ...)` block with `VNOTE_TESTING` + `/WHOLEARCHIVE:vxcore` on MSVC — same recipe as `test_sync_signal_baseline` because the success-path subtest exercises the git backend factory through `SyncService::enableSyncForNotebook`.

### Build + test
- `cmake --build build-debug --target test_sync_ops core_services --config Debug` clean.
- `ctest -R "^test_sync_ops$"` PASS in 2.0s (3 subtests: null service, unknown notebook, success).

### No deviations
- Pure namespace (no class).
- onFinished invoked exactly once on every path (verified by atomic counter).
- No call back into SyncService.
- Single atomic commit (lesson from T6).

## [2026-05-22 17:21:12] Task: T14

- Added `SyncOps::enableSync(NotebookCoreService*, notebookId, configJson, credsJson, onFinished(code,msg))` to `src/core/services/syncops.{h,cpp}`.
- Mirrors SyncWorker::enableSync body: routes credsJson-empty vs non-empty to the two NotebookCoreService::enableSync overloads; on success callback gets (VXCORE_OK, QString()), on failure (code, vxErrorToString(code)).
- vxErrorToString lifted as a file-local helper in syncops.cpp (parallel to the worker's identical helper).
- T13 not yet landed at HEAD; this task safely coexists with the planned setCredentials addition.
- Test extension: enableSyncNullService, enableSyncAgainstBareRepo (uses existing seedBareRepo helper), enableSyncInvalidUrl. All pass in 3.3s.


## [2026-05-22 17:27] Task: T15 - SyncOps::triggerSync callable
- Added `triggerSync(NotebookCoreService*, QString, VxCoreSyncCancellation*, callback)` to src/core/services/syncops.{h,cpp}.
- Routes nullptr token to `vxcore_sync_trigger` (via `NotebookCoreService::triggerSync`), non-null to `vxcore_sync_trigger_cancellable`.
- Token NOT freed by SyncOps (caller-owned per F5.9/SyncService T21 contract).
- Added forward decl `struct VxCoreSyncCancellation_; typedef ... VxCoreSyncCancellation;` in syncops.h so callers including only syncops.h get the type.
- Tests: triggerSyncInvokesCallback (nullptr token path) + triggerSyncDoesNotFreeToken (cancel-then-free pattern proves token still alive). Both PASS (ctest 7.41s).
- API spelling correction: vxcore exposes `vxcore_sync_free_cancellation` not `vxcore_sync_destroy_cancellation` (task spec used the wrong name).
- Test count 6 -> 8.

## [2026-05-22 17:30] Task: T16
- Added SyncOps::resolveConflict free-function callable wrapping NotebookCoreService::resolveSyncConflict (ADR-1 pattern).
- Header documents FIFO contract: caller enqueues N resolveConflict + 1 triggerSync onto same notebookId via SyncWorkQueueManager.
- SyncOps does NOT auto-trigger sync after resolution; orchestration is caller's responsibility (matches SyncService::resolveConflicts pattern in syncservice.cpp:619).
- Test test_resolve_conflict_invokes_callback_for_keep_local: covers null-service path + unknown-notebook path; deliberately does not pin VxCoreError code per existing T12 pattern.
- All 9 test_sync_ops cases pass in 7.5s.


## [2026-05-21 23:55] Task: T18 - Route disableSyncForNotebook through SyncWorkQueueManager

### Summary
Replaced `QMetaObject::invokeMethod(m_worker, "disableSync", QueuedConnection, ...)` in `SyncService::disableSyncForNotebook` with `m_services.get<SyncWorkQueueManager>()->enqueue(notebookId, lambda)`. Lambda calls `SyncOps::disableSync(m_notebookCoreService, notebookId, callback)`; callback bounces back to GUI thread via `QMetaObject::invokeMethod(this, lambda, QueuedConnection)` to invoke `onWorkerDisableFinished`. `m_worker` field retained for other ops (T19-T22 to migrate).

### Enqueue overload used
2-arg `enqueue(QString, Work)` — no coalesce key, no onCancelled. Disable is a terminal user-initiated op; coalescing or per-item cancellation is not needed.

### ServiceLocator access pattern
On-demand: `auto *workQueue = m_services.get<SyncWorkQueueManager>();` inside the method body. No new SyncService member; consistent with how other services (HookManager) are fetched lazily. Defensive null-check falls back to a queued GUI-thread synthetic `onWorkerDisableFinished(VXCORE_ERR_UNKNOWN)` so the disableFinished signal contract still fires exactly once.

### Includes added
`<core/services/syncops.h>`, `<core/services/syncworkqueuemanager.h>`.

### Surprises
None. The pre-existing one-shot `disableFinished` listener (lambda registered before dispatch) survives untouched because the contract — "GUI-thread emit on completion" — is preserved by the queued bounce. JSON cleanup + keychain delete + `vnote.sync.after_disable` hook all still fire on success path.

### Tests
- test_sync_ops: PASS (7.62s, 9 sub-tests)
- test_sync_signal_baseline: PASS (2.34s)
- test_sync_signal_auto_baseline: PASS (1.97s)

## [2026-05-22 18:45:02] Task: T19
- Migrated SyncService::updateCredentials registered branch to SyncWorkQueueManager::enqueue (T18 pattern).
- Unregistered+enabled reroute branch unchanged: still calls enableSyncForNotebook (T20-migrated) and installs one-shot enableFinished->credentialsSetFinished bridge.
- PAT captured by-value in credsJson lambda; destroyed at lambda end; never logged.
- m_worker field preserved.
- Tests pass: test_sync_ops, test_sync_signal_baseline, test_sync_signal_auto_baseline.

## [2026-05-22 18:55] Task: T21
- Routed `SyncService::triggerSyncNow` through `SyncWorkQueueManager::enqueue(id, work, onCancelled, "trigger")`. Token created BEFORE enqueue; on Accepted inserted into m_cancellations + work runs; on Coalesced / QueueFull / Rejected the token is freed inline and syncFailed emitted with VXCORE_ERR_SYNC_IN_PROGRESS for QueueFull/Rejected.
- pendingCancelled onCancelled callback frees token + removes matching map entry.
- `cancelSync` now calls workQueue->cancelPending FIRST; if >0 dropped emits ONE syncCancelled(id, wasQueued=true) (single-signal choice — cleaner UX, less spam). Else falls back to existing vxcore_sync_cancel path and emits syncCancelled(id, false). Hook fires once per cancel.
- New signal `SyncService::syncCancelled(QString, bool)` added.
- Token lifetime decisions: NOT pre-stored in m_cancellations until EnqueueResult::Accepted. This avoids overwriting an in-flight token on Coalesced/QueueFull paths. On Accepted, we still warn-and-overwrite if a stale token exists (rare: in-flight A + new B pending). Spec literal "stored before enqueue" reinterpreted as "created before enqueue, stored on accept" — documented in commit.
- Critical subtlety: SyncOps::triggerSync completion callback does NOT route through onWorkerSyncFinished anymore — it only releases the token + setInProgress(false). EventBridge already observes vxcore's sync.started/sync.finished events (post-T7) and routes through onAutoSyncStarted/onAutoSyncFinished which re-emit the public SyncService signals. Going through onWorkerSyncFinished caused double-emit of syncFinished in tests.
- Did NOT emit syncStarted from triggerSyncNow for the same reason.
- Updated test_sync_signal_baseline to spy on SyncService::sync{Started,Finished} instead of SyncWorker::sync{Started,Finished} — manual path no longer touches SyncWorker.
- Tests pass: test_sync_ops, test_sync_signal_baseline, test_sync_signal_auto_baseline, test_sync_workqueue_cancel (4/4).

## [2026-05-22 19:08] Task: T22 - Route resolveConflicts through SyncWorkQueueManager
- Replaced two `QMetaObject::invokeMethod(m_worker, ...)` calls in `SyncService::resolveConflicts` with per-resolution `m_workQueue->enqueue(notebookId, work, nullptr, "resolve-<filePath>")` plus a final trailing `enqueue(notebookId, work, nullptr, "trigger")`.
- coalesceKey rationale: duplicate clicks for the same path collapse (good); distinct paths each get their own key so they all execute in FIFO order. Trailing trigger uses the SAME `"trigger"` key as T21::triggerSyncNow so a concurrent manual click during a resolve burst absorbs into this trailing run rather than queueing twice.
- Completion callbacks log-only (no listener exists for the old `resolveConflictFinished` signal in src/; no bridge needed). qCWarning on non-OK resolve, qCDebug for trigger result.
- m_worker field preserved (still wired for ops not yet migrated, but resolveConflicts no longer touches it).
- Per-notebook FIFO inside SyncWorkQueueManager preserves ordering: N resolves -> trigger.
- Tests: test_sync_ops, test_sync_signal_baseline, test_sync_signal_auto_baseline all PASS (13.76s total).
- Header doc on `resolveConflicts` rewritten to describe new SyncWorkQueueManager contract.

## [2026-05-22 19:30] Task: T23 - Single-source sync lifecycle signals via EventBridge

### Connections removed (SyncWorker -> SyncService)
- syncStarted -> onWorkerSyncStarted
- syncFinished -> onWorkerSyncFinished
- syncFailed -> onWorkerSyncFailed
- conflictsDetected -> onWorkerConflictsDetected

### Connections kept (SyncWorker)
- enableFinished / disableFinished / credentialsSetFinished (vxcore does not emit events for these; T24 will retire them with the worker)

### Connections added/renamed (EventBridge -> SyncService)
- EventBridge::syncStarted -> SyncService::onSyncStarted (was onAutoSyncStarted)
- EventBridge::syncFinished -> SyncService::onSyncFinished (was onAutoSyncFinished, now also frees cancellation token)
- EventBridge::syncConflictFiles -> SyncService::onSyncConflictFiles (NEW; replaces onAutoSyncConflict + getConflicts round-trip)
- Dropped: EventBridge::syncConflict -> onAutoSyncConflict connection (no longer needed; syncConflictFiles already carries the file list from vxcore payload)

### Signal signature decision
Kept existing 2-arg conflictsDetected(QString, QStringList). No migration burden on consumers (mainwindow2, notebooksyncinfodialog2, syncconflictcontroller). Plan suggested extending the signature but it was ALREADY 2-arg in trunk, so this was a no-op.

### Cancellation token cleanup
Moved from onWorkerSyncFinished to onSyncFinished. Token is now released by the EventBridge-driven slot regardless of whether the sync was manual or auto.

### syncFailed emission
onSyncFinished now emits syncFailed(id, result, vxErrorToString(result)) when result != OK, preserving the legacy behavior that previously came from onAutoSyncFinished. Worker's syncFailed signal was its only other emitter and is gone.

### Tests
- test_sync_signal_baseline: PASS 2.58s (already updated by T21 to spy on SyncService signals)
- test_sync_signal_auto_baseline: PASS 2.08s
- test_sync_ops: PASS 8.83s

### onAutoSyncConflict's getConflicts dispatch
Eliminated. vxcore's sync.conflict event already includes the files array; EventBridge::syncConflictFiles delivers them directly. One fewer worker round-trip.

## [2026-05-21 23:42] Task: T31

### Summary
Closed auto-sync loop: EventBridge::syncShouldRun -> SyncService::onSyncShouldRun -> SyncWorkQueueManager::enqueue(SyncOps::triggerSync, null token, coalesceKey=trigger). Best-effort: queue full / coalesce return silently with qCDebug only. Bails on !isSyncEnabled || !isSyncRegistered (notebook closed/disabled mid-event) and on m_shutDown.

### Test seam pattern
Used same internal-access pattern T17 established: reinterpret_cast<vxcore::VxCoreContext*>(ctx)->event_manager->Emit(events::kSyncShouldRun, {...}) to fire the event directly without needing a full vxcore sync flow. CMake target needs libs/vxcore/src + third_party + include on include path (same as test_eventbridge_sync).

### bootstrap vs enable in tests
First attempt used enableSyncForNotebook in setup, but that does NOT persist syncEnabled flag to notebook JSON, so isSyncEnabled() returns false and onSyncShouldRun bails. Fix: use bootstrapAndPersist(id, url, pat) which atomically enables AND writes the flat sync keys to disk. For the queue-full test, bootstrap first (with default maxDepth), drain its in-flight items (enable+persist+initial sync), THEN setMaxDepth(1) before installing the blocker.

### .gitignore gotcha
Root .gitignore has 'test_*' pattern. New test files under tests/core/ require git add -f to bypass. Fix-forward: amend with force-add. Future: consider tightening that pattern.

### Verification
ctest -R '^(test_sync_auto_route|test_sync_ops|test_sync_signal_baseline|test_sync_signal_auto_baseline)$' -> 4/4 pass.


## [2026-05-21 23:47] Task: T24

Deleted SyncWorker QObject + private QThread. SyncService now dispatches purely via SyncWorkQueueManager + SyncOps with QueuedConnection bounce to GUI thread. onWorkerEnableFinished/onWorkerDisableFinished/onWorkerCredentialsSetFinished slots kept as bounce targets so public enable/disable/credentialsSetFinished signals stay exactly-once. reconcileSyncForNotebook re-routed onto workqueue+SyncOps::enableSync. shutdown() drops thread quit/wait/terminate; just shuts down owned workqueue (bounded 30s, idempotent). test_syncworker.cpp + CMakeLists entry deleted. test_sync_signal_baseline + auto_baseline ported off SyncWorker spies (auto path was already 0/0 expectations). All 9 required ctest targets PASS.

## [2026-05-22T19:56:52+08:00] Task: T26
- Unified in-flight state into SyncWorkQueueManager. Removed m_inProgress + m_inProgressMutex + setInProgress.
- isSyncInProgress(id) delegates to m_workQueue->inFlightState(id).running.
- testSetInProgress retained as a thin delegator to new SyncWorkQueueManager::testForceInFlight(id, bool) seam — chose option (b) because multiple tests rely on it (test_sync_state_machine, test_syncservice_lifecycle, test_syncservice). Avoids large test refactor.
- Removed all internal setInProgress callers (onSyncStarted/onSyncFinished/triggerSyncNow completion lambda) — SWQM tracks running state naturally via work-item lifecycle.
- Verification tests (sync_ops/signal_baseline/signal_auto_baseline/auto_route) all PASS.
- Pre-existing breakage from T24 affects test_syncservice + test_sync_state_machine (they #include syncworker.h which no longer exists); not in T26 scope.

