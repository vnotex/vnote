# Agent Guidelines for src/core/services

Qt-side service layer wrapping the vxcore C library. Services here are the bridge between the GUI (controllers, widgets) and the vxcore backend. Most are thin handles around `XXXCoreService` types holding a `VxCoreContextHandle`; a few (e.g., `BufferService`, `SyncService`) add hook firing and async orchestration on top.

For the canonical service catalog, DI rules, and Buffer2/HookManager patterns, see the parent `src/core/AGENTS.md`.

## Threading rules for SyncService

`SyncService` is the Qt-side facade for `vxcore::SyncManager`. It must respect the contract documented in `libs/vxcore/src/sync/AGENTS.md` § Threading & Callback Contract. The Qt-specific obligations are:

1. **Never invoke vxcore sync APIs (`vxcore_sync_*`) from the GUI thread synchronously for long-running ops** (`enableSyncForNotebook`, `triggerSync`, `updateCredentials`). Off-load to a worker via the current executor. Short metadata calls (`isSyncRegistered`, `hasCredentials`) may run on the GUI thread.
2. **All Qt signals emitted from the worker thread MUST use `Qt::QueuedConnection`** when crossing back to GUI-owned receivers (controllers, widgets). Hook invocations fired inside the worker likewise must not assume GUI-thread affinity.
3. **Do not hold any `SyncService` member mutex while emitting a signal, firing a hook, or invoking a credential / progress callback.** Snapshot the data under the lock, release the lock, then notify (mirrors vxcore rule 2).

### Current executor (Wave 14 will replace)

`SyncService` currently dispatches worker operations via `QtConcurrent::run`, which pulls from the global `QThreadPool`. This is a pragmatic stopgap and has known limitations: no per-notebook serialization, no cancellation token, no integration with vxcore's `WorkQueueManager`. Wave 14 will migrate these call sites to `vxcore::WorkQueueManager` named queues (one queue per notebook id) so that:

- Per-notebook ops serialize naturally (no two `triggerSync` calls collide for the same notebook).
- Cancellation tokens propagate through libgit2 progress callbacks (matches vxcore rule 4).
- Work survives across executor restarts; the GUI no longer owns the thread pool for sync.

Until Wave 14 lands, new code added to `SyncService` MUST keep its `QtConcurrent::run` usage tightly scoped (one operation per call, no nested submissions) so the migration stays mechanical.
