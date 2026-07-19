# Agent Guidelines for src/core/services

Qt-side service layer wrapping the vxcore C library. Services here are the bridge between the GUI (controllers, widgets) and the vxcore backend. Most are thin handles around `XXXCoreService` types holding a `VxCoreContextHandle`; a few (e.g., `BufferService`, `SyncService`) add hook firing and async orchestration on top.

For the canonical service catalog, DI rules, and Buffer2/HookManager patterns, see the parent `src/core/AGENTS.md`.

## NotificationService

`NotificationService` (`notificationservice.{h,cpp}`) is an in-memory notification store: a `QObject` that is deliberately **Qt-Widgets-free** (only `<QObject>`, `<QDateTime>`, `<QVector>`, `std::function`) so it stays in `src/core/services`. It holds a `QVector<NotificationMessage>`, assigns a monotonic `quint64` id + timestamp in `notify()`, and emits `messageAdded` / `messageDismissed` / `messagesCleared`. All presentation (severity→icon mapping, popup, badge) lives in the widget layer (`NotificationButton2` / `NotificationPopup2`, see `src/widgets/AGENTS.md` § Notification System).

- `NotificationMessage` is a copyable value type carrying `Severity`, `Duration`, and `QVector<NotificationAction>` (each action = label + `std::function<void()>`). It is registered via `Q_DECLARE_METATYPE` + `qRegisterMetaType` in the ctor so `messageAdded` survives a queued (cross-thread) connection if a future producer calls `notify()` off the GUI thread.
- Current usage is GUI-thread only; the service has no internal locking. If you add an off-thread producer, keep the metatype registration and rely on auto/queued connections rather than adding a mutex.
- `dismiss()` marks a message dismissed (it stays in the list but is excluded from `activeCount()` and hidden by the popup); `clearAll()` removes all messages. `Duration` is a UI auto-hide hint only, not a retention policy.

## Threading rules for SyncService

`SyncService` is the Qt-side facade for `vxcore::SyncManager`. It must respect the contract documented in `libs/vxcore/src/sync/AGENTS.md` § Threading & Callback Contract. The Qt-specific obligations are:

1. **Never invoke vxcore sync APIs (`vxcore_sync_*`) from the GUI thread synchronously for long-running ops** (`enableSyncForNotebook`, `triggerSync`, `updateCredentials`). Off-load to a worker via the current executor. Short metadata calls (`isSyncRegistered`, `hasCredentials`) may run on the GUI thread.
2. **All Qt signals emitted from the worker thread MUST use `Qt::QueuedConnection`** when crossing back to GUI-owned receivers (controllers, widgets). Hook invocations fired inside the worker likewise must not assume GUI-thread affinity.
3. **Do not hold any `SyncService` member mutex while emitting a signal, firing a hook, or invoking a credential / progress callback.** Snapshot the data under the lock, release the lock, then notify (mirrors vxcore rule 2).

### Current executor

**SyncWorkQueueManager is the sole Qt-side sync dispatch primitive.** SyncWorker has been removed (see commit `42ba209c`). All async sync operations (enable, disable, setCredentials, triggerSync, resolveConflict, auto-sync) route through `m_workQueueManager->enqueue(notebookId, lambda, [coalesceKey])` where lambda calls a `SyncOps::*` free function. Completion bounces back to the GUI thread via `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)`.

### Sync dispatch flow

**User-initiated path:**
`SyncService::triggerSyncNow` → `m_workQueueManager->enqueue(id, λ→SyncOps::triggerSync, "trigger")` → pool thread → `vxcore_sync_trigger` → vxcore emits `sync.started` / `sync.finished` / `sync.conflict` events → `EventBridge` → `SyncService` Qt signals.

**Auto-sync path:**
vxcore file events → `SyncManager::MaybeEnqueueSync` → emit `sync.should_run` → `EventBridge::syncShouldRun` → `SyncService::onSyncShouldRun` → per-notebook trailing-throttle debounce (see below) → `enqueueAutoSync` → `m_workQueueManager->enqueue(id, λ→SyncOps::triggerSync, "trigger")` → … (same tail as user path).

**Coalescing:** both paths use `coalesceKey="trigger"` so the second of two concurrent trigger requests is dropped (returns `Coalesced` result). This prevents redundant network round-trips when a user clicks Sync while an auto-sync is already queued (or vice versa).

