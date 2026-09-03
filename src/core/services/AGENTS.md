# Agent Guidelines for src/core/services

Qt-side service layer wrapping the vxcore C library. Services here are the bridge between the GUI (controllers, widgets) and the vxcore backend. Most are thin handles around `XXXCoreService` types holding a `VxCoreContextHandle`; a few (e.g., `BufferService`, `SyncService`) add hook firing and async orchestration on top.

For the canonical service catalog, DI rules, and Buffer2/HookManager patterns, see the parent `src/core/AGENTS.md`.

## `core_services` is Qt-Widgets-free and VTextEdit-free (CONTRACT)

`core_services` (`src/core/services/CMakeLists.txt`) links **neither** `Qt::Widgets` **nor** `VTextEdit`. This is deliberate and load-bearing, not an accident:

- `core_services` is a STATIC lib consumed by ~80 pure-core test executables in `tests/core`, `tests/models`, and `tests/controllers`. `VTextEdit` is a SHARED lib that links `Qt::Widgets` **PUBLIC**, so linking it forced every one of those targets to ship `VTextEdit.dll` next to the test exe (and to hit the pre-`WinMain` loader-dialog trap documented in the root `AGENTS.md`).
- It also dragged QtWidgets into a layer that has no business owning GUI code, which is what `NotificationService` (below) exists to avoid.

`Qt::Gui` **is** allowed (`QGuiApplication`, `QFontDatabase`, `QKeySequence`, `QImageReader`). Network access goes through **`core_net`** (`src/net/networkutils.{h,cpp}` — `vnotex::NetworkUtils` / `NetworkReply` / `NetworkAccess`), a Qt-Core/Network-only static lib that replaced the former `<vtextedit/networkutils.h>` dependency. `core_net` is linked **PUBLIC** because `githubprovider.h` / `giteeprovider.h` expose `vnotex::NetworkReply` in their signatures.

If a service here needs to tell the user something, use one of these three sanctioned alternatives instead of adding a `QMessageBox`:

1. **Move the code to the widget layer** (`src/widgets/`). Reference: `MainWindowTaskContext` (`src/widgets/mainwindowtaskcontext.{h,cpp}`) needs `QInputDialog` for `promptString()`, so it lives in `src/widgets/` while the `ITaskContext` interface it implements stays in `src/core/services/`.
2. **Use `NotificationService`** (see below) — the Qt-Widgets-free in-memory notification store; all presentation happens in the widget layer.
3. **Propagate in-band via the `HookContext` out-param** overload of `HookManager::doAction(hook, args, HookContext *)`. Reference: `SyncService`'s `NotebookBeforeClose` handler stashes `syncCancelReason` metadata, `NotebookCoreService::closeNotebook(id, QString *p_errorMessage)` copies it out on hook cancellation, and `ManageNotebooksController::closeNotebook` surfaces it as `result.errorMessage` for the `ManageNotebooksDialog2` banner. The `doAction` out-param is written on **every** return path (including the recursion-guard and no-callbacks paths), so stale metadata cannot leak. Handlers that want first-non-empty-wins semantics must check `getMetadata(key)` before setting; the shared context otherwise gives last-writer-wins.

The compile error is the enforcement: losing the `Qt::Widgets` link removes the QtWidgets include directories, so a regression fails to compile rather than silently re-introducing the dependency.

## NotificationService

`NotificationService` (`notificationservice.{h,cpp}`) is an in-memory notification store: a `QObject` that is deliberately **Qt-Widgets-free** (only `<QObject>`, `<QDateTime>`, `<QHash>`, `<QVector>`, `std::function`) so it stays in `src/core/services`. It holds a `QVector<NotificationMessage>`, assigns a monotonic `quint64` id + timestamp in `notify()`, and emits `messageAdded` / `messageUpdated` / `messageDismissed` / `messageRemoved` / `messagesCleared`. All presentation (severity→icon mapping, toast, popup, badge) lives in the widget layer (`NotificationToast` / `NotificationButton2` / `NotificationPopup2`, see `src/widgets/AGENTS.md` § Notification System).

- `NotificationMessage` is a copyable value type carrying `Severity`, `Duration`, `Attention`, `m_category`, `m_dedupKey`, `m_details`, a `QVector<NotificationAction>` (each action = label + `std::function<void()>` + `m_dismissOnTrigger`), and the progress hints `m_progressPermille` / `m_progressIndeterminate`. It is registered via `Q_DECLARE_METATYPE` + `qRegisterMetaType` in the ctor so signals survive a queued (cross-thread) connection if a future producer calls `notify()` off the GUI thread.
- Current usage is GUI-thread only; the service has no internal locking. If you add an off-thread producer, keep the metatype registration and rely on auto/queued connections rather than adding a mutex.
- `dismiss()` marks a message dismissed (it stays in the list but is excluded from `activeCount()` and hidden by the popup); `clearAll()` removes all messages. `Duration` is a UI auto-hide hint only, not a retention policy.
- `update(id, msg)` replaces a message's content IN PLACE and emits `messageUpdated`. It preserves `m_id`, `m_timestamp`, `m_dismissed`, **`m_category` and `m_dedupKey`** — it never renumbers, re-stamps, resurrects a dismissed message, moves `activeCount()`, or moves a key to a different message — and returns `false` for an unknown id. `isActive(id)` is the companion predicate ("exists and not dismissed") producers check before updating.

### Attention: producers declare intent, the view owns placement

`Attention` is `Passive` (default) or `Interrupt`. A producer states how badly it needs to be seen; it never names a widget. The widget layer maps that to a surface.

**`Attention` defaults to `Passive` on purpose.** A producer that does not think about attention is quiet-but-badged, which is the safe failure direction. The cost is that a producer which *should* interrupt and forgets to say so is silent — so the per-site audit matters when adding one.

### Incidents: `m_dedupKey`, `renotify()` and retirement

A `dedupKey` names an **incident**, not a message. While a message with that key is active, `notify()` folds new content into it (emitting `messageUpdated`, returning the existing id) instead of appending. This is the anti-spam mechanism; it replaced the hand-rolled `QSet` guards `NotebookExplorer2` used to keep.

Because the toast is raised **only** by `messageAdded` carrying `Interrupt`, two rules follow:

1. **To re-interrupt, use `renotify()`.** It REMOVES the old generation (emitting `messageRemoved`) and posts a fresh one, so the new state arrives as `messageAdded`. Plain `notify()` on a live key can never interrupt. This is what makes a state change reliable even when it was not preceded by a passive phase — e.g. `UpdateController` replacing a superseded "Update Available" offer with the newer release's.
2. **Producers MUST retire an incident when it genuinely ends** (`dismissByDedupKey`). A missed retirement makes a recurring failure permanently quiet. The retirement boundaries for each subsystem live in `NotificationRouter` (`src/controllers/notificationrouter.cpp`).

