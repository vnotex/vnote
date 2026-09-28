# Controllers

Controllers are QObject-based business logic mediators that sit between models, views, and services. They translate user actions into service calls and service results into UI feedback, orchestrating operations without containing any UI logic themselves.

## MVC Rules for Controllers

See [MVC Rules](../../AGENTS.md#mvc-rules-must-follow) — Key rule for controllers: **Controllers MUST NOT inherit from QWidget** (testable without GUI).

See [MVC Example](../AGENTS.md#mvc-example-notebook-node-operations) for the Controller → View → Model flow.

## Multi-Target Actions with Dialogs (Batch Pattern)

When adding a multi-target action that requires user input via a dialog, the MVC layers have distinct responsibilities:

| Layer | Owns | Forbidden |
|---|---|---|
| **Controller** (`NotebookNodeController`) | Decides "an action was requested on this list of nodes"; emits ONE list signal per request. Runs no UI; uses no `QDialog`. | Showing dialogs. Looping a per-id signal. Holding dialog state. |
| **View** (`NotebookExplorer2` + its explorers) | Receives the list signal, shows ONE dialog seeded with sensible defaults, then iterates the list and invokes the existing per-id apply path. Owns all `QDialog` instances. | Looping the controller's emit. Owning business state. Direct service calls bypassing controller. |
| **Model** (notebook node store via vxcore services) | Per-id apply primitives (`handleMarkResult`, etc.) remain single-target and idempotent. | Knowing about selections, dialogs, or batches. |

**Rule for new actions**: When adding a multi-target action that requires user input via a dialog, the request signal MUST take `QList<NodeIdentifier>` (even when size is 1); the View slot MUST show ONE dialog; the apply path MUST stay per-id and be looped in the slot. **Use ONE method/signal name per action — widen the signature rather than introducing parallel singular/plural variants.** NEVER define a per-id signal that the view will fire N dialogs from.

Cross-notebook Paste follows the same ownership boundary with a typed batch: the controller emits
`crossNotebookPasteRequested` without mutating storage, `NotebookExplorer2` owns one application-
modal progress dialog, and the view calls back through `INodeExplorer::executeCrossNotebookPaste`.
The controller then invokes `NodeTransferService`, reconciles structured clipboard entries, reloads
models, and selects the returned destination. Same-notebook paste always reconstructs and uses the
original path identifiers; optional stable UUID/kind/resume data must not affect that branch.

**Audit summary**: As of 2026-05-16, the only multi-target action that previously violated this rule was Mark; its `markRequested` signal was widened to `QList<NodeIdentifier>` (same name, one method) per the contract. As of 2026-08-03, the two remaining *undocumented* blocking modals were removed:

- `AttachmentController::addAttachments()` / `deleteAttachments()` no longer open a `QFileDialog` / `QMessageBox`. Both were triggered from `QAction` handlers in `src/widgets/attachmentpopup2.cpp`, so the view was already on the stack and no new signal was needed: the popup now shows the dialog (parented to `AttachmentPopup2::dialogParent()`, i.e. the tool button's window — `this` is a `QMenu` and would be the wrong parent) and calls `addAttachments(const QStringList &)` with the result. The popup re-applies the controller's guards so no dialog is raised whose result would be discarded; the controller keeps its own guards as defense in depth. Gate: `tests/controllers/test_attachmentcontroller.cpp` is `GUILESS` and drives both paths — before this change either call blocked on a modal.
- `NotebookNodeController::handleRenameResult`'s unsaved-changes `MessageBoxHelper::questionSaveDiscardCancel` was deleted outright (it dropped the file's last `widgets/` include). It was unreachable, and it was not protecting anything: `NotebookCoreService::renameFile/renameFolder` fire `NodeAfterRename`, and `ViewAreaController::onNodeAfterRename` (`viewareacontroller.cpp:1921-1955`) retargets open windows onto the already-updated buffer paths. Renaming a note with unsaved changes therefore proceeds without asking — today's behavior, with no data loss.

**Known dead path**: `NotebookNodeController::handleRenameResult`, `INodeExplorer::handleRenameResult` (`src/views/inodeexplorer.h:83`) and both explorer overrides have **no production caller**. Production rename is inline: `NotebookExplorer2::onRenameRequested` (`notebookexplorer2.cpp:1585-1589`) starts an edit and Qt commits it through `NotebookNodeModel::setData`, which calls `NotebookCoreService::renameFile/renameFolder` directly (`notebooknodemodel.cpp:426-469`). Do not assume renames flow through the controller.

Still violating the "no `QDialog`" rule, but **deliberately and with a recorded rationale** (do not "fix" these in passing): `UpdateController` (it opens `UpdateDialog` for a manual "Check for Updates"; `tests/controllers/CMakeLists.txt` notes it is not GUILESS and stubs the dialog rather than linking it), `SyncConflictController` (rationale in `syncconflictcontroller.h:17-47`), `NewNotebookController`'s `QProgressDialog`, `ViewAreaController`'s `SettingsWidget`/`DashboardContent` construction, and the `QMenu`-building controllers (`NotebookNodeController`, `MarkdownViewWindowController`).

## Update Check

`UpdateController` owns **ALL** update policy: the configured release source, the 24 h throttle,
the skipped version, the manual-vs-startup surface, and failure loudness. Ordinary checks are
read-only with respect to the installation and fetch no release assets. `UpdateService` and
`UpdateInfo` stay unchanged and check-only; the service must never gain a `ConfigMgr2`
dependency. Full check contract:
[`../core/services/AGENTS.md` § Update Check](../core/services/AGENTS.md#update-check).

Only explicit activation of Windows' `Update Now` notification action may start the deployed
external PowerShell updater. Keep `Check Release` first, the existing manual-check dialog
unchanged, and `m_dismissOnTrigger=false` on the new action. Preserve attention, persistence,
throttle/skip and supersession behavior; stale/dismissed/evicted offer callbacks cannot
launch. Allow one active attempt, and retire failures/cancellations so explicit retry works.
Launch failures are interrupting Warning notifications in category `update`, key
`update.install`, retaining the release-page fallback.

Snapshot `CoreConfig::getUpdateSource()` on activation using the existing source converters;
absent config normalizes to Gitee. Pass the immutable offered version and compile-time x64
Qt variant, never a newly discovered release. Production launches require the running root
`vnote.exe` and installed `updater/update-vnote.ps1`, `minisign.exe` and `LICENSE.minisign`;
never search the repository or PATH for missing helpers. Copy helpers into private temporary
storage and transfer cleanup ownership only after successful detached launch. Use resolved
system Windows PowerShell, individual arguments, a visible console, process-scoped execution
policy and install-root-prefixed PATH for bundled UCRT; never elevate or construct shell code.

A per-attempt, user-access-only `QLocalServer` owns the PID/token-authenticated bounded
HELLO/OK → READY → ACCEPTED/CANCELLED handshake. Do not extend `SingleInstanceGuard` or add
a general quit command. READY is accepted once, only from the authenticated helper after
verified download/extraction and preflight; it emits `scriptUpdateShutdownRequested()`.
`MainWindow2` owns the existing cancellable forced-close path: preserve/restore `m_requestQuit`
on refusal and return the `close()` result to `completeScriptUpdateShutdown()`. Flush the
explicit response in that call stack, never block the GUI, and retain an accepted session
through teardown. EOF, timeout or cancellation never authorizes installation. The helper must
wait on the retained original process handle through save/sync drains without force-killing.

The external process alone owns signed full-package authentication, preservation of portable
`config` and unowned files, clone/overlay/swap with complete recovery backup, and same-token
relaunch. Protected installations fail with manual-update guidance, not UAC/ACL changes.
[../../docs/update-signing.md](../../docs/update-signing.md) owns that contract.

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
| `NotebookSyncInfoController` | Git/WebDAV/Jianguoyun enable/disable, credential rotation, binding retirement, bootstrap recovery |
| `NewNotebookController` (sync portion) | New-notebook bootstrap via `bootstrapSync` (deletes notebook on enable failure) |
| `DashboardController` | Home dashboard (vx://home) layout model, occupancy math, seed/default, and WidgetConfig persistence; the `DashboardBoard` widget is its pure view |
| `NotificationRouter` | Turns subsystem failure signals into `NotificationMessage`s; owns attention/dedup policy (see below) |
| `CommentController` | The ONLY mutator of a file's `comments.json` set. Owns the active `NodeIdentifier`, debounces and coalesces add/edit/color/delete intents, receives asynchronous `CommentService` completion, and surfaces failures **without** marking the buffer modified (see below) |

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

`NotebookExplorer2::fileImportFinished` reports one batch's imported/failed counts via
`MainWindow2`. The explorer reloads and selects the last successful import by its returned
UUID (including conflict-renamed files); the router posts one keyless, interrupting toast:
success for a complete batch, warning for partial success, error for total failure. Cancelled
imports emit nothing. Do not replace this with a modal message box that takes focus away
from the imported item.

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

Both entry points take `SyncSettings`; all creation, enable, reconfiguration and remote-open
flows use the shared validation/canonicalization in `core/services/syncsettings.*`.
Only `git`, `webdav` and `jianguoyun` are supported. Raw notebooks cannot enable sync; unknown configured
backends report an error instead of opening a Git form.

| Method | Use when | Failure behavior |
|---|---|---|
| `bootstrapApply(settings)` | Existing notebook is disabled or partial (S0–S4) | Uses `SyncService::bootstrapAndPersist`; keeps working files on failure |
| `applyChanges(settings)` | Existing sync settings or credentials change | Waits for the operation result before `applyComplete`; no optimistic persistence |

A blank secret retrieves existing credentials only into the transient operation. Stored and
selected backends must match. A supplied WebDAV/Jianguoyun username requires a new secret; passwords are
never trimmed or prefilled. Same-URL WebDAV/Jianguoyun credential rotation authenticates and verifies the
remote notebook UUID before changing its username binding. Git HTTPS username-only changes
retain repository history and update origin through the existing initialization path.

## Endpoint changes and recovery

A different URL or method requires confirmation, including a persisted binding found after
Disable cleared the portable routing fields. Queued/running sync or vault work blocks changes.
Under the existing `SyncWorkQueueManager` maintenance lease, restore any interrupted archive,
then call `NotebookCoreService::checkSyncReconfiguration()`. Its offline core inspection catches
unregistered Git index conflicts as well as both DAV providers' conflicts and pending transactions. Never
infer that absence of a runtime backend means absence of recoverable on-disk state.

Provider retirement writes `vx_notebook/vx_sync/<backend>/retirement.json`, with backend restricted
to `webdav` or `jianguoyun`, before moving current
binding files/snapshots into `retired/<operationId>/`. The existing `retired` directory is never
moved into its own descendant. Archive failure restores moved entries and aborts before disabling
usable runtime state. An interrupted archive must be restored before any new enable. Inspect
both provider directories independently; never mix journal backend tags, and preserve the legacy
WebDAV journal shape. Retired-only directories are not corrupt active bindings.

Git state is removed only AFTER successful disable releases repository handles. Remove only the
explicit Git-owned allowlist; never recursively remove `vx_sync` or either provider child. Working
notes remain intact. After successful disable but failed re-enable, keep clean disabled routing
and visible retry guidance. `disableFinished` follows settled vault cleanup, so a later enable
cannot have its newly stored credential erased by an earlier delete.

## NewNotebookController bootstrapSync Rollback

`NewNotebookInput` contains `syncMethod` (`none`, `git`, `webdav`, `jianguoyun`) and `SyncSettings`. When enabled,
the selection must match the settings backend. Creation remains create-then-enable. On failure:
request credential deletion, close the newly created notebook, then remove ONLY its owned root.
WebDAV Initialize never publishes ordinary notebook data; Jianguoyun Initialize never publishes
genesis. Initial sync is queued only after routing persistence, so a later sync failure does not
enter this owned-root setup rollback or occupy a remote collection without retaining its local
notebook. This controller's pre-close cleanup is the historical
exception to the service-owned credential cleanup sites.

## Remote open ownership

`CloneAndOpenInput` carries `SyncSettings`, destination and auto-sync preference. The final
folder MUST NOT exist, even if empty. Allocate an exclusive sibling `QTemporaryDir` and leave
it EMPTY for the backend clone; ownership markers are added only after download.
After download, an exclusive random owner marker in excluded `vx_notebook/vx_transfer` travels
with the rename. Verify ownership before rollback of a newly created final root; never remove a
foreign destination created during the network operation. Remove the owner marker before success.
Do not use the old rename helper that deletes root `staging-marker.json`: that name may be a real
remote notebook file.

Authenticated open registers the selected backend. Jianguoyun requires credentials and the UI
must disclose managed remote storage, retained history and no cloud-side notebook editing.
Anonymous Git/WebDAV open remains writable partial S2 and sets `partialSyncMissingCredentials`;
it must not automatically prompt for credentials.

## Conflict resolution completion

`SyncConflictController` passes `SyncService::keepBothUnsupportedPaths()` to `SyncConflictDialog2`;
no dialog queries a busy backend or guesses capability from filenames. Metadata/encrypted
revisions are indivisible. Emit `conflictsResolved` only after successful trailing sync and no
remaining conflicts. A per-file failure or failed trailing sync keeps the incident active.

## Related Modules

- [`../core/AGENTS.md`](../core/AGENTS.md) — ServiceLocator and services used by controllers
- [`../models/AGENTS.md`](../models/AGENTS.md) — Models manipulated by controllers
- [`../views/AGENTS.md`](../views/AGENTS.md) — Views that signal controllers
- [`../widgets/AGENTS.md`](../widgets/AGENTS.md) — Widgets containing MVC wiring
- [`../../AGENTS.md`](../../AGENTS.md) — Full MVC rules, architecture overview, hook system

## CommentController

The single mutation point for a file's comment set. Both the comment dock AND the QWebChannel
overlay bridge are **views**: they emit intents (`addCommentRequested`, `textEditRequested`,
`colorChangeRequested`, `deleteRequested`, `selectCommentRequested`, `moveCommentRequested`) and
never write anything.

Rules that are load-bearing:

- **It is a `QObject`, never a `QWidget`** — testable without a GUI, per the MVC rules.
- **`moveComment` is the only GEOMETRY mutator, and it rewrites a COPY of the anchor.**
  `Comment::m_anchor` is stored verbatim (`commenttypes.h:285`), so it replaces only `page`, `x`
  and `y` and never rebuilds the object from typed fields — otherwise `fontSize` and every key a
  newer build wrote would be silently destroyed. It accepts `pdf-freetext` anchors only, returns
  without publishing or scheduling a write when the point is unchanged (nothing on the page may
  wait on a publish that will never arrive), and deliberately does **not** emit `commentAdded`,
  whose only receiver is `PdfViewWindow2::beginInlineTextEdit` and would re-open the inline editor
  on the moved box.
- **`setActiveFile()` flushes any pending write first.** A debounced save belongs to the OLD
  identifier; letting it fire after the switch would serialize one file's comments against
  another's path.
- **`PdfViewWindow2`'s destructor calls `flushPendingSave()`**, or closing a tab within the
  debounce window silently drops the user's last edit.
- **Failures never mark the buffer modified.** Comments are not buffer content and the PDF
  genuinely never changes; the window shows an `InlineBanner` instead. A read-only rejection also
  flips the provider to non-editable so the dock disables its controls.
- Two debounces exist and they are not redundant: the controller debounces the *intent* (so a
  typing burst is one re-serialization and one list rebuild), and `CommentService` coalesces the
  *write* on its own queue (so a burst is one `QSaveFile` commit).

Store contract, path resolution and the sync-dirty notification:
[`../core/services/AGENTS.md` § Comment store](../core/services/AGENTS.md#comment-store-commentsjson).


## Note-body encryption

Encryption is limited to managed Markdown, plain-text and mind-map note bodies.
`ViewAreaController` freezes only the selected note's views, captures its writer,
drains saves/comments and passes the body plus expected source SHA-256 through
`NotebookCoreService` to vxcore. Conversion does not enumerate assets, scan any
note references, or rewrite content. `LegacyImageMigrationController` is only for
legacy image migration; it must not contain encryption planning or retention code.

Dialogs must disclose that separate image/attachment files and comment sidecars
stay unencrypted. Attachments keep normal Open/Open Folder/Copy Path operations.
Encrypted note bodies cannot be exported or printed. Do not add a plaintext-copy
action or write temporary decrypted note files; ordinary exports remain unchanged.

`MarkdownEditorController::insertImageAsBase64` inserts a reference-style image
and a data-URI definition as one undoable edit. Image insertion lists Base64 first
and selects it by default only for encrypted Markdown. Ordinary notes retain
normal defaults. Base64 is encoding, not encryption: the embedded bytes receive
protection only when saved as part of an encrypted note body.