**Queue cap:** default 4 pending items per notebook. Excess enqueues return `QueueFull`, which surfaces as `syncFailed(SYNC_IN_PROGRESS, "sync queue full")` on the manual path and as a silent `qCDebug` log on the auto-sync path.

**Cancellation:** `SyncService::cancelSync(id)` calls `m_workQueueManager->cancelPending(id)` first (removes queued items, emits `syncCancelled(id, wasQueued=true)` for each dropped item) THEN cancels the in-flight token via `vxcore_sync_cancel` (emits `syncCancelled(id, wasQueued=false)` when the in-flight sync unwinds and returns).

`isSyncInProgress(id)` now delegates to `SyncWorkQueueManager::inFlightState(id).running` — there is no longer a separate `m_inFlight` set on `SyncService`. The queue manager is the single source of truth for in-flight state.

**Cancellation event payload:** `SyncCancelledEvent` (typed hook event for `vnote.sync.cancelled`) carries `notebookId` (QString) and `wasQueued` (bool). `wasQueued=true` indicates the cancellation removed a pending queue entry; `wasQueued=false` indicates the in-flight sync was aborted via `vxcore_sync_cancel`.

### Auto-sync debounce (trailing throttle)

The auto-sync path is debounced one layer ABOVE `SyncWorkQueueManager`, so queue and coalesce semantics are untouched. Manual "Sync Now" (`triggerSyncNow`) and the post-reconcile freshness trigger (`maybeTriggerPostReconcile`) BYPASS the debounce; only `EventBridge::syncShouldRun` → `onSyncShouldRun` is throttled.

**Insertion point.** `SyncService::onSyncShouldRun` (`syncservice.cpp`) keeps its shutdown / readiness / auth-circuit-breaker guards, then reads the cadence via `debounceSeconds()`. When the cadence is `<= 0` it calls `enqueueAutoSync` immediately (debounce disabled, `0 = immediate`); otherwise it calls `armOrIgnoreDebounce`.

**Trailing-throttle semantics.**
- `armOrIgnoreDebounce(id)`: if a timer is already active for the notebook, it is KEPT (the burst is absorbed into the pending fire, not reset). Otherwise it creates/reuses a parented single-shot `QTimer` whose delay is `qMax(0, (lastSyncMs + cadence*1000) - now)`. So a notebook that synced recently waits out the remainder of the window; one that has not synced in a while fires (near-)immediately on the next tick.
- `onDebounceTimeout(id)`: re-runs the shutdown / readiness / auth-circuit-breaker guards, then RE-READS the cadence and RE-CHECKS freshness. If the last sync is still inside the window it re-arms for the remaining time instead of enqueuing (defends against a sync that landed while the timer was pending). Otherwise it calls `enqueueAutoSync`.
- `enqueueAutoSync(id)`: the single shared enqueue body, `coalesceKey="trigger"` (same key as the manual path, so the two still dedupe against each other).

**Read on demand.** `debounceSeconds()` reads `ConfigCoreService::getAutoSyncDebounceSeconds()` (clamped `[0, 86400]`) on every call rather than caching, so editing `autoSyncDebounceSeconds` in `vxcore.json` takes effect without restart. The value is a GLOBAL app-config integer stored in vxcore's `vxcore.json` but consumed ONLY by VNote; vxcore does not schedule. The per-notebook `autoSyncEnabled` boolean gate is separate: it suppresses `sync.should_run` emission inside vxcore when false and carries no cadence.

**Timer cleanup.** Debounce timers are dropped on `NotebookAfterClose`, `disableSyncForNotebook` success, `unregisterSyncRuntime`, `shutdown()`, and the destructor, so a retired notebook never leaves a live timer behind.

**Test seams** (on `SyncService`, unconditional per ADR-6):
- `testSetDebounceOverrideSeconds(int)`: forces `debounceSeconds()` to return the override (pass `< 0` to clear and fall back to `ConfigCoreService`).
- `testIsDebounceTimerActive(id)`: true if a debounce `QTimer` is currently armed for the notebook.
- `testDebounceRemainingMs(id)`: remaining ms on the armed timer (for asserting the trailing-window math).
- `testFireDebounceNow(id)`: invokes `onDebounceTimeout(id)` directly so tests do not have to wait real wall-clock time.

