# Agent Guidelines for src/core/services

Qt-side service layer wrapping the vxcore C library. Services here are the bridge between the GUI (controllers, widgets) and the vxcore backend. Most are thin handles around `XXXCoreService` types holding a `VxCoreContextHandle`; a few (e.g., `BufferService`, `SyncService`) add hook firing and async orchestration on top.

For the canonical service catalog, DI rules, and Buffer2/HookManager patterns, see the parent `src/core/AGENTS.md`.

## Threading rules for SyncService

`SyncService` is the Qt-side facade for `vxcore::SyncManager`. It must respect the contract documented in `libs/vxcore/src/sync/AGENTS.md` § Threading & Callback Contract. The Qt-specific obligations are:

1. **Never invoke vxcore sync APIs (`vxcore_sync_*`) from the GUI thread synchronously for long-running ops** (`enableSyncForNotebook`, `triggerSync`, `updateCredentials`). Off-load to a worker via the current executor. Short metadata calls (`isSyncRegistered`, `hasCredentials`) may run on the GUI thread.
2. **All Qt signals emitted from the worker thread MUST use `Qt::QueuedConnection`** when crossing back to GUI-owned receivers (controllers, widgets). Hook invocations fired inside the worker likewise must not assume GUI-thread affinity.
3. **Do not hold any `SyncService` member mutex while emitting a signal, firing a hook, or invoking a credential / progress callback.** Snapshot the data under the lock, release the lock, then notify (mirrors vxcore rule 2).

### Current executor (Wave 14 partially wired; full migration deferred)

`SyncService` dispatches worker operations via a private `QThread` (`m_thread`) hosting a `SyncWorker` `QObject` (`m_worker`). Every async op is dispatched via `QMetaObject::invokeMethod(m_worker, "<method>", Qt::QueuedConnection, ...)`; worker signals back to the GUI thread via `Qt::QueuedConnection`. This is the actual current state — `QtConcurrent::run` is NOT used (the original Wave 14 plan prose incorrectly described that as the baseline).

Known limitations: no per-notebook serialization beyond the single worker thread (which serializes ALL notebooks together), no cancellation token integration with vxcore's `WorkQueueManager`. Wave 14.1 introduced `SyncWorkQueueManager` (per-notebook serialized executor) and Wave 14.2 wired it into `ServiceLocator` + bounded shutdown on `aboutToQuit`, so it is available for lookup via `m_services.get<SyncWorkQueueManager>()`. The full migration of the dispatch primitive — replacing `QMetaObject::invokeMethod(m_worker, ..., QueuedConnection)` with `m_workQueueManager->enqueue(notebookId, ...)` — is deferred to a follow-up plan because it requires re-architecting the worker signal contract (the single `QObject` worker on a dedicated `QThread` cannot have its slots invoked legally from arbitrary `QThreadPool` threads).

Until that follow-up lands, new code added to `SyncService` MUST keep using the existing `QMetaObject::invokeMethod(m_worker, ..., QueuedConnection)` pattern so the eventual migration stays mechanical.
