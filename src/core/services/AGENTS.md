# Agent Guidelines for src/core/services

Qt-side service layer wrapping the vxcore C library. Services here are the bridge between the GUI (controllers, widgets) and the vxcore backend. Most are thin handles around `XXXCoreService` types holding a `VxCoreContextHandle`; a few (e.g., `BufferService`, `SyncService`) add hook firing and async orchestration on top.

For the canonical service catalog, DI rules, and Buffer2/HookManager patterns, see the parent `src/core/AGENTS.md`.

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
vxcore file events → `SyncManager::MaybeEnqueueSync` → emit `sync.should_run` → `EventBridge::syncShouldRun` → `SyncService::onSyncShouldRun` → `m_workQueueManager->enqueue(id, λ→SyncOps::triggerSync, "trigger")` → … (same tail as user path).

**Coalescing:** both paths use `coalesceKey="trigger"` so the second of two concurrent trigger requests is dropped (returns `Coalesced` result). This prevents redundant network round-trips when a user clicks Sync while an auto-sync is already queued (or vice versa).

**Queue cap:** default 4 pending items per notebook. Excess enqueues return `QueueFull`, which surfaces as `syncFailed(SYNC_IN_PROGRESS, "sync queue full")` on the manual path and as a silent `qCDebug` log on the auto-sync path.

**Cancellation:** `SyncService::cancelSync(id)` calls `m_workQueueManager->cancelPending(id)` first (removes queued items, emits `syncCancelled(id, wasQueued=true)` for each dropped item) THEN cancels the in-flight token via `vxcore_sync_cancel` (emits `syncCancelled(id, wasQueued=false)` when the in-flight sync unwinds and returns).

`isSyncInProgress(id)` now delegates to `SyncWorkQueueManager::inFlightState(id).running` — there is no longer a separate `m_inFlight` set on `SyncService`. The queue manager is the single source of truth for in-flight state.

**Cancellation event payload:** `SyncCancelledEvent` (typed hook event for `vnote.sync.cancelled`) carries `notebookId` (QString) and `wasQueued` (bool). `wasQueued=true` indicates the cancellation removed a pending queue entry; `wasQueued=false` indicates the in-flight sync was aborted via `vxcore_sync_cancel`.

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