Coverage: `tests/core/test_sync_service_debounce.cpp`.

### Post-reconcile freshness gate (`maybeTriggerPostReconcile`)

`SyncService::maybeTriggerPostReconcile(notebookId)` closes a multi-device staleness window that `reconcileSyncForNotebook` alone did not address. Reconcile only enqueues `SyncOps::enableSync` (which registers the notebook with vxcore); the first actual `FetchOrigin` had to wait for the next file save (which triggers `mark_dirty` → auto-sync) or a manual "Sync Now". If the user closed VNote on one PC, edited on another, then reopened the first PC, they would see stale content until the next mutation.

The gate runs on the GUI thread, scheduled from the `SyncOps::enableSync` completion callback inside `reconcileSyncForNotebook` (via `QMetaObject::invokeMethod(... QueuedConnection)`). It re-uses the existing `triggerSyncNow` path so `SyncWorkQueueManager` coalescing (`coalesceKey="trigger"`) still dedupes against concurrent user-initiated or auto-sync triggers.

Early-return guards (in evaluation order):

| Guard | Condition | Why |
|---|---|---|
| shutdown | `m_shutDown` | service is tearing down |
| readiness | `!isSyncEnabled(id) \|\| !isSyncRegistered(id)` | reconcile may have raced with a disable/unregister; also serves as defense in depth if enable failed mid-reconcile |
| in-flight | `isSyncInProgress(id)` | a sync is already running; the queue would coalesce anyway, this just keeps the queue clean |
| freshness | `lastSyncMs > 0 && (now - lastSyncMs) < kPostReconcileFreshnessMs` | last successful sync was recent enough that rapid open/close cycles should not thrash the remote |

`kPostReconcileFreshnessMs = 2 * 60 * 1000` (2 minutes), defined as a `static constexpr` member of `SyncService`. Rationale: long enough to coalesce workspace switches and window refocus; short enough that a real sleep/wake cycle is treated as stale and triggered. Not yet runtime-configurable; future tuning may come from telemetry. The threshold is applied only when `lastSyncMs > 0` — a notebook that has never synced on this device falls through to the trigger path (correct: cold-start needs a fresh sync).

Routing impact: BOTH lifecycle triggers that call `reconcileSyncForNotebook` (`onNotebookAfterOpen` per-notebook and `onMainWindowAfterStart` per-notebook sweep) now produce a follow-up `triggerSync` whenever the gate's conditions are met. The wiring lives in the existing reconcile work-queue lambda; the trigger fires ONLY on `VXCORE_OK` from `SyncOps::enableSync` (no attempt to sync a notebook that failed to register).

Test seams (unconditional per ADR-6):
- `testForceLastSyncUtc(notebookId, ms)`: overrides the value `NotebookCoreService::getLastSyncUtc` would return for the freshness check; pass `-1` to clear.
- `testInvokeMaybeTriggerPostReconcile(notebookId)`: invokes the helper directly so tests do not need to stage a full reconcile (which would require a bare repo + keychain).
- `testSetMaybeTriggerBypassReadinessCheck(bool)`: skips the `isSyncEnabled / isSyncRegistered` defense so the freshness / in-progress gates can be exercised without a real vxcore registration. Defaults to `false` in production.

Coverage: `tests/core/test_sync_service_freshness.cpp` (4 cases: stale→trigger, fresh→skip, in-progress→skip, not-ready→skip).

### Save / sync I/O serialization

The auto-save path NEVER calls `vxcore_buffer_save` on the UI thread. `BufferService` (`bufferservice.h`/`.cpp`) snapshots `(content, revision)` on the GUI thread and hands the work to `BufferSaveQueue` (`buffersavequeue.h`/`.cpp`), a per-notebook FIFO that wraps `BufferCoreService::saveBuffer` on a worker so auto-save IO never blocks the editor.

