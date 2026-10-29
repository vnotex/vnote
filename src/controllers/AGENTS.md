# Controllers

Controllers are QObject-based business logic mediators that sit between models, views, and services. They translate user actions into service calls and service results into UI feedback, orchestrating operations without containing any UI logic themselves.

## MVC Rules for Controllers

See [MVC Rules](../../AGENTS.md#mvc-rules-must-follow) — Key rule for controllers: **Controllers MUST NOT inherit from QWidget** (testable without GUI).

See [MVC Example](../AGENTS.md#mvc-example-notebook-node-operations) for the Controller → View → Model flow.

## Explicit file-open destinations

`FileOpenSettings::m_targetWorkspaceId` names an existing hosted workspace GUID,
including `DetachedWindow::getWorkspaceId()` / `ViewSplit2::getWorkspaceId()`.
The field travels through `FileOpenEvent` and QVariant maps as `targetWorkspaceId`.
It is a per-open option, not a persisted setting or CLI argument.

```cpp
FileOpenSettings settings;
settings.m_targetWorkspaceId = targetWorkspaceId;
bufferService->openBuffer(nodeId, settings);
```

A nonempty target takes precedence over `m_detachedView`. Empty preserves current
main-workspace / per-batch detached routing. Missing, closed or hidden targets are
rejected without opening elsewhere or creating a replacement workspace; cleanup
closes only unowned candidate buffers. Configured external applications and
unsupported-type system dispatch retain their existing precedence: this option
routes built-in note views, not external programs. `BufferService::openBuffer()`
returns a buffer handle, not an acknowledgement that a destination view was created.

Detached destinations retain separate tabs and deduplicate within that workspace,
without changing the main split/current buffer. Main destinations follow the normal
new-tab preference. `m_focus=false` preserves selection for both new and duplicate
opens; focused opens select the destination tab and activate its detached host.
`onViewWindowOpened` always receives the actual workspace ID from the view; never
infer ownership from current focus or a request's `detachedView` flag.

Coverage: `tests/widgets/test_viewarea_tab_reuse.cpp` exercises real services and
windows, destination precedence, batch boundaries, duplicates, background opens,
main-tab reuse, unavailable targets, ownership cleanup and reentrant opens.

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

- `AttachmentController::addAttachments()` / `deleteAttachments()` no longer open a `QFileDialog` / `QMessageBox`. Both were triggered from `QAction` handlers in `src/widgets/attachmentpopup2.cpp`, so the view was already on the stack and no new signal was needed: the popup now shows the dialog (parented to `AttachmentPopup2::dialogParent()`, i.e. the tool button's window — `this` is a `QMenu` and would be the wrong parent) and calls `addAttachments(const QStringList &)` with the result. The popup re-applies the controller's guards so no dialog is raised whose result would be discarded; the controller keeps its own guards as defense in depth. Gate: `tests/controllers/test_attachmentcontroller.cpp` drives both paths (its `QApplication` also supports real clipboard coverage) — before this change either call blocked on a modal.
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
absent config normalizes to Gitee. Pass the immutable offered version, never a newly
discovered release. The updater supports only the `win64` package; do not pass a variant
selector. Production launches require the running root `vnote.exe` and installed
`updater/update-vnote.ps1`, `minisign.exe` and `LICENSE.minisign`;
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

## Native Typst runtime and image cleanup

`TypstViewWindowController::clearObsoleteImages()` borrows the application-owned
`vte::TypstRuntime` from ServiceLocator. The accepted-close/rebinding path scans initial,
current and saved source before detaching the buffer. Parsing runs on the separate scanner
lane; the caller waits at most ten seconds without pumping GUI events. Missing/stopped/busy
runtime, timeout, failed or incomplete scans never authorize deletion. Retain ownership,
save-queue/read-only guards and the saved-byte equality recheck immediately before deletion.
The runtime outlives all windows and controllers; workers capture source data, not Buffer2
or controller pointers. Native compilation timeout does not force-stop its thread.

## Native Markdown rendering

`MarkdownRenderController` owns native reader and heading-anchor jobs. It uses the shared
QtConcurrent pool with one running and one latest-pending preview per view; explicit heading
requests complete exactly once. Cancellation explicitly exchanges the active callback
with an empty function: moving a `std::function` alone does not guarantee an empty
source on every standard library. Workers capture immutable source/options/documents, never
buffers, widgets or services. GUI-thread admission and delivery both enforce authorization.

`MarkdownHtmlRenderer` uses the same cmark viewer profile for direct Read, editor snapshots,
heading links and WebEngine export. Reuse requires identical source bytes and parse options;
soft-break output options do not invalidate the tree. Heading IDs are allocated in rendered
order, including relocated footnotes, while the cmark decoration table remains source-ordered.

Invalidate on source/profile/page changes and suspend before reader eviction. Protected revoke
closes admission and suppresses delivery immediately; running parses release their inputs on
completion and drain asynchronously before final clearance. Never block the GUI waiting for
cmark or reset a finished QFutureWatcher to a default future to release its result.

## Markdown JS Jobs

`MarkdownWebJobController` is a per-editor QObject receiving `ServiceLocator` and the buffer.
It owns typed preview, code/math highlighting, stylesheet and HTML-to-Markdown
requests. Heading anchors use the native controller without requiring WebEngine. Views only emit intents, provide geometry/session observations, and display results.
The existing document models remain authoritative; the controller never constructs a QWidget.

For each request, prefer the authorized current ready reader session. Otherwise ordinary notes
use `WebViewService`; protected notes request a per-view isolated host through a signal. They
must never fall through to the ordinary helper. In-flight work remains on its original endpoint.
Generation cancellation rejects stale results; explicit actions receive failures, not empty success.

`setSuspended(true)` closes admission before invalidating once, cancels outstanding work and drops
preferred/per-view protected endpoints. Requests and late readiness/session deliveries cannot
allocate or bind a fallback while suspended; late protected sessions request release. Resuming only
clears the suspension flag: it allocates nothing and never clears terminal lock/revoke state.
Keep suspension out of `contextIsCurrent()` and close-hook authorization checks, so closing a
same-buffer sibling cannot permanently revoke a dormant view. The shared ordinary runtime stays
alive for other tabs.

MathJax preview parsers are isolated per editor scope, preserving that editor's declarations
without mutating another editor or the reader. Cancel work before releasing scopes on live
endpoints; the cleanup callback must not capture a destroyed controller. Announce readiness only
when the effective endpoint/generation changes, so the view can replay renderer-dependent caches.

`MarkdownViewWindowController::computeModeTransition` receives the desired EditViewMode:
EditOnly never requests a missing reader; Read/EditPreview do. Global editor-config invalidation
runs before per-window refresh. Coverage: `test_markdownwebjobcontroller` and
`test_markdownviewwindowcontroller`; native lifecycle/rendering coverage lives under tests/widgets.

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
| `MarkdownWebJobController` | Editor JS job routing, generations, typed results and protection policy |
| `TextViewWindowController` | Plain text editing |
| `PdfViewWindowController` | PDF viewing |
| `MindMapViewWindowController` | Mind map viewing |
| `NotebookSyncInfoController` | Git/WebDAV/Jianguoyun enable/disable, credential rotation, binding retirement, bootstrap recovery |
| `NewNotebookController` (sync portion) | New-notebook bootstrap via `bootstrapSync` (deletes notebook on enable failure) |
| `DashboardController` | Home dashboard (vx://home) layout model, occupancy math, seed/default, and WidgetConfig persistence; the `DashboardBoard` widget is its pure view |
| `NotificationRouter` | Turns subsystem failure signals into `NotificationMessage`s; owns attention/dedup policy (see below) |
| `CommentController` | The ONLY mutator of a file's `comments.json` set. Owns the active `NodeIdentifier`, debounces and coalesces add/edit/color/delete intents, receives asynchronous `CommentService` completion, and surfaces failures **without** marking the buffer modified (see below) |

## Editor Section Numbering

`MarkdownEditorController::generateSectionNumbers()` numbers only nonempty ATX (`#`) headings.
AST-empty headings and both Setext underline forms (`-` and `=`) remain untouched, including
existing numeric prefixes, and do not affect counters, base level or sole-H1 title detection.
Use `HeadingInfo::m_isEmpty` and `m_isSetext`, not rendered titles or source regexes:
image-only ATX headings still have content. Users can pause after `#` or a paragraph's
following `-` and continue typing `#abc` or `- item` without an inserted number.
The result stays index-aligned with the parsed headings. This is an editor source-writing
policy only; Markdown parsing and read-mode/outline numbering are unchanged.

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

The apply checkpoint must use `BufferService::isNoteConversionWritable()` for a
reserved buffer, not `Buffer2::isReadOnly()`: the conversion's own `Converting` flag
intentionally freezes editor writes. Missing reservations and genuine read-only
overrides still reject conversion; keep the live notebook read-only, revision,
participant, save/comment and source-hash checks. Conversion diagnostics identify
checkpoint reasons and backend phases without logging note bodies, paths, passwords
or hashes; see [logging diagnostics](../../docs/logging.md#note-conversion-diagnostics).

Dialogs must disclose that separate image/attachment files and comment sidecars
stay unencrypted. Attachments keep normal Open/Open Folder/Copy Path operations.
Encrypted note bodies cannot be exported or printed. Do not add a plaintext-copy
action or write temporary decrypted note files; ordinary exports remain unchanged.

`MarkdownEditorController::insertImageAsBase64` inserts a reference-style image
and a data-URI definition as one undoable edit. Image insertion lists Base64 first
and selects it by default only for encrypted Markdown. Ordinary notes retain
normal defaults. Base64 is encoding, not encryption: the embedded bytes receive
protection only when saved as part of an encrypted note body.


## Source-aware export

`ExportController` only selects sources, resolves canonical file-type names through
`FileTypeCoreService`, rejects protected notes and forwards progress/results. It must not
construct editors or WebEngine pages. Current Buffer keeps the supplied live snapshot from
`MainWindow2`; an explicitly supplied empty snapshot is authoritative, never a disk fallback.
Current Note and batch selections use disk snapshots without saving open notes.
`sourceFileTypes()` previews the actual selection without reading bodies, rendering or saving;
protected, empty and unresolvable selections are unavailable. Preview suppresses controller
collector diagnostics, but actual export retains protected-note refusal before writes.

`ExportRendererFactory` in `src/export` stores capabilities and creators together, using the
same lowercase/Others lookup for metadata and construction. Metadata queries create no renderer,
editor, browser or compiler. Built-ins map Markdown/Text/Others to `WebViewExporter`, MindMap to
`MindMapExporter`, and Typst to `TypstExporter`. Typst targets are Typst/HTML/PDF/Custom;
MindMap adds native MindMap to its Markdown/HTML/PDF/Custom projection targets. Markdown and
MindMap cannot target Typst. Persisted format values 0–3 are unchanged; Typst=4 and MindMap=5.

`Exporter` owns naming, attachments, per-operation renderer reuse and batch failure policy.
It validates every selected source/target pair before creating any output directory; empty
batches create nothing. Mixed selections use only shared formats, never silently omit an
incompatible source. Source output uses `renderSource` in unique per-note folders, preserving
the original Markdown filename, `.md` for MindMap projection, `.mmm` for native MindMap, and
`.typst` for Typst. Failed source attempts remove only their fresh bundle folder. Every HTML
consumer carries its Standalone/Pdf/Custom purpose in `ExportHtmlRequest`; never infer native
types from suffixes inside renderers or batch loops.

Native source output preserves the supplied QString text and uses library-owned content scanners.
MindMap URL paths are classified then URL-decoded; Typst image literals are already filenames
and retain literal `%`, `#` and `?`. Only internal relative image files are copied, preserving
subdirectories. Canonical containment includes ancestor symlinks/junctions; outside/missing
images warn, while copy failures and destination collisions fail without renaming or overwriting.
Whitespace-only MindMap snapshots bypass parsing and remain unchanged. Typst incomplete scans
copy known literals and warn; imports and dynamic references are not bundled. Attachments remain
an independent optional Exporter copy, not an imported-project bundler.

Typst HTML/PDF borrow the application-owned runtime through widget-independent `TypstCompiler`.
The canonical containing directory and actual main filename identify the project; only the
supplied text is overlaid. Theme text-font defaults apply, authored styles win, downloads remain
off and preview colors never enter output. Native PDF preserves document page layout and never
passes through HTML/wkhtmltopdf. Semantic HTML is reflowable, preserves native warnings, and does
not rewrite arbitrary authored HTML URLs. MIME HTML is unsupported. Cancellation retires the
compiler without shutting down the shared runtime; successful bytes publish through QSaveFile.

PDF all-in-one requires every source's Html backend and wkhtmltopdf; any native PDF source is
rejected before writes. Custom may combine heterogeneous raw disk paths or per-source HTML;
all HTML preparation must succeed before launching the command. Raw Custom deliberately uses
original saved files, even for Current Buffer. The selected Custom option is copied at operation
entry so event-loop mutations cannot invalidate it. `%3`/`%4` remain independent style inputs.

`ExportDialog2` intersects target values in first-source order and maps enum values explicitly
to source/HTML/PDF/Custom pages. Its visible controls are the union of applicable option groups;
labels hide with fields. Native Typst HTML/PDF show document-owned styling/layout information,
not web-print controls. Hidden preferences survive switching: only the effective export copy
suppresses unavailable PDF all-in-one. Custom style placeholders remain usable for native/raw
input, and all option widgets and scheme actions are disabled while export pumps events.

The HTML page always exposes **Apply customized styles** and **Edit**, including for native
renderers. The optional `htmlOption.applyCustomizedStyles` value stores only an explicit user
choice; absence defaults off for Markdown (or any mixed selection containing Markdown) and on
for other sources. Merely viewing a source's default must not persist it as an override.
`Exporter::doExportHtml` is the single application point for standalone HTML and PDF/Custom
HTML intermediates. Native source, raw Custom and native Typst PDF bypass it.

`ExportStyleSheet` resolves the user-owned `web/css/export.css` through ConfigMgr2. Missing
files are seeded from the single bundled default, including same-version profiles; existing
customized or empty files survive updates/recovery. Without ConfigMgr2, native-only consumers
use that bundled default. The default centers a border-box body with `max-width: 80%`. **Edit** opens
the real external CSS buffer without replacing the export's original source snapshot; save
before exporting. Each output embeds the saved CSS after renderer styles as a UTF-8 data-URL
stylesheet; relative CSS resources are not copied. Unreadable CSS or incompatible HTML fails
without returning a published HTML file. Markdown's exportglobalstyles.css remains separate.

MindMap Markdown is the m3 C API's readable document projection with `.md` naming and
media copied from that projection's resource base. The main topic is H1, levels 2–6 are
H2–H6, and deeper nodes are nested lists; orphan roots restart at H1. Notes become literal
text paragraphs, with images and meaningful links rather than labeled data fields, image
dimensions or internal link IDs. Tags use `#tag` notation. Like HTML articles, cross-links
appear beneath the originating topic and navigate directly to heading/list-item anchors;
labels are tooltips, and backward/both/undirected navigation matches HTML. There is no
relationship appendix. It is not a lossless native round trip.
Native HTML uses an export-only, fully expanded editor, rebased before every load, and waits
for queued image requests to settle. Missing images retain explicit placeholders; cancellation,
malformed input and the whole-export image deadline fail rather than snapshot incomplete work.
The visual map is selectable HTML node text plus inline SVG connectors, not a whole-map PNG;
only supplied node images are raster data. Standalone and Custom HTML use the same native
`toHtml()` document: a natural 100% map directly in normal flow, without map-width caps,
nested scrolling, viewer controls or sizing JavaScript. Topic anchors navigate to the continuous articles below it.
The visual map uses the mind-map automatic-numbering preference and shared editor section-number
pattern, refreshed for every export including renderer reuse. Main/orphan roots and article titles
remain unnumbered; article anchors use node identity rather than decorated visual text.
The requested m3 favicon is hosted on GitHub; map content, styles and supplied images are embedded.

Article depth restarts at 1 for every main/orphan root. H1–H6 are flat articles without list wrappers;
deeper nodes are genuine nested list items. Node rows are title, icons, tags, URL with link icon,
other-node links, then notes and figures, skipping absent values. Source text and cached pixels are
preserved without extra resource I/O. PDF still fits the full map on page 1 and prints articles
from page 2; Custom does not acquire paper fitting or forced breaks.

For PDF layout, validate the native section markers and positive `#mindmap` dimensions. Reserve
a scaled in-flow rectangle on `#visual-map` and make `#mindmap` absolute with natural dimensions;
apply CSS **layout zoom** only to that map. Pin geometry/overflow with `!important` and reset
html/body/main zoom and transforms. Keep the map page break and hide only the shared `main > h1`
and separator. Article titles keep with next; deeper lists retain outside bullets with a single
gutter rather than cumulative print indentation. Bound figures to the printable area and leave
notes splittable. A paint-only transform or unscaled flow width can shrink every printed page;
the regression produced 8pt rather than 12pt body text. The Qt 6.9+ host test reads every PDF page
with the bundled reader and checks map-first/article-next ordering, complete notes and image
geometry plus actual 12pt body. DOM bounds/signatures alone miss this. A PDF-only ES5 fitter
measures the styled parent's content width after load and before print, capping the map's zoom
and reserved footprint together. It must not undo the user's body width/margins or shrink
article fonts; standalone and Custom maps remain script-free.

`HtmlPdfExporter` owns final transport for Html-backend PDF targets, not native Typst or source
rendering. Qt loads the rendered HTML by file
URL (including >2 MiB documents), allows local sidecars and the existing unprotected remote-image
policy, and waits for decoded images/fonts before printing. Its off-record profile/page and scoped
non-displayed WebEngine view provide the render surface required for `image.decode()` completion.
Load, resource and print waits are bounded/cancellable; late callbacks cannot publish output or
complete another operation, and PDF bytes commit through `QSaveFile`. wkhtmltopdf remains the only
multi-document PDF backend; missing executables are failures, not backend fallbacks.

Intermediate Markdown policy: Qt PDF rasterizes math but retains vector diagrams; wkhtmltopdf
also enables SVG-to-PNG and diagram rasterization with its paper geometry and MathJax scale 1.5;
custom HTML enables math/SVG-to-PNG but retains vector diagrams and its selected scrollability.
Keep temporary HTML/resources alive until the external consumer returns. Raw custom input stays raw.

Behavior coverage: `tests/export/test_mindmapexport.cpp`, `test_htmlpdfexporter.cpp`,
`tests/widgets/test_typstexport.cpp`, `test_exportdialog2.cpp`, and the complete
controller/protected-editor targets.
The controller's substitute exporter tests source policy only; it is not evidence of rendering
correctness. Native compiler tests are registered in the vtextedit build subtree, so run its
`test_typsteditor` CTest directory separately when the parent CTest tree does not include it.
For Windows Debug GUI smoke, leave `QTWEBENGINEPROCESS_PATH` unset as the export tests do;
forcing the release `QtWebEngineProcess.exe` into a Debug host causes Mojo IPC failures.