There is deliberately **no escalation signal**. An earlier design emitted one when a dedup replacement raised `Passive → Interrupt`; it had two structural holes (a terminal state reached without an intervening passive phase never escalated, and `update()` could escalate an already-**dismissed** message back onto the screen). Retire-and-repost removes both.

### The index-erasure invariant (load-bearing)

`m_dedupIndex` maps key → id of the **active** message holding it. A message can outlive its *ownership* of a key: `notify(K)` → user `dismiss()`es it → `notify(K)` leaves a dismissed old generation and an active new one, both carrying `K`, with only the new one owning the index entry.

Therefore a key may be erased from the index **only while the index still points at that exact message** (`eraseDedupIndexIfOwned`, called from `dismiss()` and from eviction; `clearAll()` is the exception and wipes the whole index). An unconditional `remove(key)` while retiring the *old* generation would strip the *live* message's entry, so the next `notify(K)` would append a second active message and pop an unwanted toast.

### Retention

`c_maxMessages` (200) is a hard bound, enforced in `notify()` **before appending** with `>=` (a store holding exactly the cap is not "over cap", yet appending would put it one past). The incoming message is never an eviction candidate, so `messageAdded` always names a message that is actually stored.

`evictOneExistingMessage()` prefers, in order: oldest **dismissed**; else oldest active that is cheap to lose (**no actions and not `Persist`**); else oldest active outright. The middle tier exists so a flood of unique keys cannot silently delete the only "Restart to finish update" / "Open Sync Info…" affordance. Every eviction emits `messageRemoved(id)` — **not** `messageDismissed` — and every renderer must drop the id.


## Threading rules for SyncService