Save workers and `SyncOps::triggerSync` share `NotebookIoGate` ([`notebookiogate.h`](notebookiogate.h)/[`.cpp`](notebookiogate.cpp)), a per-notebook async mutex, but they hold it for different windows. Save workers wrap their full `BufferCoreService::saveBuffer` call in `NotebookIoGate::ScopedLock(notebookId)`. `SyncOps::triggerSync` ([`syncops.cpp`](syncops.cpp)) splits the sync into two phases against [`ISyncNotebookService`](isyncnotebookservice.h): it acquires the gate, calls [`NotebookCoreService::syncStageOnly`](notebookcoreservice.h) (which wraps `vxcore_sync_stage_only` — StageAll + CommitIndex), releases the gate, then calls [`NotebookCoreService::syncNetworkPhase`](notebookcoreservice.h) (which wraps `vxcore_sync_network_phase` — FetchOrigin + RebaseOntoOrigin + PushOrigin) WITHOUT the gate held. This guarantees a sync never reads a half-flushed file, a save never lands inside someone else's `git add`/commit, and a save queued on the same notebook gets to run the instant the local commit lands instead of waiting on a network round-trip. The injection seam through `ISyncNotebookService` also makes the released-early property unit-testable without a real remote — see `tests/core/test_syncops_gate_release.cpp`. The full rationale lives in the root [Save Path Threading Contract](../../../AGENTS.md#save-path-threading-contract).

Performance instrumentation: the Qt logging category `vnote.perf.save` covers UI-thread enqueue + worker save latency, and vxcore emits `VXCORE_LOG_DEBUG` lines tagged `[perf.mark_dirty]` / `[perf.maybe_enqueue]` for the synchronous tail that still runs on the caller thread. Both are off by default; enable them when chasing UI-thread regressions.

### External-change detection gate (false-positive defense)

VNote detects files modified on disk by external tools via polling, NOT a `QFileSystemWatcher` (no watcher observes open buffers; the only `QFileSystemWatcher` instances watch the theme dir and the notebook tree). `ViewAreaController` runs a 2 s `QTimer` that checks the ACTIVE buffer (`checkSingleExternalChange`) plus a full sweep of ALL buffers on app re-focus (`checkAllExternalChanges`). The check chain is `BufferService` → `BufferCoreService::checkExternalChanges` → `vxcore_buffer_check_external_changes` → `vxcore::Buffer::CheckExternalChanges`.

The detector is a THIRD concurrent actor on the mutex-less vxcore `Buffer` (the other two are the UI thread and the `BufferSaveQueue` worker). It is NOT serialized by the save FIFO or `NotebookIoGate`. To stop a self-save from being mis-reported as an external edit, the gate is TWO-LAYER:

1. **Qt-side scheduling gate (consumer policy).** `BufferService::checkSingleExternalChange` and `checkAllExternalChanges` call `BufferSaveQueue::isBusy(notebookId, bufferId)` and SKIP the buffer when a save is pending or in-flight. This applies per-buffer to BOTH the single-active check and the full sweep (a background tab must not false-positive against its own in-flight save). It also guarantees `content_` is stable when the vxcore content-compare (below) runs, since no save worker is mutating that buffer. `isBusy` reads the queue's existing `m_pending`/`m_running` maps under `m_mutex`.
2. **vxcore content-fact confirmation (library fact).** Because the Qt gate cannot cover the sync NETWORK phase (rebase/checkout run with `NotebookIoGate` RELEASED — see Save/sync I/O serialization above) nor Windows lazy-mtime-flush, `Buffer::CheckExternalChanges` no longer flags on a bare mtime mismatch. When `current_mtime != last_modified_time_` it compares the on-disk bytes against `content_` (exact, then EOL-normalized) and only flags `FILE_CHANGED` on a real content difference; a benign mtime bump refreshes `last_modified_time_` and stays NORMAL. The stamp is refreshed ONLY on confirmed equality, so a genuine unresolved external edit keeps flagging. See `libs/vxcore/AGENTS.md` and `libs/vxcore/tests/test_buffer.cpp` (`test_buffer_external_change_content_aware`).

The git sync backend sets `core.autocrlf=false` (`git_sync_pipeline.cpp`), so sync itself does not rewrite EOLs; the EOL-normalized compare in layer 2 is defense against third-party external editors. Coverage: `tests/core/test_buffer_save_queue.cpp` (`testIsBusyReflectsPendingAndRunning`).

## Credential Cleanup Invariants

