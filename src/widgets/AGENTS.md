# Widgets Module

Widgets are UI components that receive `ServiceLocator&` via constructor injection. This is the largest `src/` module, containing the main window, editor windows, dialogs, and the split-pane view area framework.

## `2` Suffix Convention

New architecture files use a `2` suffix (e.g., `MainWindow2`, `NotebookExplorer2`) to coexist with legacy code during migration. Legacy files without the suffix remain for reference but should not be used as templates for new code. See [Migration Guide](../../AGENTS.md#migration-guide) for full details.

## Widget Families

### MainWindow

- `MainWindow2` — new main window shell with ServiceLocator DI; owns the top-level layout, toolbar, sidebar, and view area

### Notebook Explorer

- `NotebookExplorer2` — sidebar explorer for notebook nodes; wires together the MVC triad (model, view, controller)
- `NotebookSelector2` — notebook dropdown selector

### ViewArea Framework

The split-pane editor area, designed around vxcore workspaces:

- `ViewArea2` — QSplitter tree managing split panes (pure view, no business logic)
- `ViewSplit2` — QTabWidget-based split pane; each instance maps 1:1 to a vxcore workspace
- `ViewWindow2` — abstract base for file viewer windows; receives a `Buffer2` in its constructor
- `ViewWindowFactory` (in `../gui/services/`) — registry mapping file types to `ViewWindow2` creators

### Concrete ViewWindows

- `MarkdownViewWindow2` — Markdown editor with preview
- `TextViewWindow2` — plain text editor
- `PdfViewWindow2` — PDF viewer
- `MindMapViewWindow2` — mind map viewer

### Toolbar

- `ToolbarHelper2` — main window toolbar construction
- `ViewWindowToolbarHelper2` — per-view-window toolbar construction

### Dialogs (`dialogs/`)

- `NewNoteDialog2`, `NewFolderDialog2`, `NewNotebookDialog2`
- `ManageNotebooksDialog2`, `ImportFolderDialog2`

Each dialog is driven by a corresponding controller in `../controllers/`.

### Search / Snippet / Tag

- `SearchPanel2`, `SnippetPanel2`
- `TagExplorer2`, `TagViewer2`, `TagPopup2`

### Dashboard (`dashboard/`)

The home dashboard shown at `vx://home`. Pure **view** layer — all layout logic
and persistence live in `DashboardController` (`../controllers/`).

- `DashboardContent` — `IViewWindowContent` host at `vx://home`; owns the board
- `DashboardBoard` — fixed-column `QGridLayout` of stickers; creates/owns its
  `DashboardController`, forwards user gestures as intents, and reacts to
  controller signals to build/move/remove sticker frames
- `Sticker` — abstract sticker content widget; `CalendarSticker` is the concrete built-in
- `StickerFactory` (in `../gui/services/`) — registry mapping sticker type-ids to creators

### Other

- `LocationList2` — results list (search hits, backlinks, etc.)
- `FindAndReplaceWidget2` — in-editor find/replace bar
- `AttachmentPopup2` — attachment management popup
- `WordCountPopup2` — word/character count display
- `NotificationButton2` / `NotificationPopup2` — toolbar notification button + popup (see below)

## Notification System

The in-app notification UI is the **View** layer over `NotificationService` (data/signals only, `src/core/services/notificationservice.{h,cpp}` — Qt-Widgets-free). Producers call `NotificationService::notify(NotificationMessage)`; deciding which subsystems emit is out of scope of the widgets.

- `NotificationButton2` (`QToolButton`) lives on the settings toolbar **immediately after the Theme button** (added by `ToolBarHelper2::setupNotificationButton`, called right after `setupThemeSwitcherButton` in `setupSettingsToolBar` — covers both unified and split toolbar paths). It paints a red badge with `NotificationService::activeCount()`, refreshes its bell icon on `ThemeService::themeChanged`, and auto-shows the popup on `messageAdded`.
- `NotificationPopup2` (extends `ButtonPopup`) lists messages newest-first with severity icon + title + text + per-message action buttons + Dismiss, and reuses the shared **`TitleBar`** class (same as every dock panel) for its header holding the "Notifications" title and the Clear All action button. Do NOT hand-roll a titlebar — construct `TitleBar(themeService, title, /*hasInfoLabel*/false, TitleBar::Action::None, parent)` and `addActionButton(iconName, text)`; it self-manages theme QSS and icon refresh.

### Lifetime-safety rules (MUST FOLLOW)

`NotificationAction::m_callback` is arbitrary application code. The popup keeps NO copy of it across a rebuild:

- Rows are rebuilt from `service.messages()` on every show and on `messageDismissed` / `messagesCleared` / `themeChanged` (while visible), so a stale row's callback can never fire after `clearAll()`.
- Action buttons capture only the message id + action index and re-resolve the callback from the **current** service state at click time (a cleared/dismissed message becomes an inert no-op).
- Because a callback may synchronously destroy the popup (e.g. restart the main window), every post-callback access to `this`/`m_services` is guarded with `QPointer<NotificationPopup2>`.

`Duration` controls only the auto-popup's auto-hide (`Short` ~3s, `Long` ~7s, `Persist` = no timer, owned by the popup's `QTimer`); it does NOT affect memory retention. Messages stay in the in-memory list until dismissed or cleared.


