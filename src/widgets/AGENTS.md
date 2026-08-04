# Widgets Module

Widgets are UI components that receive `ServiceLocator&` via constructor injection. This is the largest `src/` module, containing the main window, editor windows, dialogs, and the split-pane view area framework.

## `2` Suffix Convention

Many existing files here carry a `2` suffix (e.g., `MainWindow2`, `NotebookExplorer2`). This is a historical artifact of the now-complete migration off the legacy singleton architecture — the pre-migration counterparts have been removed, and the suffix is simply the retained name for those existing classes (renaming them would be needless churn). **New code does NOT get a suffix unless the name genuinely conflicts with an existing type.** Since the migration is finished, a brand-new widget with no `1`/legacy counterpart should use the plain name (e.g. `EncodingButton`, not `EncodingButton2`). Never introduce a `3` suffix. See [root AGENTS.md](../../AGENTS.md#widget-construction-pattern) for the constructor-injection pattern all widgets follow.

## Hiding the QToolButton Menu Indicator

Plain-text status-bar / toolbar `QToolButton`s that open an `InstantPopup` menu
(e.g. the status bar "Spelling" menu, the `EncodingButton`, the toolbar theme
switcher) must NOT show the built-in dropdown-arrow menu indicator. Use the
single shared mechanism — the dynamic property:

```cpp
btn->setProperty("NoMenuIndicator", true);
```

Every bundled theme's `src/data/extra/themes/<theme>/interface.qss` styles this
away with `QToolButton[NoMenuIndicator="true"]::menu-indicator { image: none; }`,
so all such buttons share one look. Do NOT hand-roll a per-button inline
stylesheet (`setStyleSheet("QToolButton::menu-indicator { image: none; }")`) —
that bypasses theming and drifts from the shared style. When adding a new theme,
carry the `NoMenuIndicator` rule forward. Pair the property with
`setToolButtonStyle(Qt::ToolButtonTextOnly)` + `setAutoRaise(true)` for the flat,
text-only status-bar look.

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
- `PdfViewWindow2` — PDF viewer. Like `MarkdownViewWindow2`, it overrides
  `getOutlineProvider()` and feeds the Outline dock + toolbar popup. The JS↔C++
  outline contract is **index-based**: JS owns the pdf.js `dest` objects and
  publishes `{ name, level, index }` per entry, where `index` addresses a private
  destination array on the web side and is `-1` when the entry is not jumpable.
  It must be carried explicitly — `OutlineProvider::makePerfectHeadings` inserts
  filler headings, so a heading's position is NOT its destination index. See
  `src/data/extra/web/pdf.js/pdfviewercore.js` and
  `tests/widgets/test_pdfviewercore_js.cpp`.
- `MindMapViewWindow2` — mind map viewer
- `WidgetViewWindow2` — generic widget-hosting window

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

The in-app notification UI is the **View** layer over `NotificationService` (data/signals only, `src/core/services/notificationservice.{h,cpp}` — Qt-Widgets-free). Producers are wired in `NotificationRouter` (`src/controllers/notificationrouter.{h,cpp}`) and `UpdateController`; deciding which subsystems emit is out of scope of the widgets.

Three surfaces, one rule each:

- `NotificationToast` (`notificationtoast.{h,cpp}`) is the **transient** surface for `Attention::Interrupt`. It is a plain **child `QFrame` of `MainWindow2`**, anchored bottom-right of the central widget — NOT a `QMenu` and NOT a `Qt::Tool` top-level. A child widget cannot take window activation, so an arriving toast can never eat the user's keystrokes; being a child (rather than a `Qt::Tool` window) also avoids the Windows native-unmap-on-deactivate quirk, multi-monitor clamping and taskbar overlap.
- `NotificationButton2` (`QToolButton`) lives on the settings toolbar immediately after the Theme button. It paints a red badge with `NotificationService::activeCount()` and refreshes its bell icon on `ThemeService::themeChanged`. It **does not auto-show the popup** — `showPopup()` is called on click, or by `MainWindow2` forwarding `NotificationToast::popupRequested`.
- `NotificationPopup2` (extends `ButtonPopup`) is the click-to-open **notification centre**: messages newest-first with severity icon + title + text + optional collapsible "Details" + optional progress bar + per-message action buttons + Dismiss, in a height-capped `QScrollArea`, under the shared `TitleBar` holding "Notifications" and Clear All. Do NOT hand-roll a titlebar.

### Attention → surface routing (owned by `NotificationToast`)

The routing table lives INSIDE the toast rather than in `MainWindow2`, so it is unit-testable against the real widget instead of a duplicated copy. `MainWindow2` injects the two window-policy inputs:

- `setCanShowInWindow(...)` — must be `isVisible() && !(windowState() & Qt::WindowMinimized)`. **`isVisible()` alone is not enough**: a minimized top-level window is still logically visible, so an in-window child would be "shown" where the user cannot see it.
- `setFallbackSink(...)` — where an `Interrupt` goes when the above is false. `MainWindow2` routes it to `QSystemTrayIcon::showMessage()` (title + text only; a balloon carries no actions and cannot be retracted).

| Signal | Condition | Toast action |
|---|---|---|
| `messageAdded` | `Interrupt` | show (or fall back to the sink) |
| `messageAdded` | `Passive` | nothing |
| `messageUpdated` | shown id, `Interrupt` | refresh content, **do not** restart the timer |
| `messageUpdated` | shown id, `Passive` | **hide** (the producer downgraded it; leaving stale interrupting content up would show a stale action button) |
| `messageUpdated` | any other id | nothing |
| `messageDismissed` / `messageRemoved` | shown id | hide |
| `messagesCleared` | always | hide |

**A `messageAdded` carrying `Interrupt` is the ONLY thing that raises the toast.** A producer that must re-interrupt an ongoing incident calls `NotificationService::renotify()`, which removes the old generation and posts a new one. See `src/core/services/AGENTS.md` § NotificationService for why there is no escalation signal.

`renotify()` always changes the id, so it necessarily runs `messageRemoved(old)` → hide, then `messageAdded(new)` → show. Under the current GUI-thread-only producer contract both deliveries are direct and synchronous, so the pair completes before a repaint and no blank frame is rendered. **Keep these plain `hide()`/`show()` calls** — adding a fade, animation or deferred teardown would turn that into a visible flicker. The accepted cost of this two-signal protocol is that a *visible* popup rebuilds twice and the badge recomputes twice per `renotify()`.

### Progress and in-place updates

A message renders a `QProgressBar` when `NotificationMessage::m_progressIndeterminate` is
set (range `0..0`, busy indicator, wins over the permille) or `m_progressPermille >= 0`
(range `0..1000`, clamped). Permille rather than percent, so a multi-hundred-megabyte
download still moves the bar smoothly — same scale as `UpdateDialog`.

The popup rebuilds on `messageAdded` / `messageUpdated` / `messageDismissed` /
`messageRemoved` / `messagesCleared` / `themeChanged`, but **only when it is already
visible**. `messageAdded` is in that list deliberately: nothing auto-pops any more, so
without it an open popup would go stale.

`NotificationAction::m_dismissOnTrigger` (default `true`) controls whether triggering the
action dismisses the message. Producers that keep updating one message in place — the
updater's Update / Cancel / Retry — set it `false`.

`m_details` is rendered as a collapsible disclosure in the **popup list only** — never in
the toast or the tray balloon, both of which must stay small. It is the home for the error
blobs that used to be a `QMessageBox::setDetailedText` (or were lost to `qWarning`).

### Per-severity accent

Rows and the toast set `PropertyDefs::c_state` (`info` / `warning` / `error` / `success`),
driven by the shared `*[State="..."]` rules every theme defines, and tint their severity
icon with `@base#<state>#fg`. When adding a theme, carry all four states plus a
`vnotex--NotificationToast` block forward; `tests/core/test_theme.cpp`
(`testInterfaceQssFullyResolved`) is the gate.

### Lifetime-safety rules (MUST FOLLOW)

`NotificationAction::m_callback` is arbitrary application code. Neither the popup nor the toast keeps a copy of it across a rebuild:

- Rows are rebuilt from `service.messages()` on every show and on the signals above, so a stale row's callback can never fire after `clearAll()`.
- Action buttons capture only the message id + action index and re-resolve the callback from the **current** service state at click time (a cleared/dismissed/removed message becomes an inert no-op).
- The callback AND its `m_dismissOnTrigger` flag are snapshotted in that **same** lookup, before the callback runs. Do NOT re-resolve the action index afterwards: an Update/Retry callback synchronously replaces the action vector (with Cancel / Restart), so a second lookup would read a different action's flag.
- Because a callback may synchronously destroy the widget (e.g. restart the main window), every post-callback access to `this`/`m_services` is guarded with a `QPointer`. The callback may also have already triggered a rebuild via `messageUpdated`, so nothing after it may touch the row widgets that existed when the lambda started.

`Duration` controls only auto-hide (`Short` 3 s, `Long` 7 s, `Persist` = capped at 15 s **on the toast only**, so a toast is never permanently stuck); it does NOT affect memory retention. Messages stay in the in-memory list until dismissed, cleared, or evicted by the retention cap.



## Inline Notification Banners

Use `InlineBanner` (`src/widgets/inlinebanner.{h,cpp}`) for any inline
notification strip — an in-editor prompt above the content, a "results
truncated" warning above a list, a dialog-level notice. Do NOT hand-roll a
`QLabel` with an inline stylesheet; that is exactly the drift this class
replaces (it previously existed twice, in `LocationList2` and the legacy-image
bar, with copy-pasted `#FFF3CD` / `#856404` hex that looked wrong in all six
dark themes).

`InlineBanner` is a **pure view**: a wrapping message plus zero or more trailing
action buttons. It takes no `ServiceLocator` — it has no service needs, matching
the other leaf presentational widgets (`EncodingButton`, `StatusBar`). Consumers
own all policy.

```cpp
auto *banner = new InlineBanner(InlineBanner::Severity::Warning, tr("..."), this);
connect(banner->addActionButton(tr("Fix It")), &QPushButton::clicked, this, &X::onFix);
addTopWidget(banner);   // ViewWindow2 host; or any QLayout elsewhere
```

`addActionButton()` returns the `QPushButton` it created (the
`QDialogButtonBox::addButton` convention) so the caller connects it directly —
there is no index bookkeeping and no signal the banner has to re-emit.

For a feature-specific banner, subclass it and keep the copy plus the named
intents in the subclass (see `LegacyImageMigrationBar`). Never put the strings
in the hosting view window.

### Theming (do not bypass)

Severity is published as the `BannerSeverity` dynamic property
(`PropertyDefs::c_bannerSeverity`, values `info` / `warning` / `error`), set via
`WidgetUtils::setPropertyDynamically` so the style engine repolishes. Every
bundled theme styles it:

```qss
vnotex--InlineBanner { background-color: @base#normal#bg; ... }
vnotex--InlineBanner[BannerSeverity="warning"] { border-left: 3px solid @base#warning#fg; }
```

The block uses **only** `@base#` tokens, because those are the only ones every
theme defines — `native` has no `widgets.qwidget` section, and only `danger`
has a background role, so info/warning/error are carried by the left rule
rather than a tinted fill.

**When adding a theme, carry the `vnotex--InlineBanner` rules forward.** A
missing palette key does not fail loudly: `Theme::translateStyleByPalette` logs
a `qWarning` and leaves the literal `@base#...` in the stylesheet, after which
Qt's CSS parser silently drops the declaration. Two data-driven gates in
`tests/gui/test_themeservice.cpp` cover this for all 10 themes —
`interfaceQssFullyResolved` (no unresolved token survives) and
`interfaceQssStylesInlineBanner` (the selector and both severity rules exist).

`InlineBanner` sets `Qt::WA_StyledBackground`; without it a bare `QFrame`
subclass ignores `background-color` from the global stylesheet.

Note that the primary instance copies themes into `<appData>/themes` after
acquiring `SingleInstanceGuard`; rejected secondary launches never mutate the
resource tree. Themes are refreshed only when the per-folder stamp
`<appData>/themes/.vnote-extra-version` does not match
`ConfigMgr2::c_version`, so QSS edits reach an existing installation only at the
next version bump (`scripts/update_version.py` handles that). The stamp is
written by `FileUtils2::installVersionedDir` **only after the folder copied
completely**, which is what makes a partial copy retry on the next launch
instead of being remembered as done; deleting the stamp (or the folder) forces a
re-copy at the next start. For local work, run with `--watch-themes` and edit
the deployed copy.

## No Hardcoded Colors in C++

**Never put a literal color in a `setStyleSheet()` call.** VNote ships 10
themes, 6 of them dark, and the global stylesheet is applied on `QApplication`
(`main.cpp:725`) and re-applied on every theme change. A hardcoded
`background-color: #FFF3CD` is correct only in whichever theme its author
happened to be running, and it cannot follow a theme switch.

This is enforced: `tests/utils/test_hardcoded_color_drift.cpp` parses every
`.cpp`/`.h` under `src/`, extracts the string literals (comment-aware, raw-string
aware, and **coalescing adjacent literals** the way the compiler does), and fails
when one contains **both** a CSS color property and a literal color value — a
`#hex`, a numeric `rgb()/rgba()/hsl()/hsla()/hsv()/hsva()`, or any name in
`QColor::colorNames()` except `transparent`.

Use, in order of preference:

| Need | Do this |
|---|---|
| A notification strip | `InlineBanner` (see above) — already themed |
| Severity-colored **text** | Set the `SeverityText` property (`PropertyDefs::c_severityText`, values `info`/`warning`/`error`) via `WidgetUtils::setPropertyDynamically`; every `interface.qss` maps it to `@base#{info,warning,error}#fg`. An unset/empty value falls back to the normal color. |
| Muted / secondary / hint text | Set the `MutedText` property (`PropertyDefs::c_mutedText`, value `true`). Add italics with `QFont::setItalic`, not QSS. |
| Anything else static | Add the rule to each theme's `interface.qss`, selecting on the class name (`vnotex--YourWidget`) or a dynamic property. This is the only option that re-themes for free. |
| A color computed at runtime | `ThemeService::paletteColor("widgets#foo#bg")` interpolated with `.arg()`, as in `NavigationMode::generateNavigationLabelStyle`. A style string whose color comes from a `%N` placeholder is not flagged. |

**Do NOT use `setEnabled(false)` to mute text.** It advertises
`QAccessible::State::unavailable` for what is ordinary informative text, and it
does not even work here: every theme styles `QLabel { color: ... }`
unconditionally with no `:disabled` variant, so the palette's disabled role
never reaches the label. An attribute selector such as `*[MutedText="true"]`
outranks the plain type rule, which is why the property works where the disabled
state does not.

`MutedText` resolves to each theme's dedicated `base.muted.fg`, **not** to
`base.disabled.fg`. The disabled role is tuned for disabled *controls* and is
far too faint for enabled text — on `solarized-light` it is `#DAD3C2` over
`#FDF6E3`, about **1.38:1**. The `base.muted.fg` values are derived by blending
the theme's normal foreground toward its background until the WCAG contrast
floor is hit, and `TestThemeService::mutedTextIsReadable` asserts the resolved
ratio per theme: at least `min(4.5, contrast(normal.fg, normal.bg))`, and never
*more* contrast than normal text. (`native` is skipped — it takes the OS's own
muted color from the system palette at runtime.) When adding a theme, add
`base.muted.fg` and let that test tell you whether the value is legible.

What is **not** an offender, and is not flagged:

- A color literal used as **data** rather than chrome — the mark-node swatch
  palette (`marknodedialog2.cpp`) and the notebook avatar colors
  (`notebookselector2.cpp`) offer colors *to* the user.
- A colorless style string, e.g. `"QLabel { font-style: italic; }"`. (Prefer
  `QFont` anyway; and for the menu indicator use the `NoMenuIndicator` property
  documented above, not an inline stylesheet.)

Known scope limits, so nobody mistakes a green build for a proof: the gate
covers **stylesheet strings only**. A `QColor` painted directly in a
`paintEvent` or a `QStyledItemDelegate` is out of scope, and a color assembled
at runtime from a non-literal constant is indistinguishable from the legitimate
`paletteColor()` pattern without real type analysis. Those still need review.

If you genuinely need a literal, append `// hardcoded-color-allow: <reason>` to
any line the literal spans. Use it sparingly — every existing case was
removable.

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
