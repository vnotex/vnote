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

## Related Modules

- [`../core/AGENTS.md`](../core/AGENTS.md) — ServiceLocator and services used by controllers
- [`../models/AGENTS.md`](../models/AGENTS.md) — Models manipulated by controllers
- [`../views/AGENTS.md`](../views/AGENTS.md) — Views that signal controllers
- [`../widgets/AGENTS.md`](../widgets/AGENTS.md) — Widgets containing MVC wiring
- [`../../AGENTS.md`](../../AGENTS.md) — Full MVC rules, architecture overview, hook system