State-model counterpart (S0-S7, reconcile, disable cleanup): [Sync State Model](#sync-state-model) below.

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

Save workers and `SyncOps::triggerSync` share `NotebookIoGate` ([`notebookiogate.h`](notebookiogate.h)/[`.cpp`](notebookiogate.cpp)), a per-notebook async mutex, but they hold it for different windows. Save workers wrap their full `BufferCoreService::saveBuffer` call in `NotebookIoGate::ScopedLock(notebookId)`. `SyncOps::triggerSync` ([`syncops.cpp`](syncops.cpp)) splits the sync into two phases against [`ISyncNotebookService`](isyncnotebookservice.h): it acquires the gate, calls [`NotebookCoreService::syncStageOnly`](notebookcoreservice.h) (which wraps `vxcore_sync_stage_only` — StageAll + CommitIndex), releases the gate, then calls [`NotebookCoreService::syncNetworkPhase`](notebookcoreservice.h) (which wraps `vxcore_sync_network_phase` — FetchOrigin + RebaseOntoOrigin + PushOrigin) WITHOUT the gate held. This guarantees a sync never reads a half-flushed file, a save never lands inside someone else's `git add`/commit, and a save queued on the same notebook gets to run the instant the local commit lands instead of waiting on a network round-trip. The injection seam through `ISyncNotebookService` also makes the released-early property unit-testable without a real remote — see `tests/core/test_syncops_gate_release.cpp`. The full rationale lives in [Save Path Threading Contract](#save-path-threading-contract) below.

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

**Idempotent delete across platforms (issue #2718)**: `SyncCredentialsStore::deleteCredentials` normalizes `QKeychain::EntryNotFound` to success (emits `credentialsDeleted`, not `credentialsError`). macOS's Apple keychain `DeletePasswordJob` reports a missing-entry delete as an error (`errSecItemNotFound`), whereas Windows Credential Manager and libsecret return success; the normalization makes deleting a never-stored PAT a no-op everywhere. This matters because `NotebookAfterClose` unconditionally deletes the PAT even for notebooks that never enabled sync (e.g. the clone-staging notebook in `OpenNotebookController::cloneAndOpen`). Relatedly, `storeCredentials` failures emit a dedicated `credentialsStoreError` signal (not the generic `credentialsError`); the enable/update flows in `SyncService` filter on `credentialsStoreError` so a concurrent retrieve/delete error for the same notebook id cannot be misread as a store failure and abort the enable.

## SearchService drain pool

`SearchService` owns the pool of drain threads that empty vxcore's `"vxcore.search"` work queue (see [Search Threading Contract](#search-threading-contract) below). vxcore owns no search threads; this pool is VNote's side of that contract.

**Size.** `min(std::thread::hardware_concurrency(), 8)`, substituting `2` only when `hardware_concurrency()` returns `0` (count unknown). This is a fallback for the unknown case, not a floor: a genuine single-core host gets `1` drain thread. Each thread loops `vxcore_work_queue_process_next(ctx, "vxcore.search", 100)`.

**Lifetime.** The drain threads are spawned in the `SearchService` constructor AFTER the worker thread has started, and torn down in the destructor by setting `m_stopDrain` and joining every drain thread BEFORE the queue mutex is deleted and while the vxcore context is still alive. Joining first prevents a drain thread from touching a half-destroyed queue or a freed context.

**Idle cost.** Because the `"vxcore.search"` queue is pre-created at `vxcore_context_create`, idle drain threads block on the queue's condvar (~0 CPU). There is no busy-spin and no need to guard against a missing queue.

**Degradation.** The initiating thread help-drains its own enqueued items, so a search stays correct even if this pool is absent or stalled. With no drain threads the search simply runs single-threaded; results, ordering, cancellation, and `max_results` are unaffected.

## UpdateService

Mechanism half of the update check: the release API, the source-scoped host allowlist,
manual redirect walking, the response cap and cancellation. The full contract (source
selection, the "VNote never modifies its own install directory and never downloads
anything" invariant, the forbidden patterns) lives in the [Update Check](#update-check) section below; only the service-specific rules are
repeated here.

### It is a CHECK, and only a check

`UpdateService` fetches ONE JSON document and reports what it says. It has no install
directory, no download directory, no staging directory, no lease, no notion of "applying"
anything, and it writes nothing to disk. `UpdateInfo` carries exactly `updateAvailable`,
`currentVersion`, `latestVersion`, `releaseNotes` and `releaseUrl`.

The release's `assets[]` array is **ignored entirely**: no asset name is matched, no
`browser_download_url` is read, and no asset is ever requested. Do not add an install-dir
or download-dir parameter back, and do not reintroduce asset selection — the absence of both
is what fixes issue #2728 and what keeps this service free of a whole class of
file-ownership races. `testAssetsAreIgnoredEntirely` and `testTheCheckWritesNothingToDisk`
are the gates.

### No ConfigMgr2 dependency

`UpdateService` takes `currentVersion` as a plain value and never touches `ConfigMgr2`. This
is NOT a style preference: `core_configs` links `core_services`, so a dependency the other
way would be a CMake cycle. Every config-driven decision - `checkForUpdatesOnStart`, the 24 h
throttle (`lastUpdateCheckTime`), `skippedUpdateVersion` and the release source - therefore
lives in `UpdateController`, which is compiled into the `vnote` target and may use
`ConfigMgr2` freely.

Corollary: do NOT "fix" a future need for config inside the service by registering
`ConfigMgr2` with it. Add the policy to the controller and pass the decision down.

`UpdateService::Source` (`GitHub` | `Gitee`) is a plain enum on the service with
`setSource()` / `source()` and the `sourceFromString()` / `sourceToString()` converters;
`UpdateController::applyConfiguredSource()` PUSHES `CoreConfig::getUpdateSource()` in from the
constructor and again at the top of every `startCheck()`, so a Settings change takes effect
without a restart. `setSource()` is a no-op (with a `qWarning`) while `m_busy` is set:
switching origins mid-flight would let one check send its request to one forge and parse the
answer as the other's. Pinned by `testSourceChangeIsIgnoredWhileACheckIsRunning`.

**`sourceFromString()` defaults to Gitee.** Only an explicit case-insensitive `"github"`
selects GitHub; empty, absent and unrecognized values are Gitee. This mirrors
`CoreConfig::normalizeUpdateSource()` exactly - keep the two in step.

### Per-source endpoints

| | GitHub | Gitee |
|---|---|---|
| `apiLatestUrl()` | `https://api.github.com/repos/vnotex/vnote/releases/latest` | `https://gitee.com/api/v5/repos/vnotex/vnote/releases/latest` |
| `releasesPageUrl()` | `https://github.com/vnotex/vnote/releases` | `https://gitee.com/vnotex/vnote/releases` |
| `releasePageUrl(tag, htmlUrl)` | the API's `html_url`, but ONLY when it is a valid URL on an allowlisted host — it is handed straight to `QDesktopServices` and `UpdateDialog` has no empty-URL branch, so an absent/off-forge/plain-http value would render a dead (or hostile) button. Otherwise a `<releasesPageUrl>/tag/v<tag>` URL is synthesized | always synthesized `<releasesPageUrl>/tag/v<tag>` — Gitee's release JSON has no `html_url`. Verified against the live `https://gitee.com/vnotex/vnote/releases/tag/v4.3.0`; the `tag/` segment is NOT present in the asset download path, so do not "simplify" the two to share a base |
| `Accept` | `application/vnd.github+json, */*` | `application/json, */*` (Gitee rejects the vendor type) |

`m_apiLatestOverride` (the test seam) covers the API entry point, which is the only URL the
service ever requests.

### Outcomes, and who owns a result

A release whose `tag_name` is missing or empty is a `failed()` — the check genuinely could
not be performed. Everything else is a `checkFinished()`, including "you are up to date":
`updateAvailable` is simply `latest > current` under `QVersionNumber`, and `latestVersion` /
`releaseUrl` are populated either way so a manual check can say "up to date" and still link
the page.

**`checkForUpdates()` returns `bool`: whether the request was ACCEPTED.** A call made while
another check is in flight is dropped and returns `false`, emitting nothing. This return
value is load-bearing, not a convenience: `UpdateController` sets its manual-vs-startup mode
from it. When it did not, a user clicking "Check for Updates" during the silent startup
check re-labelled that *background* check's outcome — surfacing a modal dialog for a check
they never started, and a modal warning box for a failure that is supposed to be silent.

The controller keeps its own `m_checkInFlight` flag rather than trusting the service's
`m_busy`, because the worker releases `m_busy` before its queued terminal signal is
DELIVERED: a new check can be accepted while the previous result is still in the event
queue. The controller-side flag is cleared in the terminal slots, which is the only point
where the result and its mode are known to belong together.

### Threading

`checkForUpdates()` returns immediately and does its work on a `QtConcurrent` worker,
because `fetchToMemory()` blocks on a nested event loop.

- **`QNetworkAccessManager` is created on the WORKER'S STACK**, never as a member. QNAM is
  not thread-safe and belongs to the thread that created it, so every network helper takes
  it by reference. There is deliberately no QNAM member; adding one would reintroduce the
  cross-thread bug.
- Both signals (`checkFinished`, `failed`) are emitted through
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` so receivers see them on the GUI
  thread.
- **`m_busy` is released as the LAST statement of the worker**, after the terminal signal has
  been queued. Releasing it before the worker finishes lets check N+1 win the
  compare-exchange while check N is still running; a second `checkForUpdates()` in flight
  returns `false` and is DROPPED, not queued
  (`testASecondCheckIsIgnoredWhileOneIsRunning`).
- The destructor waits for **every** outstanding worker via `waitForWorkers()`, not just the
  most recently started one. A single stored `QFuture` is not enough: it can be replaced by
  the next check while the previous worker is still unwinding, and every worker holds a raw
  `this`. `m_workers` is guarded by `m_workerMutex`, snapshotted under the lock and waited on
  OUTSIDE it.

### Redirects, the response cap and the host allowlist

Redirects are followed MANUALLY (`QNetworkRequest::ManualRedirectPolicy` set on EVERY
request, because Qt 5 and Qt 6 differ in their default) so each hop can be checked. At most
`c_maxRedirects` hops, and there is no HTTPS -> HTTP downgrade path.

`allowedHosts(Source)` is **source-scoped and disjoint**, so a client on one forge can never
follow a redirect onto the other's hosts:

- GitHub: exact `api.github.com`, `github.com`, `codeload.github.com`, plus any host under
  `.githubusercontent.com`.
- Gitee: exact `gitee.com`, plus any host under `.gitee.com`.

`fetchToMemory()` enforces `c_maxApiResponseBytes` by accumulating on `readyRead` and
`abort()`ing the reply the moment the cap is crossed — NOT by buffering the whole body and
rejecting afterwards, which a server declaring a huge `Content-Length` could turn into an
unbounded allocation or a full-timeout stall.

Cancellation is POLLED every 250 ms inside the blocking request, because
`QNetworkReply::abort()` must run on the reply's own thread; without the poll, closing VNote
during a stalled check would block service teardown (and therefore `vxcore_context_destroy`)
for the whole 60 s timeout.

`testSetEndpointOverride` / `testSetExtraAllowedHost` exist for the local server harness in
`tests/core/test_updateservice.cpp`; the plain-HTTP exemption they enable applies ONLY to the
explicitly nominated host, and it is independent of the source.

### Test coverage

`tests/core/test_updateservice.cpp` drives the REAL `QNetworkAccessManager` against a local
`QTcpServer`. Two properties look incidental and are not, so please do not "simplify" them
away:

- `testOversizedApiResponseAbortsTheReply` serves a response declaring an 8 GB
  `Content-Length` and never closes the socket. It passes only if the cap ABORTS mid-stream;
  a buffer-then-reject implementation would hang until the 60 s request timeout.
- `testTheCheckWritesNothingToDisk` redirects the process working directory into a scratch
  tree, snapshots both it and the user's real Downloads folder around a check whose release
  object DOES carry `assets[]`, and asserts both trees are unchanged and that no asset /
  manifest / signature path was ever requested. That is the feature's central invariant
  expressed as a test; the CWD redirect is what catches a regression writing to a relative
  path.

### NotificationService fields consumed by the updater

`UpdateController` posts ONE notification per offer, keyed `update.available`, via
`NotificationService::renotify()` — the toast is raised only by `messageAdded` carrying
`Interrupt`, so a later check must replace the message rather than fold into it. The offer is
`Duration::Persist` with a single `Check Release` action; there is no progress bar and no
in-place update, because there is nothing to report progress on. See `src/widgets/AGENTS.md`
§ Notification System for the rendering side.

---

## Sync State Model

> Moved here from the root `AGENTS.md`. It overlaps the
> [Threading rules for SyncService](#threading-rules-for-syncservice) section above and
> `libs/vxcore/src/sync/AGENTS.md`; deduping the three is a pending follow-up.

Threading rules: see `libs/vxcore/src/sync/AGENTS.md` § Threading & Callback Contract.
Qt-side dispatch (single queue via `SyncWorkQueueManager` + `SyncOps`, coalescing, cancellation, auto-sync routing through `EventBridge::syncShouldRun`): see [Threading rules for SyncService](#threading-rules-for-syncservice) above. The per-notebook `autoSyncEnabled` flag (boolean, default true) is a pure on/off gate inside vxcore's `MaybeEnqueueSync`: when false, vxcore suppresses `sync.should_run` entirely. It carries no cadence. Auto-sync cadence is owned Qt-side by `SyncService`, which applies a trailing-throttle debounce keyed off the global `autoSyncDebounceSeconds` app-config value (stored in vxcore's `vxcore.json` but consumed only by VNote).

Notebook sync has 8 reachable states (S0-S7). Every controller, widget, and service that touches sync must reason in terms of these states. The state is the tuple of: on-disk JSON sync fields, PAT presence in the OS keychain, and runtime registration in vxcore's `states_` map.

### Canonical State Predicates

| State | syncEnabled (JSON) | syncBackend (JSON) | syncRemoteUrl (JSON) | PAT in keychain | states_ entry |
|---|---|---|---|---|---|
| S0 | false / absent | absent | absent | absent | absent |
| S1 | true | "git" | empty | maybe | absent |
| S2 | true | "git" | set | **absent** | absent |
| S3 | true | empty | maybe | maybe | absent |
| S4 | true | "git" | set | present | **absent** |
| S5 | true | "git" | set | present | present |
| S6 | false | absent | absent | **present** | absent |
| S7 | true | "git" | set | present | present + active sync |

S5 is the only "ready" state. S1-S4 and S6 are partial/inconsistent; S0 is cleanly disabled; S7 is in-flight.

F3.5 in-flight sub-states (fetching/resolving/pushing) are NOT modeled as separate SyncState values, they remain runtime properties exposed by SyncService progress signals while the notebook is in S7.

### Recovery Paths: bootstrapApply vs applyChanges

| Path | Use when | Behavior |
|---|---|---|
| `NotebookSyncInfoController::bootstrapApply(url, pat)` | Notebook is in S1/S2/S3/S4 (any partial state). Atomic enable for an existing notebook. | Calls `SyncService::enableSyncForNotebook` directly; on success persists `syncRemoteUrl` and triggers initial sync; on failure keeps notebook in current state (NO delete, unlike `NewNotebookController::bootstrapSync`). |
| `NotebookSyncInfoController::applyChanges(url, pat)` | Notebook is in S5 (registered). PAT refresh or URL change. | PAT-only update routes through `SyncService::updateCredentials`. URL change triggers `confirmUrlChangeRequested` signal and, on confirm, runs atomic disable+wipe `vx_notebook/vx_sync/`+re-enable. |

The dialog (`NotebookSyncInfoDialog2`) auto-routes to `bootstrapApply` when `m_bootstrapMode == true` OR when `SyncService::isSyncRegistered(id) == false`. This is defense in depth: even when a caller bypasses the bootstrap entry point, partial-state notebooks still get the atomic path.

### Reconcile Semantics

`SyncService::reconcileSyncForNotebook` is called by `MainWindowAfterStart` and on notebook open to lift S4 notebooks (disk-complete, runtime-absent) into S5.

Key invariants (`src/core/services/syncservice.cpp:858-970`):
- `m_reconcileAttempted.insert(id)` happens **after** all precondition checks pass (line 893), not before. The disk-enabled check (line 869), idempotence guard (line 875), and complete-config check (line 884) all run first; any of them returning early leaves the attempted set untouched. Precondition failures therefore do NOT block future retries. Concrete consequence: a notebook in S1/S3 (enabled but no backend/url, or no backend) hits the `incomplete config` branch at line 885, emits `reconcileFinished(VXCORE_ERR_INVALID_PARAM)`, and is NOT marked attempted. When the user later supplies the missing URL via `bootstrapApply` and the notebook reaches S4, the very next reconcile trigger (notebook open or app start) will pass the precondition check and proceed.
- `m_reconcileAttempted.remove(id)` fires on transient PAT fetch failure (line 945) so the next reconcile call retries. The same key is re-cleared by `updateCredentials` (line 969) before manually re-driving reconcile, so a user re-entering a fresh PAT never hits the "already attempted" guard.
- No remove on success (notebook is registered; no retry needed).
- Idempotence check at line 875 prevents duplicate in-flight reconciles when `MainWindowAfterStart` and `NotebookAfterOpen` race.

**Post-reconcile freshness gate (auto-sync on open / app start).** After reconcile's `SyncOps::enableSync` work item returns `VXCORE_OK`, `SyncService::maybeTriggerPostReconcile(notebookId)` (`src/core/services/syncservice.cpp`) optionally enqueues a follow-up `triggerSyncNow` so the notebook is auto-synced when the user reopens VNote (or opens a notebook for the first time in the session) after remote changes. Closes the multi-device staleness window where reconcile alone only registered the notebook and the first `FetchOrigin` waited for the next save / manual Sync Now. The gate skips when: shutdown is in progress; the notebook is no longer enabled or registered; a sync is already in flight for the notebook; or the per-device last successful sync timestamp is newer than `kPostReconcileFreshnessMs` (2 minutes — covers rapid open/close cycles without thrashing). Both L1 (`onMainWindowAfterStart`, per-notebook sweep) and L2 (`onNotebookAfterOpen`, single notebook) inherit this behavior since both call `reconcileSyncForNotebook`. Full rationale and test seams live in [Post-reconcile freshness gate (`maybeTriggerPostReconcile`)](#post-reconcile-freshness-gate-maybetriggerpostreconcile) above.

### bootstrapAndPersist Rollback × Reconcile

`SyncService::bootstrapAndPersist` (`src/core/services/syncservice.cpp:409-508`) is the atomic enable+persist path used by `NewNotebookController` (W13.4, F1.6). On persist failure AFTER vxcore enable already succeeded, it issues a rollback by calling `disableSyncForNotebook`, which per "Disable Cleanup" below clears the three flat sync JSON keys then deletes the keychain entry.

Interaction with reconcile:
- **Rollback succeeds** (the common case): the notebook returns to clean S0. The disk-enabled check at `reconcileSyncForNotebook` line 869 fails immediately, the function early-returns, `m_reconcileAttempted` is never touched, and reconcile is correctly a no-op. The notebook needs a fresh user-initiated bootstrap, not silent re-registration. This is the intended recovery story.
- **Rollback fails** (rare; both persist AND disable failed, logged at `qCritical` line 497): the notebook is left in a partial state. If JSON still has all three keys set (vxcore enable succeeded, JSON write succeeded for some keys, then disable failed leaving keys intact), the notebook is effectively in S4. On the next reconcile trigger, all precondition checks pass, `m_reconcileAttempted` gets set, and reconcile will attempt to register the notebook. This is the expected recovery path for that edge case; the loud `qCritical` log gives operators a chance to investigate.

The key property: rollback NEVER causes reconcile to silently resurrect a notebook the user wanted disabled. Either rollback succeeded (so disk is clean and reconcile bails) or rollback failed (so disk truthfully says "enabled" and reconcile correctly tries to complete the job).

### Disable Cleanup

`SyncService::disableSyncForNotebook` on `VXCORE_OK` clears all three flat sync keys (`syncEnabled`, `syncBackend`, `syncRemoteUrl`) from notebook JSON BEFORE deleting the keychain entry (`src/core/services/syncservice.cpp:246-290`). On failure, JSON is preserved for retry. This closes the "resurrection trap" where a disabled notebook would reappear as S6 (orphan PAT) or S1 (orphan disk fields) on next app start.

For the full table of all five credential cleanup sites (bootstrap rollback, `bootstrapAndPersist` rollback, notebook removal, sync disable, S6 startup sweep) and the "when fires / when does NOT fire" matrix, see [Credential Cleanup Invariants](#credential-cleanup-invariants) above.

### Startup S6 Sweep

`SyncService::onMainWindowAfterStart` (`src/core/services/syncservice.cpp:828-854`) sweeps S6 orphans before reconciling. For each notebook it iterates, if `!isSyncEnabled(id) && m_credentialsStore->hasCredentials(id)` (the S6 predicate: disk says disabled but a PAT is still in the keychain), it calls `m_credentialsStore->deleteCredentials(id)` to drop the orphan PAT. This handles the scenario where a previous session's disable succeeded inside vxcore but the app crashed (or was killed) before the keychain delete completed, leaving an orphan PAT that the new "disable clears JSON then keychain" ordering would otherwise not catch on its own.

The sweep runs BEFORE `reconcileSyncForNotebook(nbId)` on each notebook, so by the time reconcile examines the notebook the keychain state is consistent with disk truth.

### Re-enable UI Affordance

S0 notebooks expose a re-enable surface via the same Sync button and Sync Info menu used for S5 (`src/widgets/notebookexplorer2.cpp:1512-1635`). For S0:
- Button label: "Enable Sync" (distinct from "Sync Now" for S5)
- On click: opens `NotebookSyncInfoDialog2` with `setBootstrapMode(true)` and all fields empty
- Sync Info menu item enabled regardless of `syncEnabled` (dialog opens in bootstrap mode with disable button hidden)

Without this affordance, users who disable sync cannot re-enable without recreating the notebook.

### Sync Architecture Layers

VNote consumes vxcore as an embedded library following the contract documented in `libs/vxcore/AGENTS.md` § Library Integration Contract. Vxcore emits facts (events, dirty marks); VNote owns sync scheduling policy via `SyncService` + `SyncWorkQueueManager` (see [Threading rules for SyncService](#threading-rules-for-syncservice) above). Vxcore must NOT contain Qt-side concerns (no `QTimer`, no `QObject`, no scheduling policy); VNote must NOT bypass the contract by reaching into vxcore internals (no direct backend calls, no touching libgit2, no `states_` mutation). The 4-layer ownership table lives in the vxcore doc to avoid duplication.

**Qt-side scheduling shape (post May 2026 audit, debounce added June 2026).** `SyncService::onSyncShouldRun` no longer enqueues immediately on the auto-sync path. It keeps the shutdown / readiness / auth-circuit-breaker guards, then routes through a per-notebook trailing-throttle debounce: when the global cadence `autoSyncDebounceSeconds` is `0` it enqueues immediately, otherwise it arms a per-notebook single-shot `QTimer` (`armOrIgnoreDebounce`) whose delay is `lastSyncMs + autoSyncDebounceSeconds*1000 - now`. An already-active timer is kept (the burst is absorbed), and `onDebounceTimeout` re-reads the cadence and re-checks freshness at fire time, re-arming if the last sync is still inside the window before finally calling the shared `enqueueAutoSync` body (`coalesceKey="trigger"`). The cadence is read on demand from `ConfigCoreService::getAutoSyncDebounceSeconds()` (clamped `[0, 86400]`), so config edits take effect without restart. Manual "Sync Now" (`triggerSyncNow`) and the post-reconcile freshness trigger BYPASS the debounce entirely. The coalesce key still dedupes whatever lands in the queue. The debounce lives one layer ABOVE `SyncWorkQueueManager`, so queue semantics are unchanged. vxcore's per-notebook `autoSyncEnabled` is a boolean on/off gate only (it suppresses `sync.should_run` when false) and carries no schedule.

---

## Save Path Threading Contract

> Moved here from the root `AGENTS.md`. Related:
> [Save / sync I/O serialization](#save--sync-io-serialization) above.

The `Buffer2` / [`BufferService`](bufferservice.h) auto-save path used to call `vxcore_buffer_save` inline on the UI thread, so any slow filesystem operation (large file flush, virus scanner, network drive, antivirus quarantine) froze the editor. That synchronous call now runs on a worker via [`BufferSaveQueue`](buffersavequeue.h). The UI thread's job is reduced to: snapshot the current content plus a monotonically increasing revision, call `BufferSaveQueue::enqueue(...)`, and return. No disk I/O on the UI thread.

Save work and any git-stage / git-commit work on the SAME notebook are serialized by [`NotebookIoGate`](notebookiogate.h), a per-notebook async mutex. `BufferSaveQueue` workers acquire `NotebookIoGate::ScopedLock(notebookId)` for the full duration of their disk write. [`SyncOps::triggerSync`](syncops.cpp) now runs sync as two phases: it holds the gate ONLY around [`vxcore_sync_stage_only`](../../../libs/vxcore/include/vxcore/vxcore.h) (StageAll + CommitIndex, working-tree-touching), then releases it BEFORE calling [`vxcore_sync_network_phase`](../../../libs/vxcore/include/vxcore/vxcore.h) (FetchOrigin + RebaseOntoOrigin + PushOrigin). The result: a sync never reads a half-written file, a save never lands inside someone else's `git add`/commit, AND a queued save on the same notebook resumes the moment the local commit lands, regardless of how long the network round-trip takes.

vxcore's `mark_dirty` → `MaybeEnqueueSync` → `Emit("sync.should_run")` chain remains synchronous on the caller thread BY DESIGN. In steady state it is microseconds, and pushing it onto another thread would buy nothing while costing event-ordering guarantees. **This contract does NOT change vxcore.** The threading discipline is consumer-side only: keep `vxcore_buffer_save` off the UI thread, and the `mark_dirty` tail it triggers stays off the UI thread for free.

> **Forbidden Patterns (post-T7):**
> - Calling `vxcore_buffer_save` directly from the UI thread. Use [`BufferSaveQueue::enqueue`](buffersavequeue.h) instead.
> - Touching a notebook's working tree (save, stage, commit, checkout) without holding `NotebookIoGate::ScopedLock(notebookId)`.

---

## Cross-Notebook Node Transfer

`NodeTransferService` is the only VNote-side entry point for cross-notebook Copy/Paste and
Cut/Paste. It keeps application policy outside vxcore and keeps dialogs outside services.

Per item, the service flushes matching open buffers and comment participants for Copy, blocks Move
when an open non-virtual buffer is in the selected subtree, atomically leases both notebook IDs in
`SyncWorkQueueManager`, and calls `NotebookCoreService::prepareNodeTransfer()` with no IO gate held.
After preparation it fires the cancellable before-transfer hook, acquires both `NotebookIoGate`
locks in sorted notebook-ID order with bounded try-locks, revalidates buffer/comment state, and runs
the callback-free commit. Gates are released before the maintenance lease, and hooks are fired only
after both are released. Never pump events, invoke callbacks, emit signals, or fire hooks while an
IO gate is held.

The explorer controller owns application-local clipboard retention and model refresh. It emits one
list-valued request for cross-notebook Paste; `NotebookExplorer2` owns one modal progress dialog and
calls the explorer's apply method. Successful Cut entries are removed individually, failed entries
remain, and `CopiedSourceRetained` retains its resume token so retry calls finalization only and can
never import a duplicate. The existing same-notebook path and drag/drop route remain separate.

vxcore owns storage facts: bundled-notebook validation, private snapshots, fresh IDs, assets/link and
tag fidelity, atomic conflict naming/publication, source-removal journaling, recovery, and mutation
events. VNote must not reproduce those mechanisms; vxcore must not know about buffers, comments,
clipboard state, sync scheduling, hooks, progress dialogs, or policy defaults.

---

## Search Threading Contract

> Moved here from the root `AGENTS.md`. VNote's side of this contract is the
> [SearchService drain pool](#searchservice-drain-pool) above.

Content search in vxcore owns NO thread pool. As of the streaming-search work, `vxcore_search_content` / `vxcore_search_content_ex` / `vxcore_search_content_streaming` enqueue ONE work item per FILE-CHUNK (default `kDefaultSearchChunkSize = 64` files, tunable via the streaming `batch_size` parameter) onto a dedicated `"vxcore.search"` `WorkQueue` that is pre-created at `vxcore_context_create`. The CALLER's threads drain that queue, and the initiating thread help-drains its own enqueued items (caller-helps-drain: it loops `ProcessNext(5ms)` until the batch is done). VNote's [`SearchService`](searchservice.h) owns the drain pool that loops `vxcore_work_queue_process_next(ctx, "vxcore.search", 100)`. A search that fits in a single chunk (`fileCount <= batch_size`, i.e. ≤ 64 files by default) runs inline sequentially on the calling thread; larger searches fan out one queued item per chunk. Chunking subsumes the former `kParallelSearchThreshold` (was 50 files): the chunk boundary is now both the parallelism unit AND the incremental-delivery unit. This coarsens parallelism granularity for medium searches versus the old per-file fan-out — an accepted tradeoff to unify the blocking and streaming code paths; lower `batch_size` for finer-grained parallelism.

The initiating thread's self-drain is the correctness floor: a consumer that provides NO external drain threads still gets correct, single-threaded results, and extra drainers only add parallelism. Cancellation, `max_results`, and result ordering are preserved across both paths; an exception thrown mid-scan is caught and surfaces as `VXCORE_ERR_UNKNOWN`.

This mirrors the vxcore/VNote ownership split used by sync: vxcore emits per-file search work as facts, VNote owns the drain policy. No vxcore-owned threads remain, the former `BS::thread_pool` search pool having been removed.

---

## Update Check

> Moved here from the root `AGENTS.md`. The mechanism-only notes in
> [UpdateService](#updateservice) above are the service-scoped subset of this section.

VNote checks a forge for a newer release and, when one exists, tells the user and offers
the **release page**. That is the whole feature.

> **VNote never modifies its own install directory, and never downloads anything.** There is
> no lease file, no staging tree, no journal, no swap, no restart-to-apply, no downloader,
> and nothing is ever extracted or executed. The only thing the check writes is the
> `lastUpdateCheckTime` / `skippedUpdateVersion` config values. This invariant is what makes
> a read-only install location (`/usr/bin`, Program Files, a read-only DMG) launchable
> (issue #2728) — do not reintroduce install-tree mutation, or a downloader, without
> replacing this section.

The built-in incremental updater that used to live here (manifest verification, delta
chains, `UpdateInstaller`, `UpdateLease`, `ZipExtractor`, the vendored `miniz` /
`minicrypto`) has been removed. **Release CI still publishes manifests, minisign signatures
and delta ZIPs unchanged**; they are now the interface for a future *external* updater, not
something this client consumes. See `docs/update-signing.md`.

### Ownership map

| Layer | Unit | Responsibility |
|---|---|---|
| Service | [`UpdateService`](updateservice.h) | release API, per-source endpoints/headers, manual redirect walking, source-scoped host allowlist, response cap, cancellation, worker lifetime |
| Controller | [`UpdateController`](../../controllers/updatecontroller.h) | ALL policy: configured source, 24 h throttle, skipped version, manual-vs-startup surface, failure loudness |
| View | [`UpdateDialog`](../../widgets/dialogs/updatedialog.h) | version, notes, Open Release Page / Skip This Version / Later |
| View | [`NotificationPopup2`](../../widgets/notificationpopup2.h) | the startup surface: one persistent, interrupting "Update Available" row with a Check Release action |

`UpdateService` deliberately does NOT depend on `ConfigMgr2`: `core_configs` links
`core_services`, so the reverse dependency would be circular. The installed version is
injected and every config-dependent decision lives in `UpdateController`, which pushes the
configured source down via `setSource()`.

`UpdateInfo` carries exactly `updateAvailable`, `currentVersion`, `latestVersion`,
`releaseNotes` and `releaseUrl`. The release's `assets[]` array is **ignored entirely** —
no asset is selected and no asset URL is ever requested
(`testAssetsAreIgnoredEntirely` pins this).

`checkForUpdates()` returns whether the request was ACCEPTED (a call made during another
check is dropped and returns `false`). `UpdateController` sets its manual-vs-startup mode
**only for an accepted request**, and keeps its own `m_checkInFlight` until the terminal slot
runs. Both are load-bearing: without them a manual click during the silent startup check
re-labels that background check's outcome, turning a failure that must stay silent into a
modal warning box.

### Release source (GitHub or Gitee)

`CoreConfig::updateSource` (`"github"` | `"gitee"`, Settings › General) selects the forge.
**The default is Gitee**: `normalizeUpdateSource()` returns `"github"` only for an explicit
case-insensitive `"github"`, and `"gitee"` for empty, absent or unrecognized values.
`UpdateService::sourceFromString()` applies the same rule — keep the two in step.
`CoreConfig::toJson()` always persists the key, so an **existing installation keeps whatever
it already had** (in practice GitHub); only fresh installs and hand-cleared configs get the
new default. No migration is performed.

| | GitHub | Gitee |
|---|---|---|
| latest-release API | `https://api.github.com/repos/vnotex/vnote/releases/latest` | `https://gitee.com/api/v5/repos/vnotex/vnote/releases/latest` |
| `Accept` header | `application/vnd.github+json, */*` | `application/json, */*` |
| release page | the API's `html_url`, validated against the allowlist first (it is handed to `QDesktopServices`); an absent, malformed or off-forge value falls back to a synthesized `<releases>/tag/v<tag>` | synthesized `https://gitee.com/vnotex/vnote/releases/tag/v<tag>` (Gitee's JSON has no `html_url`; the pattern is verified against the live v4.3.0 page, and note the `tag/` segment the asset download path does **not** have) |
| host allowlist | exact `api.github.com`, `github.com`, `codeload.github.com` + suffix `.githubusercontent.com` | exact `gitee.com` + suffix `.gitee.com` |

The allowlists are **disjoint and source-scoped**: a client on one source must never follow
a redirect to the other's hosts. Redirects are followed MANUALLY
(`QNetworkRequest::ManualRedirectPolicy` set on EVERY request, because Qt 5 and Qt 6 differ
in their default), at most `c_maxRedirects` hops, with no HTTPS→HTTP downgrade.

### Update check threading

`checkForUpdates()` returns immediately and does its work on a `QtConcurrent` worker,
because it blocks on a nested event loop.

- **`QNetworkAccessManager` is created on the WORKER'S STACK**, never as a member. QNAM is
  not thread-safe and belongs to the thread that created it, so every network helper takes it
  by reference. There is deliberately no QNAM member.
- Both signals (`checkFinished`, `failed`) are emitted through
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` so receivers see them on the GUI
  thread.
- **`m_busy` is released as the LAST statement of the worker**, after the terminal signal has
  been queued. Releasing it earlier would let the next check start while this one is still
  running. A second `checkForUpdates()` while one is in flight is DROPPED, not queued.
- The destructor calls `cancel()` and then waits for **every** outstanding worker
  (`waitForWorkers()`), not just the most recent one: workers hold a raw `this`, and a single
  stored `QFuture` could be replaced while the previous worker was still unwinding.
- Cancellation is POLLED every 250 ms inside a blocking request so service teardown is never
  blocked for the full request timeout.
- The API response cap **aborts** the reply mid-stream rather than buffering the whole body
  and rejecting afterwards; `testOversizedApiResponseAbortsTheReply` proves this by declaring
  an 8 GB `Content-Length` and never closing the socket.

### Forbidden patterns

- **Never** download, extract, execute or install a release artifact.
- **Never** write outside the configuration directory as part of an update check.
- **Never** read `assets[]`; the release page is the only affordance.
- **Never** give `UpdateService` a `ConfigMgr2` dependency — add the policy to the controller.

---

## Comment store (`comments.json`)

`CommentService` ([`commentservice.h`](commentservice.h) / [`.cpp`](commentservice.cpp)) owns the
per-file comment sidecar. Value types are in [`commenttypes.h`](commenttypes.h).

It is **file-type-agnostic on purpose**. Only `Comment::m_anchor` carries file-type knowledge, so
Markdown (or anything else) reuses the same service, schema, controller and dock by adding an
anchor type — no rework of the store, the queue or the UI.

### Where the store lives

| Case | Path | Takes `NotebookIoGate`? |
|---|---|---|
| Bundled notebook | `getAttachmentsFolder()` + `/comments.json` | **yes** |
| Raw notebook | `<filename>.comments.json` beside the file | no |
| External file (empty `notebookId`) | `<filename>.comments.json` beside the file | no |

One rule: **the store travels with the file.**

`NotebookCoreService::getAttachmentsFolder()` **already returns `<assets-root>/<file-uuid>` and
does NOT create the directory.** Use it as the single source of truth. Do NOT reconstruct the path
from `getFileInfo()[kJsonKeyId]`, and do not assume the directory exists — the worker creates it
under the gate, at write time. `tests/core/test_commentservice.cpp` asserts the directory is
absent at resolve time precisely so a "helpful" eager mkdir cannot creep in.

### Writes are asynchronous and coalescing

This is a correctness requirement, not a performance tweak. `NotebookIoGate::ScopedLock` is
**worker-thread-only** and blocks on construction, so a synchronous save from the dock or the
QWebChannel bridge would either freeze the GUI or have to degrade to `ScopedTryLock` and drop
edits under contention.

The shape mirrors [`BufferSaveQueue`](buffersavequeue.h):

1. the UI serializes a validated `CommentSet` to bytes and `scheduleSave()` returns;
2. a pool worker takes `NotebookIoGate::ScopedLock(notebookId)` — **bundled stores only**;
3. the worker `mkpath`s the parent, then commits with `QSaveFile`;
4. completion is queued back to the service's owning thread;
5. a newer pending snapshot **replaces** an older pending one for the same file;
6. `shutdown()` drains deterministically and never discards a pending snapshot.

Reads are synchronous: the file is small and `QSaveFile` commits by rename, so a reader sees
either the old file or the new one, never a half-written one.

### Read-only notebooks

vxcore rejects asset writes on a read-only notebook **before touching disk**
(`vxcore_buffer_api.cpp`), and a direct `QSaveFile` bypasses that guard. `scheduleSave()`
therefore re-applies it via `NotebookCoreService::isNotebookReadOnly()` and emits
`saveRejectedReadOnly` **before any mutex, queue insertion or worker dispatch**, so nothing is
written. The UI surfaces this **without marking the buffer modified** — comments are not buffer
content and the PDF genuinely never changes.

### `storeDirty` → `SyncService`

`comments.json` is written with a plain `QSaveFile`, so vxcore emits **no** `file.saved` event and
therefore no `sync.should_run`. Without a nudge, a sidecar in a synced notebook would sit
uncommitted until some unrelated edit happened to trigger a sync.

`CommentService::storeDirty(notebookId)` is wired in `main.cpp` to
`SyncService::notifyWorkingTreeDirty()`, which routes into the **ordinary auto-sync path** —
deliberately not `triggerSyncNow`. A sidecar write is not user intent to sync, so it inherits every
guard (shutdown, readiness, auth circuit-breaker), the per-notebook trailing-throttle debounce and
the `"trigger"` coalesce key. A burst of comment edits costs at most one network round-trip per
debounce window.

### Sibling lifecycle

A bundled store follows its file for free (the UUID folder is stable). A **sibling** store does
not: renaming `paper.pdf` would orphan `paper.pdf.comments.json`. `CommentService` subscribes to
`NodeAfterRename` / `NodeAfterMove` / `NodeAfterDelete` and moves or removes the sidecar, for
`Kind::Sibling` only.

These are **Qt-side `HookManager` hooks fired by `NotebookCoreService`**, not vxcore events, so
they fire for raw notebooks too (vxcore's "raw notebooks emit no metadata events" note does not
apply here).

**Collision policy: the mover wins the name, the orphan stays put.** When the destination sidecar
already exists it is NOT overwritten — destroying someone's comments to satisfy a move is never
acceptable. The stale file is left for manual recovery and logged.

### Schema and forward compatibility

Full schema: the header comment of [`commenttypes.h`](commenttypes.h).

**An older PDF-only build must round-trip a newer build's comments untouched.** Three mechanisms:

- `Comment::m_anchor` is stored as a **raw `QJsonObject`** and never rebuilt from typed fields, so
  an unknown `anchor.type` survives verbatim (and `Comment::isValid()` returns **true** for it — it
  is valid-but-opaque, merely not renderable here);
- unknown top-level keys are preserved in `m_extraKeys`, on both the comment and the document;
- a known key always wins over `m_extraKeys` on write, so the preserved blob cannot shadow a typed
  field.

`toJson()` emits a **stably ordered** document (anchor type, then page, then id) so a git/sync diff
shows only what changed and a conflict stays readable. The ordering is total for unknown anchor
types too (page defaults to `-1`).

`color` is a **semantic token**, never a literal hex: highlights are resolved by `ThemeService` and
injected into the PDF template as CSS custom properties. An unknown or literal color normalizes to
the default rather than rendering unstyled.

### PDF tool options (`PdfToolOptions`)

`PdfToolOptions` (in [`commenttypes.h`](commenttypes.h)) is the **single
normalization choke point** for the three PDF annotation tools' persisted
settings. Both `PdfViewerConfig` (`fromJson`, `setToolOptions`) and
`PdfViewerAdapter::setToolOptions` call it; a second copy of the policy would
eventually disagree with the first.

The tool keys — `highlight`, `ink`, `freetext` — are the **same strings**
`PdfViewerAdapter::toolToString()` and the web side already use. One vocabulary
end to end means a value round-trips config → C++ → JS with no translation
table to drift. `color` is carried by every tool; `width` and `opacity` by ink
only and `fontSize` by free text only, and the serialized object **omits** the
key for a tool that does not carry it.

The contract:

| Input | Result |
|---|---|
| Key absent | the freshly initialized per-tool default |
| Wrong JSON type (string where a number belongs, etc.) | treated as absent → default |
| Colour not in `CommentColor::all()` (incl. a literal hex) | **default** colour |
| Width / font size / opacity non-finite (NaN, Inf) | **default** for that field |
| Width / font size / opacity finite but out of range | **clamped** to `PdfInkAnchor::min/maxWidth()`, `PdfFreeTextAnchor::min/maxFontSize()` or `PdfInkAnchor::min/maxOpacity()` |

The split is deliberate: a non-finite number carries no intent to preserve,
whereas "width 1e9" plainly means "as thick as possible" and clamping respects
it. Clamping to the **anchor validators'** bounds also guarantees config can
never express an anchor `PdfInkAnchor::isValid()` / `PdfFreeTextAnchor::
isValid()` would reject.

`PdfViewerConfig::fromJson()` **resets to defaults before overlaying**, so
calling it twice with different objects cannot retain stale state from the
first call. Tests assert the **exact** normalized value — the getter, the
serialized JSON and the adapter signal payload — never merely "the resulting
anchor validates", which passes for both a default and a clamped value.

### Anchor types

| Type | Geometry | Body |
|---|---|---|
| `pdf-quads` | `page` + text-selection quads in PDF page space, plus the quoted `text` | `Comment::m_text` (optional note) |
| `pdf-ink` | `page` + `strokes` (flat `[x0,y0,x1,y1,...]` polylines) + `width`, all in PDF page space, plus an OPTIONAL `opacity` | optional note |
| `pdf-freetext` | `page` + `x`/`y` + `fontSize` | **the box's visible text** — editing it in the dock and on the page are the same operation |

`isKnownAnchorType()` / `isAnchorStructurallyValid()` / `anchorPage()` dispatch on
`anchor.type`. **Add a new type to all three**, or it will render but never persist (or
sort into a random position). `isAnchorStructurallyValid()` returns **true** for an unknown
non-empty type — valid-but-opaque, carried through untouched — and false for a typeless one.

All geometry is stored in **PDF page space**, never CSS pixels, so zoom/rotate/resize only
re-project. Every coordinate is finiteness-checked: a NaN would silently poison the overlay
projection instead of failing loudly.

**`pdf-ink`'s `opacity` is optional, and absent means `1.0`.** An anchor written by a build
that predates the field must stay valid and render solid, so `PdfInkAnchor::isValid()` accepts
its absence — but a **present** value must be a finite double in `[minOpacity, maxOpacity]`,
otherwise the anchor is **rejected**. Rejection rather than clamping is deliberate: the
adapter copies an inbound anchor **verbatim** (`PdfViewerAdapter::requestAddComment`), so
clamping there would have to be a second copy of the policy. Config-side values still clamp,
via the `PdfToolOptions::normalize` table above.

These keys are Qt-only — vxcore never reads `comments.json` — so they stay **out** of
`<vxcore/notebook_json_keys.h>` and out of `test_json_key_drift`'s gated list.

Coverage: `tests/core/test_commentservice.cpp`.
