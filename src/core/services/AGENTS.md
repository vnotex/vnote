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

`NotificationService` (`notificationservice.{h,cpp}`) is an in-memory notification store: a `QObject` that is deliberately **Qt-Widgets-free** (only `<QObject>`, `<QDateTime>`, `<QVector>`, `std::function`) so it stays in `src/core/services`. It holds a `QVector<NotificationMessage>`, assigns a monotonic `quint64` id + timestamp in `notify()`, and emits `messageAdded` / `messageDismissed` / `messagesCleared`. All presentation (severity→icon mapping, popup, badge) lives in the widget layer (`NotificationButton2` / `NotificationPopup2`, see `src/widgets/AGENTS.md` § Notification System).

- `NotificationMessage` is a copyable value type carrying `Severity`, `Duration`, a `QVector<NotificationAction>` (each action = label + `std::function<void()>` + `m_dismissOnTrigger`), and the progress hints `m_progressPermille` / `m_progressIndeterminate`. It is registered via `Q_DECLARE_METATYPE` + `qRegisterMetaType` in the ctor so `messageAdded` survives a queued (cross-thread) connection if a future producer calls `notify()` off the GUI thread.
- Current usage is GUI-thread only; the service has no internal locking. If you add an off-thread producer, keep the metatype registration and rely on auto/queued connections rather than adding a mutex.
- `dismiss()` marks a message dismissed (it stays in the list but is excluded from `activeCount()` and hidden by the popup); `clearAll()` removes all messages. `Duration` is a UI auto-hide hint only, not a retention policy.
- `update(id, msg)` replaces a message's content IN PLACE and emits `messageUpdated`. It preserves `m_id`, `m_timestamp` and `m_dismissed` — it never renumbers, re-stamps, resurrects a dismissed message, or moves `activeCount()` — and returns `false` for an unknown id. `isActive(id)` is the companion predicate ("exists and not dismissed") producers check before updating. This is what lets the updater carry one notification from offer → progress → success/failure instead of spamming four.

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