The keychain PAT for a notebook is tied to its lifecycle. To avoid orphan vault entries (which surface to users as qtkeychain Win32 error 8 and similar storage faults on the next enable attempt), `m_credentialsStore->deleteCredentials(notebookId)` runs at FIVE well-defined sites. Every code path that retires a notebook or its sync registration goes through one of these.

| Lifecycle | Site | When fires | When does NOT fire |
|---|---|---|---|
| Bootstrap rollback (new-notebook flow) | `src/controllers/newnotebookcontroller.cpp:258` | `bootstrapSync` receives `enableFinished` with a non-OK result. Runs BEFORE `closeNotebook` so the keychain slot is freed before the notebook is torn down. | Bootstrap succeeds (notebook keeps the PAT it just stored). |
| `bootstrapAndPersist` atomic rollback | `src/core/services/syncservice.cpp:815` | Persist fails AFTER vxcore enable already succeeded AND the compensating `disableSyncForNotebook` returns `VXCORE_OK`. Removes the orphan PAT left by the successful enable. | Persist succeeds (normal path); or rollback `disableSyncForNotebook` itself fails (loud `qCritical` log, PAT preserved for operator inspection). |
| Notebook removal (`NotebookAfterClose`) | `src/core/services/syncservice.cpp:176` (hook handler installed in ctor at line 172) | `NotebookCoreService::closeNotebook` returns `VXCORE_OK` (the hook only fires on success). Centralized point covering ManageNotebooks close, NewNotebook rollback close, VNote3 migration, etc. Idempotent: notebooks that never enabled sync are a no-op. | `closeNotebook` returns an error (notebook is still listed, may still need its PAT). |
| Sync disable success | `src/core/services/syncservice.cpp:423` (inside `if (p_result == VXCORE_OK)` at line 406) | `disableSyncForNotebook` worker returns `VXCORE_OK`. Runs AFTER the three flat sync JSON keys are cleared. | **INTENTIONAL**: disable failure does NOT call `deleteCredentials` (lines 433-439). The PAT is preserved so the user can retry without re-entering credentials after a transient backend error; the next successful disable cleans both JSON and keychain. |
| S6 startup sweep | `src/core/services/syncservice.cpp:1280` (`onMainWindowAfterStart`) | App start, for each notebook where `!isSyncEnabled(id) && m_credentialsStore->hasCredentials(id)` (disk says disabled but keychain still holds a PAT). Backstop for previous-session crashes between the JSON-clear and keychain-delete steps. | Disk and keychain already agree (normal case). |

**Rule for new sync-related code paths**: any time you retire a notebook, roll back an enable, or transition to a state where the on-disk JSON no longer claims sync is enabled, route through one of the five sites above. Do not call `deleteCredentials` from controllers or widgets; the cleanup contract lives in `SyncService` (and the one historical exception in `NewNotebookController::bootstrapSync`, which is documented in `src/controllers/AGENTS.md`).

## SearchService drain pool

`SearchService` owns the pool of drain threads that empty vxcore's `"vxcore.search"` work queue (see the root [Search Threading Contract](../../../AGENTS.md#search-threading-contract)). vxcore owns no search threads; this pool is VNote's side of that contract.

**Size.** `min(std::thread::hardware_concurrency(), 8)`, substituting `2` only when `hardware_concurrency()` returns `0` (count unknown). This is a fallback for the unknown case, not a floor: a genuine single-core host gets `1` drain thread. Each thread loops `vxcore_work_queue_process_next(ctx, "vxcore.search", 100)`.

**Lifetime.** The drain threads are spawned in the `SearchService` constructor AFTER the worker thread has started, and torn down in the destructor by setting `m_stopDrain` and joining every drain thread BEFORE the queue mutex is deleted and while the vxcore context is still alive. Joining first prevents a drain thread from touching a half-destroyed queue or a freed context.

**Idle cost.** Because the `"vxcore.search"` queue is pre-created at `vxcore_context_create`, idle drain threads block on the queue's condvar (~0 CPU). There is no busy-spin and no need to guard against a missing queue.

**Degradation.** The initiating thread help-drains its own enqueued items, so a search stays correct even if this pool is absent or stalled. With no drain threads the search simply runs single-threaded; results, ordering, cancellation, and `max_results` are unaffected.
