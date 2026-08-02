# Controllers

Controllers are QObject-based business logic mediators that sit between models, views, and services. They translate user actions into service calls and service results into UI feedback, orchestrating operations without containing any UI logic themselves.

## MVC Rules for Controllers

See [MVC Rules](../../AGENTS.md#mvc-rules-must-follow) — Key rule for controllers: **Controllers MUST NOT inherit from QWidget** (testable without GUI).

See [MVC Example](../../AGENTS.md#mvc-example-notebook-node-operations) for the Controller → View → Model flow.

## Multi-Target Actions with Dialogs (Batch Pattern)

When adding a multi-target action that requires user input via a dialog, the MVC layers have distinct responsibilities:

| Layer | Owns | Forbidden |
|---|---|---|
| **Controller** (`NotebookNodeController`) | Decides "an action was requested on this list of nodes"; emits ONE list signal per request. Runs no UI; uses no `QDialog`. | Showing dialogs. Looping a per-id signal. Holding dialog state. |
| **View** (`NotebookExplorer2` + its explorers) | Receives the list signal, shows ONE dialog seeded with sensible defaults, then iterates the list and invokes the existing per-id apply path. Owns all `QDialog` instances. | Looping the controller's emit. Owning business state. Direct service calls bypassing controller. |
| **Model** (notebook node store via vxcore services) | Per-id apply primitives (`handleMarkResult`, etc.) remain single-target and idempotent. | Knowing about selections, dialogs, or batches. |

**Rule for new actions**: When adding a multi-target action that requires user input via a dialog, the request signal MUST take `QList<NodeIdentifier>` (even when size is 1); the View slot MUST show ONE dialog; the apply path MUST stay per-id and be looped in the slot. **Use ONE method/signal name per action — widen the signature rather than introducing parallel singular/plural variants.** NEVER define a per-id signal that the view will fire N dialogs from.

**Audit summary**: As of 2026-05-16, the only multi-target action that previously violated this rule was Mark; its `markRequested` signal was widened to `QList<NodeIdentifier>` (same name, one method) per the contract.

## Controller Inventory

| Controller | Purpose |
|------------|---------|
| `NotebookNodeController` | Node CRUD operations (new/delete/rename/move) |
| `NewNoteController` | New note creation flow |
| `NewFolderController` | New folder creation flow |
| `NewNotebookController` | New notebook creation flow |
| `OpenNotebookController` | Open existing notebook flow |
| `ManageNotebooksController` | Notebook management operations |
| `ImportFolderController` | Folder import flow |
| `RecycleBinController` | Recycle bin operations |
| `ViewAreaController` | View area orchestration (open/close/split/move buffers) |
| `SearchController` | Search operations |
| `SnippetController` | Snippet management |
| `TagController` | Tag operations |
| `OutlineController` | Document outline |
| `AttachmentController` | Attachment management |
| `MarkdownEditorController` | Markdown editing logic |
| `MarkdownViewWindowController` | Markdown view window logic |
| `TextViewWindowController` | Plain text editing |
| `PdfViewWindowController` | PDF viewing |
| `MindMapViewWindowController` | Mind map viewing |
| `NotebookSyncInfoController` | Sync enable/disable, PAT refresh, URL change, bootstrap recovery |
| `NewNotebookController` (sync portion) | New-notebook bootstrap via `bootstrapSync` (deletes notebook on enable failure) |
| `DashboardController` | Home dashboard (vx://home) layout model, occupancy math, seed/default, and WidgetConfig persistence; the `DashboardBoard` widget is its pure view |
| `NotificationRouter` | Turns subsystem failure signals into `NotificationMessage`s; owns attention/dedup policy (see below) |

## NotificationRouter

Translates already-existing subsystem failure signals into notifications. It exists because
most of those signals had **zero receivers** — `BufferService::bufferAutoSaveAborted` has
been emitted-and-ignored for a long time. The failure was never that core services could
not speak, but that nobody listened.

Rules:

- **It owns attention/dedup POLICY ONLY, never recovery logic.** `NotebookExplorer2` keeps
  its sync failure filtering, state refresh and `m_credentialUpdateRetryArm` arming; it
  merely stopped popping modals and emits `syncUserMessageRequested` instead.
- **Its constructor takes only `ServiceLocator &`** and holds no widget pointers, so the
  header stays widget-free and `test_notificationrouter` is genuinely `GUILESS` (it drives
  the widget-owned sources through the public slots). `MainWindow2` owns the connections
  from its private members into those slots, plus the one out of `openSyncInfoRequested`.
- **It never constructs a dialog.** The sync-auth notification's action emits
  `openSyncInfoRequested(notebookId)`, which `MainWindow2` forwards to
  `NotebookExplorer2::openSyncInfo` — a notification may name a notebook that is not the
  one currently on screen, which is why that method takes an explicit id.
- `BufferService` privately inherits its QObject base and exposes only `asQObject()`, so its
  three signals are connected with the string-based `SIGNAL`/`SLOT` form. That is why the
  buffer handlers are **named slots** rather than lambdas.
- It passes the existing `VxCoreError` through rather than inventing a "kind" enum;
  `SyncService::syncFailed` already supplies exactly that code.

**Incident retirement is not optional.** Because the toast is raised only by
`messageAdded`, a repeat failure within a live incident is silent by design. Every boundary
where an incident genuinely ends must call `dismissByDedupKey`, or that failure becomes
permanently quiet. The current boundaries are sync success / enable / credentials-update /
disable (from `SyncService`), manual Sync Now (via
`NotebookExplorer2::syncIncidentRetryRequested`, emitted immediately before
`triggerSyncNow`), buffer save success, and upload success.

**Notebook switch is deliberately NOT a retirement boundary.** The anti-spam `QSet`s it
replaced were transient modal-suppression bookkeeping, so clearing them cost nothing; a
notification is a user-visible record of a failure that is still unresolved. Merely looking
at a different notebook does not resolve it, and retiring by prefix would also delete other
notebooks' unresolved failures.


## NotebookSyncInfoController: bootstrapApply vs applyChanges

`NotebookSyncInfoController` exposes two recovery paths. Picking the wrong one is the root cause of B7 (chicken-and-egg) and B8 (resurrection trap) historically. See the root [Sync State Model](../../AGENTS.md#sync-state-model) for the S0-S7 predicates.

| Method | Source | Use when | Failure behavior |
|---|---|---|---|
| `bootstrapApply(url, pat)` | `notebooksyncinfocontroller.cpp` (atomic enable for existing notebook) | Notebook is partial (S1/S2/S3/S4). Always called by `NotebookSyncInfoDialog2` when `m_bootstrapMode == true` OR when `SyncService::isSyncRegistered(id) == false`. | Keeps notebook intact (no delete). Emits `error(message)` then `applyComplete(false)`. Diverges from `NewNotebookController::bootstrapSync` which removes the half-created notebook on failure. |
| `applyChanges(url, pat)` | `notebooksyncinfocontroller.cpp:107-146` | Notebook is fully registered (S5). PAT refresh or URL change. | PAT-only changes route through `SyncService::updateCredentials`. |

Implementation patterns:

- **One-shot signal disconnect**: `bootstrapApply` connects to `SyncService::enableFinished` via `std::make_shared<QMetaObject::Connection>`; the lambda filters by `m_notebookId`, self-disconnects, then emits `applyComplete`. Mirrors `NewNotebookController::bootstrapSync` (`newnotebookcontroller.cpp:217-244`) minus the delete-on-failure branch.
- **Persist after success only**: `persistRemoteUrl(p_url)` runs inside the success branch of the lambda. vxcore is the source of truth; the on-disk URL advertises success only when vxcore actually accepted it.

## URL Change on S5: confirmUrlChangeRequested

Changing the remote URL on a registered notebook is destructive (drops the existing git remote linkage). `NotebookSyncInfoController::applyChanges` detects URL change on a registered notebook and gates it behind a confirmation flow:

1. **Detect**: `urlChanged && isSyncRegistered(id) && !newUrl.isEmpty()` → cache new URL + PAT in member state, emit `confirmUrlChangeRequested(oldUrl, newUrl)`, return without further work.
2. **Dialog catches signal**: shows a `QMessageBox` with the URL change warning. On confirm calls `controller->confirmUrlChange(true)`; on cancel calls `controller->confirmUrlChange(false)` (which clears pending state, no-op).
3. **PAT preservation**: if the PAT field was empty, controller fetches the existing PAT from the keychain via async `SyncCredentialsStore::retrieveCredentials` BEFORE running disable (disable wipes the keychain entry per `SyncService::disableSyncForNotebook`).
4. **Atomic re-register**: `performAtomicUrlReChange` chains `disableSyncForNotebook` → on `VXCORE_OK` wipes `<root>/vx_notebook/vx_sync/` via `QDir::removeRecursively` (required because vxcore `DisableSync` only clears in-memory maps; the gitdir remains and a re-enable against a different URL would otherwise see the stale `remote.origin.url`) → calls `enableSyncForNotebook(newUrl, pat)` → on `VXCORE_OK` restores the three flat sync keys, calls `triggerSyncNow`, emits `applyComplete(true)`.
5. **Failure recovery**: re-enable failure leaves notebook in clean S0 (sync fields stay cleared per W2.T5 disable JSON clear). The W4.T2 "Enable Sync" UI affordance is the retry surface.

## NewNotebookController bootstrapSync Rollback

`NewNotebookController::bootstrapSync` (`src/controllers/newnotebookcontroller.cpp`) wires the new-notebook flow to `SyncService::enableSyncForNotebook` and on failure tears the half-created notebook down. The cleanup order is invariant:

1. `credStore->deleteCredentials(p_notebookId)` (line 258) — free the keychain slot for the just-stored PAT.
2. `notebookService->closeNotebook(p_notebookId)` (line 261) — drop the notebook from vxcore.
3. `QDir::removeRecursively` on the rootPath (lines 266+) with Windows-retry loop.

**Why this ordering matters**: `deleteCredentials` MUST run BEFORE `closeNotebook`. Although `SyncService` also wipes the PAT from inside the `NotebookAfterClose` hook handler (see `src/core/services/AGENTS.md` § Credential Cleanup Invariants), keeping the explicit pre-close delete in `bootstrapSync` is defense in depth: it ensures the keychain entry is gone even if the hook subscription is ever broken, reordered, or skipped (e.g., by a future refactor that runs `closeNotebook` against a different service handle). The hook then runs idempotently and is a no-op.

This is the ONLY controller-side `deleteCredentials` call site. All other credential cleanup belongs to `SyncService`.

## Related Modules

- [`../core/AGENTS.md`](../core/AGENTS.md) — ServiceLocator and services used by controllers
- [`../models/AGENTS.md`](../models/AGENTS.md) — Models manipulated by controllers
- [`../views/AGENTS.md`](../views/AGENTS.md) — Views that signal controllers
- [`../widgets/AGENTS.md`](../widgets/AGENTS.md) — Widgets containing MVC wiring
- [`../../AGENTS.md`](../../AGENTS.md) — Full MVC rules, architecture overview, hook system