## MVC Rule for Widgets

See [MVC Rules](../../AGENTS.md#mvc-rules-must-follow) — Key rule for widgets: **All layers receive `ServiceLocator&`** via constructor injection (enables DI and testing).

Widgets are the **View** layer. They display data and emit signals but **MUST NOT** modify data directly. Business logic belongs in controllers; data access belongs in services.

## ViewArea2 Framework

For ViewArea2 framework design decisions (splitter orientation, session layout persistence, etc.), see [Key Design Decisions (ViewArea2)](../../AGENTS.md#key-design-decisions-viewarea2-framework) in root.

The orchestrator for all open/close/split/move operations is `ViewAreaController` in `../controllers/`.

## NotebookExplorer2: Sync Button State

`NotebookExplorer2::updateSyncButtonState` (`src/widgets/notebookexplorer2.cpp:1512-1665`) paints the per-notebook sync button and Sync Info menu. It classifies the current notebook into one of the 8 sync states (see root [Sync State Model](../../AGENTS.md#sync-state-model)) and sets the `partialSyncConfig` QWidget property, which downstream QSS reacts to.

### Classifier

`partialSyncConfig` is set to `true` for any non-ready state inside the `syncEnabled && syncReady` branch (`notebookexplorer2.cpp:1614-1628`):

| State | Detected by | Reason |
|---|---|---|
| S1 (no URL/backend) | `syncEnabled && (backend.isEmpty() || remoteUrl.isEmpty())` | Incomplete disk config |
| S2 (PAT missing) | `syncEnabled && syncReady && !credStore->hasCredentials(id)` | Disk-complete but no keychain entry |
| S4 (not registered) | `syncEnabled && syncReady && !syncSvc->isSyncRegistered(id)` | Disk-complete, PAT present, but vxcore runtime never registered |
| S5 (ready) | `syncEnabled && syncReady && hasCredentials && isRegistered` | partialSyncConfig=false; tooltip "Sync Now" |

Use `SyncCredentialsStore::hasCredentials(id)` (cached, paint-safe per W2.T0) instead of `retrieveCredentials`. Keychain access on every paint event is too expensive. Use `SyncService::isSyncRegistered(id)` (synchronous runtime query) for S4 detection.

Do not rename or remove the `partialSyncConfig` property: downstream QSS depends on it.

### Tooltip Variants

The button tooltip changes per state to give the user actionable guidance:

| State | Tooltip | Action on click |
|---|---|---|
| S5 | "Sync Now" | Triggers sync |
| S1/S2/S4 (partial) | Includes "credentials" or "initializ" hint | Opens `NotebookSyncInfoDialog2` in dialog's auto-detected mode |
| S0 | "Enable Sync" | Opens `NotebookSyncInfoDialog2` with `setBootstrapMode(true)` and empty fields |
| Reconcile error | Existing tooltip + `"Last sync init failed: error code %1"` appended | Same as the underlying partial/ready state |

The reconcile error code is stored in `m_lastReconcileError` (in-memory `QHash<QString, int>`, `notebookexplorer2.h:201`) populated by `SyncService::reconcileFinished` (`notebookexplorer2.cpp:130-140`) and cleared on `syncFinished` success or notebook switch. No persistence to disk; no toast/modal.

### Re-enable UI Affordance for S0

Without this affordance, users who disable sync can never re-enable without recreating the notebook. The Sync button and Sync Info menu remain visible AND clickable for S0 notebooks (`notebookexplorer2.cpp:1602-1635`):

- Button enabled regardless of `syncEnabled`; label changes to "Enable Sync" when `syncEnabled == false`.
- `onSyncButtonClicked` branches on `!syncEnabled` → opens `NotebookSyncInfoDialog2` with `setBootstrapMode(true)` (dialog hides the Disable button in this mode).
- Sync Info menu action enabled regardless of `syncEnabled` (was previously `setEnabled(syncEnabled)`).

The dialog's bootstrap-mode dispatch (`notebooksyncinfodialog2.cpp` accept/apply handlers) routes the user's inputs through `NotebookSyncInfoController::bootstrapApply` rather than `applyChanges`, performing the atomic enable that takes a clean S0 to S5 in one shot.

## Related Modules

- [`../core/AGENTS.md`](../core/AGENTS.md) — ServiceLocator, services injected into widgets
- [`../controllers/AGENTS.md`](../controllers/AGENTS.md) — Controllers that handle widget-initiated actions
- [`../views/AGENTS.md`](../views/AGENTS.md) — Views embedded in widgets
- [`../gui/AGENTS.md`](../gui/AGENTS.md) — GUI services and utilities used by widgets
- [`../../AGENTS.md`](../../AGENTS.md) — Architecture overview, MVC rules, ViewArea2 design decisions