**Idempotent delete across platforms (issue #2718)**: `SyncCredentialsStore::deleteCredentials` normalizes `QKeychain::EntryNotFound` to success (emits `credentialsDeleted`, not `credentialsError`). macOS's Apple keychain `DeletePasswordJob` reports a missing-entry delete as an error (`errSecItemNotFound`), whereas Windows Credential Manager and libsecret return success; the normalization makes deleting a never-stored PAT a no-op everywhere. This matters because `NotebookAfterClose` unconditionally deletes the PAT even for notebooks that never enabled sync (e.g. the clone-staging notebook in `OpenNotebookController::cloneAndOpen`). Relatedly, `storeCredentials` failures emit a dedicated `credentialsStoreError` signal (not the generic `credentialsError`); the enable/update flows in `SyncService` filter on `credentialsStoreError` so a concurrent retrieve/delete error for the same notebook id cannot be misread as a store failure and abort the enable.

## SearchService drain pool

`SearchService` owns the pool of drain threads that empty vxcore's `"vxcore.search"` work queue (see the root [Search Threading Contract](../../../AGENTS.md#search-threading-contract)). vxcore owns no search threads; this pool is VNote's side of that contract.

**Size.** `min(std::thread::hardware_concurrency(), 8)`, substituting `2` only when `hardware_concurrency()` returns `0` (count unknown). This is a fallback for the unknown case, not a floor: a genuine single-core host gets `1` drain thread. Each thread loops `vxcore_work_queue_process_next(ctx, "vxcore.search", 100)`.

**Lifetime.** The drain threads are spawned in the `SearchService` constructor AFTER the worker thread has started, and torn down in the destructor by setting `m_stopDrain` and joining every drain thread BEFORE the queue mutex is deleted and while the vxcore context is still alive. Joining first prevents a drain thread from touching a half-destroyed queue or a freed context.

**Idle cost.** Because the `"vxcore.search"` queue is pre-created at `vxcore_context_create`, idle drain threads block on the queue's condvar (~0 CPU). There is no busy-spin and no need to guard against a missing queue.

**Degradation.** The initiating thread help-drains its own enqueued items, so a search stays correct even if this pool is absent or stalled. With no drain threads the search simply runs single-threaded; results, ordering, cancellation, and `max_results` are unaffected.

## UpdateService

Mechanism half of the incremental updater: eligibility, network, planning, download and
staging. The apply half lives in [`UpdateInstaller`](../updateinstaller.h) and runs AFTER
this service has been destroyed. The full contract (manifest format, staging layout, lease
protocol, journal invariants, the two-rename executable swap and the accepted residual
risks) is in the root [Incremental Update](../../../AGENTS.md#incremental-update-windows-x64)
section; only the service-specific rules are repeated here.

### No ConfigMgr2 dependency

`UpdateService` takes `(installDir, currentVersion)` as plain values and never touches
`ConfigMgr2`. This is NOT a style preference: `core_configs` links `core_services`, so a
dependency the other way would be a CMake cycle. Every config-driven decision -
`checkForUpdatesOnStart`, the 24 h throttle (`lastUpdateCheckTime`), and
`skippedUpdateVersion` - therefore lives in `UpdateController`, which is compiled into the
`vnote` target and may use `ConfigMgr2` freely.

Corollary: do NOT "fix" a future need for config inside the service by registering
`ConfigMgr2` with it. Add the policy to the controller and pass the decision down.

The release source is the newest instance of that rule. `UpdateService::Source`
(`GitHub` | `Gitee`) is a plain enum on the service with `setSource()` / `source()` and the
`sourceFromString()` / `sourceToString()` converters; `UpdateController::applyConfiguredSource()`
PUSHES `CoreConfig::getUpdateSource()` in from the constructor and again at the top of every
`startCheck()`, so a Settings change takes effect without a restart. `setSource()` is a
no-op (with a `qWarning`) while `m_busy` is set: switching origins mid-flight would let one
plan mix manifests and archives from two servers. A source change that IS accepted discards
the cached `Plan`, which was built against the previous origin.

### Per-source endpoints

| | GitHub | Gitee |
|---|---|---|
| `apiLatestUrl()` | `https://api.github.com/repos/vnotex/vnote/releases/latest` | `https://gitee.com/api/v5/repos/vnotex/vnote/releases/latest` |
| `assetUrl()` base | `https://github.com/vnotex/vnote/releases/download` | `https://gitee.com/vnotex/vnote/releases/download` |
| `releasesPageUrl()` | `https://github.com/vnotex/vnote/releases` | `https://gitee.com/vnotex/vnote/releases` |
| `releasePageUrl(tag, htmlUrl)` | the API's `html_url` | synthesized `<releasesPageUrl>/tag/v<tag>` — Gitee's release JSON has no `html_url`. Verified against the live `https://gitee.com/vnotex/vnote/releases/tag/v4.3.0`; the `tag/` segment is NOT present in the asset download path, so do not "simplify" the two to share a base |
| `Accept` | `application/vnd.github+json, …` | `application/json, …` (Gitee rejects the vendor type) |

`m_apiLatestOverride` / `m_assetBaseOverride` (the test seams) still WIN over all of these,
which is what keeps `tests/core/test_updateservice.cpp` pointed at its local server
regardless of the configured source.

### Check-only degradation on an absent manifest

`fetchVerifiedManifestEx()` is tri-state: `Ok` / `Absent` / `Error`, with the historical
`fetchVerifiedManifest()` kept as the bool wrapper for the chain walk (where absence and
verification failure are equally fatal — both fall back to the full package).

`Absent` is returned **only** when the MANIFEST fetch itself answers HTTP 404, which
`fetchToMemory()` reports through its `p_notFound` out-param. `checkForUpdates()` turns that
into `eligible = false` + a release-page `ineligibleReason`, and still emits
`checkFinished` with `updateAvailable = true`; it does NOT call `reportFailure()`, because a
mirror that carries the release object but not the update assets is a degradation, not a
check failure.

Once manifest bytes have been received, everything downstream stays FAIL CLOSED. A missing
or unfetchable `.minisig` — 404 included — is `Error`, never `Absent`. That is the property
`testMissingSignatureIsRefused()` pins; do not "unify" the two 404 paths.

### Threading

Both public operations (`checkForUpdates`, `startDownload`) return immediately and do their
work on a `QtConcurrent` worker, because they block on nested event loops (network) and hash
hundreds of megabytes (verification).

- **`QNetworkAccessManager` is created on the WORKER'S STACK**, never as a member. QNAM is
  not thread-safe and belongs to the thread that created it, so every network helper takes
  it by reference. There is deliberately no QNAM member; adding one would reintroduce the
  cross-thread bug.
- All signals (`checkFinished`, `progress`, `readyToApply`, `failed`) are emitted through
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` so receivers see them on the GUI
  thread.
- The destructor calls `cancel()` and then WAITS on the stored `QFuture`. The worker holds a
  raw `this`; letting it outlive the object is a use-after-free.

`startDownload(expectedTargetVersion)` returns `DownloadStart`
(`Started` / `Busy` / `NoPlan` / `Stale`) because a caller that tracks which UI surface owns
the transfer cannot otherwise tell whether the request landed. Only `Started` and `NoPlan`
own a later terminal signal (`NoPlan` emits `failed()`); `Busy` and `Stale` start nothing and
emit nothing. `UpdateController` keys its `TransferSurface` off that return value; claiming a
surface before the call would strand it forever on a `Busy` refusal, or hand an
already-running transfer's result to the wrong surface.

Two ordering rules inside it are load-bearing:

- **The `m_busy` compare-exchange runs BEFORE `m_plan` is read.** `m_plan` is written by the
  check worker, so reading it first would be an unsynchronized read of a value another
  thread may be assigning, and would also let a request that arrives mid-check answer
  `NoPlan` (which emits `failed()` and hands the caller ownership of a terminal signal) when
  the truthful answer is `Busy`. Winning the exchange synchronizes-with the worker's
  `m_busy.store(false)`, which is what makes the subsequent `m_plan` read well-defined.
- **`expectedTargetVersion` pins the request to the plan the caller was OFFERED.** A
  notification or a non-modal `UpdateDialog` can outlive the check it came from; without the
  check, its Update button would download whatever plan the service holds now — possibly a
  different version, or one built against a different source — while still advertising the
  old one. A mismatch is `Stale`. Pass an empty string only where no expectation is
  meaningful.

### Redirects and the host allowlist

Redirects are followed MANUALLY (`QNetworkRequest::ManualRedirectPolicy` set on EVERY
request, because Qt 5 and Qt 6 differ in their default) so each hop can be checked. At most
5 hops, and there is no HTTPS -> HTTP downgrade path.

`allowedHosts(Source)` is **source-scoped and disjoint**, so a client on one forge can never
follow a redirect onto the other's hosts:

- GitHub: exact `api.github.com`, `github.com`, `codeload.github.com`, plus any host under
  `.githubusercontent.com` (release assets redirect there; the exact subdomain has moved
  from `objects.` to `release-assets.`, which is why the suffix rather than a fixed host is
  pinned).
- Gitee: exact `gitee.com`, plus any host under `.gitee.com` (the download endpoint
  redirects through `attach_files` to `foruda.gitee.com`).

`testSetEndpointOverride` / `testSetExtraAllowedHost` exist for the local end-to-end harness
described in the plan's Validation section; the plain-HTTP exemption they enable applies
ONLY to the explicitly nominated host, and it is independent of the source.

### Delta path preconditions

`buildPlan` falls back to the full package - never to a partial update - unless ALL of these
hold. Each fallback is logged with its reason:

1. `<installDir>/manifest.json` exists, parses, is `channel == "stable"`, and describes the
   installed version;
2. the chain of `delta.baseVersion` pointers reaches the installed version within
   `UpdateManifest::c_maxChainHops`, with every hop publishing a delta;
3. the PUBLISHED manifest for the installed version is fetched and
   `UpdateManifest::validateBaseIdentity` matches it against the local one exactly (version,
   variant, platform, commit, and the full `files[]` map);
4. every file in that verified base still hashes correctly on disk (drift check);
5. total delta bytes are within `UpdateManifest::c_maxChainSizeRatio` of the target's
   expanded size.

### Staging equality rules

- Per hop, the archive's file entry set must EQUAL `UpdateManifest::hopArchiveSet(hopBase,
  hopTarget)` exactly, enforced through `ZipExtractor::Options::expectedEntries`.
- Hops are extracted OLDEST FIRST so newer blobs win.
- Afterwards, staged paths outside `UpdateManifest::expectedChanged(base, target)` are
  PRUNED - this is what handles a file changed by a hop and reverted by a later one, and a
  file that only ever existed in an intermediate release. Pruning is enabled ONLY on the
  delta path; on the full-package path an unexpected entry is an error, because the archive
  is supposed to equal the target exactly.
- Then `stagedPaths == expectedChanged` is required, and every staged file is verified by
  size AND SHA-256.
- `manifest.json` is handled out of band: extracted with everything else, compared against
  the published manifest, then REMOVED from `staged/` so the swap never moves it (the
  installer commits it separately at the end of the transaction).

### Pending-update lifecycle

`revalidatePending()` runs at startup and silently discards a plan whose schema is wrong,
whose target version is not strictly newer than the installed one, whose variant does not
match, or whose staged files no longer verify. `consumeStoredResult()` reads and clears
`result.json`, which is how an apply outcome crosses the restart - `NotificationService` is
in-memory only and cannot.

### Test coverage and its seams

`tests/core/test_updateservice.cpp` (43 cases) drives the REAL
`QNetworkAccessManager` against a local `QTcpServer`, and signs its manifest fixtures
in-process with the vendored minicrypto primitives - Ed25519 SIGNING needs no randomness,
so `libs/minicrypto/randombytes_stub.c` (which aborts by design) is never reached. If a run
ever aborts inside `randombytes`, something started calling key GENERATION.

The suite `QSKIP`s itself from `initTestCase()` off Windows. `checkEligibility()` returns
"only available on Windows" before anything else there, so almost every case would be
asserting behavior the feature never promises. The target still BUILDS on Linux and macOS
CI, which is what catches compile breakage; only the assertions are skipped. Add new cases
inside this suite rather than creating a second, unguarded one.

Three seams exist purely for it, all unconditional per ADR-6:

- `ManifestSignature::testClearTrustedKeys()` forces a genuinely EMPTY trusted-key list, so
  the fail-closed branch of `checkEligibility()` is reachable.
  `testSetTrustedKeys({})` cannot do this: an empty vector means "restore the production
  keys", which is what makes it safe to call from a test's `cleanup()`.
- `testSetEndpointOverride()` / `testSetExtraAllowedHost()` redirect the service at the
  local server. The plain-HTTP exemption applies ONLY to the explicitly nominated host.
- `testSetPackagedAppOverride(int)` forces the MSIX/Store detection used by
  `checkEligibility()`: `-1` auto-detect (production), `0` force not packaged, `1` force
  packaged. Without it the Store gate is unreachable from a test process, which is never
  packaged.

Two behaviors that look like bugs and are not, so please do not "fix" them without reading
the tests that pin them down:

- an INELIGIBLE install still fetches the release metadata. `UpdateController` needs
  `latestVersion` / `releaseUrl` to send the user to the download page. What must never
  happen is a manifest or archive fetch, and `testNoTrustedKeysMakesTheInstallIneligible`
  asserts exactly that (no `/download/` request, nothing staged).
- a forged or unverifiable INTERMEDIATE hop (or published base) falls back to the full
  package rather than failing the check. That is strictly safer: the target manifest is
  itself signature-verified, and the full archive is then checked against the signed size,
  SHA-256 and complete file map.

The suite's acceptance property is that a signature bypass at ANY ONE of the three manifest
fetch sites - target, intermediate hop, published base - is caught individually.

Also pinned here, and easy to break together: `testMissingManifestDegradesToCheckOnly()`
(a 404 MANIFEST is `eligible = false` + `updateAvailable = true` and NO `failed()`) and
`testMissingSignatureIsRefused()` (a 404 `.minisig` after the manifest arrived is a hard
failure). Any change to `fetchVerifiedManifestEx` must keep BOTH green; passing only one is
exactly the regression the tri-state was introduced to prevent.

### NotificationService fields consumed by the updater

`UpdateController` drives ONE notification from offer → progress → terminal state through
`NotificationService::update()`, so the value type carries the presentation hints the popup
needs: `NotificationMessage::m_progressPermille` / `m_progressIndeterminate`, and
`NotificationAction::m_dismissOnTrigger` (set `false` on Update / Cancel / Retry so the
message survives its own button). `update()` preserves `m_id`, `m_timestamp` and
`m_dismissed` and therefore never changes `activeCount()`; `isActive(id)` is the guard the
controller uses before touching a tracked message. See `src/widgets/AGENTS.md` §
Notification System for the rendering side.
