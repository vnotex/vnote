# Widgets Module

Widgets are UI components that receive `ServiceLocator&` via constructor injection. This is the largest `src/` module, containing the main window, editor windows, dialogs, and the split-pane view area framework.

## `2` Suffix Convention

Many existing files here carry a `2` suffix (e.g., `MainWindow2`, `NotebookExplorer2`). This is a historical artifact of the now-complete migration off the legacy singleton architecture — the pre-migration counterparts have been removed, and the suffix is simply the retained name for those existing classes (renaming them would be needless churn). **New code does NOT get a suffix unless the name genuinely conflicts with an existing type.** Since the migration is finished, a brand-new widget with no `1`/legacy counterpart should use the plain name (e.g. `EncodingButton`, not `EncodingButton2`). Never introduce a `3` suffix. See [Widget Construction Pattern](#widget-construction-pattern) below for the constructor-injection pattern all widgets follow.

## Toolbar Buttons With a Popup Menu

Put the menu on the **QAction**, and let the toolbar create the button:

```cpp
auto *act = p_toolBar->addAction(icon, tr("More"));
act->setMenu(menu);                                     // <- on the ACTION
auto *btn = qobject_cast<QToolButton *>(p_toolBar->widgetForAction(act));
btn->setPopupMode(QToolButton::InstantPopup);
```

**Do NOT build a `QToolButton` yourself and add it with `QToolBar::addWidget()`.**
That produces a `QWidgetAction`, and when the toolbar runs out of horizontal room
it hides the trailing items and re-offers them through its own extension (`»`)
popup — which it builds by adding the hidden **actions** to a `QMenu`. A
`QWidgetAction` cannot render there, so the button silently **disappears** and
every verb behind it becomes unreachable, while the plain actions beside it keep
working. This shipped once in `PdfViewerToolBar`: at narrow widths the whole
overflow menu (rotate, cursor, scroll mode, spread mode, presentation, document
properties) was simply gone.

With the menu on the action both surfaces work from one declaration:
`QToolButton::menu()` falls back to `defaultAction()->menu()` on the toolbar, and
`QMenu` renders an action-with-a-menu as a **submenu** inside the extension
popup. It also makes the icon reachable by
`ViewWindowToolBarHelper2::refreshToolBarIcons()`, which recursively visits the
toolbar actions and their action-owned menus. Precedents: `Outline` / `Tag` / `Attachment` in
`viewwindowtoolbarhelper2.cpp`. Gate:
`theOverflowMenuSurvivesANarrowToolBar` in `tests/widgets/test_pdfviewertoolbar.cpp`,
which resizes a real toolbar until it overflows.

`addWidget()` remains correct for something that genuinely **is** a widget — a
spin box, a combo, a slider. Those degrade by vanishing at narrow widths, which
is accepted; make sure a plain-action route to the same operation exists (the
PDF page spin box is flanked by prev/next page actions for exactly this reason).

Custom editor-toolbar menus derive from `ButtonPopup` and opt into
`ButtonPopup::Alignment::Right` in their constructor. The base anchors to the
button's global right edge and clamps horizontally to its screen's available
geometry; Qt retains vertical placement. Never shift from the menu's existing
position in a subclass `showEvent()` — Qt may already have moved it at a screen
edge. Outline, Tags, Attachments, Word Count and Insert Table share this policy.
Hidden overflow buttons keep Qt placement, as do non-editor popups using the
`Native` default (including Notifications). Ordinary `QMenu`s remain Qt-managed.
Gate: `tests/widgets/test_buttonpopup.cpp`.

## Hiding the QToolButton Menu Indicator
Status-bar / toolbar `QToolButton`s that open an `InstantPopup` menu
(e.g. the status bar "Spelling" menu, the `EncodingButton`, the main toolbar
"View" and "Settings" menus) must NOT show the built-in dropdown-arrow menu indicator. Use the
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

The main toolbar ends with icon-only **View** and **Settings** menus, then the
notification button. View starts with **Expand Content Area**; Settings starts
with the **Settings** action and a **Themes** submenu. Those primary actions
are also registered on the main window so their shortcuts work with menus closed.
View uses Lucide `view.svg`; Settings uses `settings.svg`. Both retain translated action labels.

Comments and Tools remember their separate docked sizes across editor switches,
content-area expansion and restarts, including when saved while hidden. `DockSizeMemory`
restores after the dock is shown and laid out; same-row/column peers share the remaining
space and Qt still enforces size constraints. Parent-window hide/show does not replay
an old size. Floating and tabbed geometry remains owned by Qt's main-window state.
`SessionConfig::MainWindowStateGeometry` stores `commentDockSize` and `toolsDockSize`
as optional `[width, height]` values. **Reset Main Window Layout** clears both preferences.
Fresh layouts use Qt's size hints; there is no fixed Outline/Comments ratio.

### Notebook Explorer

- `NotebookExplorer2` — sidebar explorer for notebook nodes; wires together the MVC triad (model, view, controller)
- `NotebookSelector2` — notebook dropdown selector

`NotebookExplorer2` owns both its `NotebookAfterClose` and `FileBeforeSave` hook handles
and removes them in its destructor. `HookManager` callbacks are not QObject connections;
widget destruction does not disconnect them automatically.

Windows directory watches on expanded descendants can block a parent folder rename.
`NotebookExplorer2` pauses its watches at `NotebookCoreService::folderRenameStarted`
and restores them at `folderRenameFinished`, rebasing descendants only on success.
These signals bracket the synchronous backend call after cancellable hooks; do not
pause for the inline-edit session or restore through the success-only rename hook.

### ViewArea Framework

The split-pane editor area, designed around vxcore workspaces:

- `ViewArea2` — QSplitter tree managing split panes (pure view, no business logic)
- `ViewSplit2` — QTabWidget-based split pane; each instance maps 1:1 to a vxcore workspace
- `ViewWindow2` — abstract base for file viewer windows; receives an initial `Buffer2` and owns guarded same-type rebinding

Markdown Edit-to-Read transitions must adopt the revision from their successful `syncNow()`
before exposing/focusing the reader. A delayed native focus gain otherwise misclassifies that
own write as an external edit and cancels a pending presentation. Real foreign revisions still
refresh through `onFocusGained()`. `presentationCreatesReader` covers the delayed-focus ordering.
First-reader creation can rebuild a native ancestor and transiently hide the view. Ignore those
Hide notifications only while preparing presentation, then validate the settled visibility and
enabled state before sending a request. A genuinely hidden view must not start presentation when
shown later; Close, Escape and disabling still cancel immediately. Preparation may pump events,
so its state restoration must be guarded by the view's lifetime.

`ViewWindow2` mounts both `StatusBar` and shared `StatusWidget` through `StatusOverlay`,
not through the bottom layout. Each strip floats over the bottom edge of its own content
layout, above Find and other bottom widgets; showing, clearing or expiring a message must
not resize the editor/WebEngine viewport. Persistent editor controls also float: the strip
covers content rather than reserving a row. Geometry follows layout, resize and theme/font
changes, including detached and fullscreen hosts; shared widgets reparented away are no
longer positioned by their previous attachment.

Message-only surfaces pass mouse input through; editor and corner controls remain interactive.
Windows overlays need native stacking above late-created WebEngine children and input
transparency on both QWidget and its native window, preserved across hide/show. Keep the
background theme-painted and URLs plain text. Every bundled `interface.qss` gives both
status surfaces a 6px radius, palette-based border/fill, and padded messages; internal stack
pages stay transparent without overriding the nested editor chips or corner controls.
Windows derives the native mask from the QSS-painted surface, cached until size/style changes,
so changing the QSS radius does not require a matching C++ value. Activate the content layout
before sampling its geometry; an invalidated layout can temporarily report an empty rectangle.
Markdown Read hides an empty overlay; Edit retains its status controls. Regression coverage:
`test_statusbar` and `test_statuswidget`.

#### Link preview dismissal

`LinkPreviewOverlay` requests the existing cancellable workspace close on a primary click in
its originating view's exposed content or toolbar. Save/Discard/Cancel remains authoritative;
Cancel preserves the remaining preview tabs and their keyboard focus; accepted dismissal
returns focus to the source. Consume the dismissal press/release and double-click continuation
so source links and toolbar actions do not fire.
Clicks inside the preview, other views, popups and modal dialogs retain their normal routing.
Dragging, focus loss and source hide/show do not dismiss drafts. The application mouse filter
is installed only while the overlay is visible; a short source-owned guard handles a trailing
double-click after the closed overlay retires, without delaying closure or owning tabs.

Native-rendered Markdown links handled by the application intercept trusted clicks before
browser navigation and bind activation to the current document generation. Browser-owned
script/data actions retain their behavior. Local file URLs are converted to paths before
entering the existing link policy; same-document anchors scroll in place. Chromium emits
`loadStarted` even for rejected link navigation, so allowing that navigation would retire the
still-visible source bridge and break subsequent links. Genuine reload invalidation is unchanged.

#### Note tab reuse

Settings → Note Management → **Always open notes in new tabs** persists
`core.alwaysOpenNotesInNewTabs`, default `true` (also for missing/non-boolean values).
Apply affects the next open. With it off, ordinary focused opens replace the current
file tab; `FileOpenSettings::m_alwaysNewWindow` and Ctrl on existing explorer activation
gestures bypass replacement. Ctrl does not turn selection-only clicks into activation.
Multi-note opens, restore, reopen-last-closed, split clones and detached/background opens
retain separate-tab behavior. An already-open target still deduplicates within its workspace.
Settings and other virtual content are never replacement candidates; lone-Home retirement
remains owned by the existing successful-open path.

`ViewWindowFactory` stamps the normalized creator key. Reuse requires that exact key and
`supportsBufferReplacement()`; Text and Others are distinct even though their class matches.
The controller owns add-before-remove workspace membership, exact-ID reentrancy guards,
rollback, closed-tab history and consistent current-buffer publication. Unowned candidates
must be closed through BufferService after checking all visible, hidden and detached owners.
A retained window ID does not imply an unchanged buffer: explicitly refresh current providers.

`ViewWindow2::replaceBuffer()` calls `prepareBufferReplacement()` before comment flushing or
Save/Discard/Cancel can pump events. Typst revokes pending insertion/image actions and closes its
image overlay here, even if replacement is later cancelled; the outgoing draft/history remain intact.
It then obtains a durable CommentService checkpoint and uses qualified base confirmation before
releasing outgoing resources. Refusal leaves the
old document/history attached and never falls through to another open strategy. Different-type
opens create successfully before closing the old tab; cancellation then leaves both tabs.
Never reload or clear a shared outgoing buffer merely to discard this view.

Release runs against the old buffer; bind runs after assigning the new handle into the stable
base member and advancing its generation. Reset document history (including load-time formatting
commands), selection, find and caret even for equal text/revision. Preserve native editors,
ordinary readers, tool/status widgets and user zoom/layout preferences. Late navigation, preview,
image and print callbacks must validate document generation, not just the retained QObject.
Incoming load failure hides/blanks unavailable content and rejects snapshots; it must never save
the outgoing document under the new identity.

Protected Markdown reader pages/profiles are authorization-bound: retire page before profile
when either identity is protected, without terminally destroying the retained native editor.
Web adapter reset clears pending actions/callbacks even while already not-ready. PDF replacement
flushes JS-debounced comment drafts before the durable gate, uses `setActiveFile()` for comments,
and clears document replay through `resetForNewDocument()` both on release and after the inert
navigation succeeds, immediately before re-registering the adapter. The second reset discards
outgoing WebChannel calls that recaptured replay during retirement; theme reload still replays
that PDF's own camera. Typst uses native `resetForNewDocument()` and calls `finishDocumentReset()` only
after installing the validated incoming project and source; its native compiler session survives.

Coverage: `test_viewarea_tab_reuse` uses production services, windows, real explorer gestures,
dialogs and separate test-mode session contexts; `test_configmgr2` covers preference persistence
and `test_pdfvieweradapter_viewer` covers new-document versus same-document replay.

#### Native Typst editor

`TypstViewWindow` hosts one `vte::VTypstEditor` and borrows its `VTextEditor` for source
helpers. `.typst` is a newable/searchable plaintext registry type; `.typ` is not claimed.
Legacy nonempty registries gain Typst without stealing suffixes or changing saved rows.
Registry-local `schemaVersion` preserves deliberate deletions after migration; the Qt
wrapper and encryption allowlists remain unchanged.

Ordinary opens default to Edit with both Live Preview and In-Place Preview enabled.
Explicit Quick Access Read/Edit and restored/last-closed modes remain authoritative;
ordinary activation preserves the tab's presentation. Use the common Markdown-style
Read/Edit toolbar action, including Save-and-Read and Discard behavior. Its Edit-only
Menu actions independently toggle Live Preview and In-Place Preview; either, both, or
neither can be enabled. The native editor owns these runtime preferences and retains
them across Read; only the existing host Read/Edit mode is persisted. Read always shows
PDF-only, regardless of the stored Edit preferences, without replacing the buffer's
writer/save lifecycle. The AlternateViewMode shortcut toggles Live Preview via the common
action helper. Find uses source in Edit and native PDF search in Read; source-only options
are saved/restored locally with `FindAndReplaceWidget2::setFindOptions()`.

Session restoration uses the shared source-line cursor metadata and `FileOpenSettings`, like
Markdown. Native preview layout must keep that restored caret visible; fresh opens start at
the top, not at an anchor retained from the empty document. Do not add host scroll timers.
`test_viewarea_tab_reuse::typstSessionPosition` covers a new session context with either tab policy.

Like Markdown, the editor status bar collapses in Read and returns in Edit. Transient
messages temporarily expand it in Read, then collapse it again on timeout.

Compiler diagnostics stay in the native bottom panel, without a duplicate compile-message
column in the host status bar. With Live Preview enabled, source caret movement reveals the
mapped PDF location automatically; VNote exposes no separate manual navigation action.
The main Menu has no manual compilation or publication commands. Source edits drive native
compilation; pending PDF updates held by a selection publish when that selection is cleared.

The canonical containing directory is the project boundary and the actual basename is
the main/active file. Imports use saved files under that tree; only the current source is
an unsaved overlay. Set `TypstEditorConfig::m_runtime` from the application-owned
`vte::TypstRuntime` in ServiceLocator before constructing the native widget. The runtime
outlives the windows and drains after accepted-close image scans. No executable lookup or
fallback remains; the shared library is a required loader dependency. Downloads stay disabled.
Missing/stopped runtime still permits source editing and saving; failed source reads instead
disable mutations and reject save snapshots.

Use inherited focus/save/reload/close and BufferService, never per-keystroke buffer writes
or QFile saves. Retarget project/active identity and remove the obsolete overlay without
reloading text. Theme changes go through the composite `setConfig()`, preserving source
history and PDF zoom. The right-side toolbar Menu's **Theme Style in Preview** action is
checkable in both Edit and Read. It defaults on per view, retains its choice through mode,
theme and editor-config changes, and is not persisted or exposed in Settings. Other Typst
views are independent. Pass the action's checked state to
`TypstEditorConfig::m_enableThemeStyleInPreview` on every config refresh, and set
`m_previewForeground` from `base#content#fg`. The native compiler applies page/text color
defaults only for previews; explicit document styles win and export colors remain unchanged.
Set `TypstEditorConfig::m_pdfBackground` from `ThemeService::getBaseBackground()`, matching
the PDF view's outer canvas in Edit and Read. The default preview page fill instead follows
`text-editor.theme`'s `typst-editor-styles.Text.background-color`, inheriting
`editor-styles.Text.background-color` when omitted, just like the source editor.
Turning the option off restores native Typst page/text colors without changing the canvas.
The native pane paints the canvas color directly:
QSS can override widget palettes, so a `QPalette::Dark` override is not sufficient.

The resolved `text-editor.theme` also supports `typst-editor-styles.Text`. Native code merges
it over the source editor's base Text style without mutating shared themes. Its explicit
`font-family` list and positive integer `font-size` (points) also supply default typography
for PDF/in-place previews and native compiler exports; authored document styles win.
Omitted font properties preserve native Typst output defaults. Source zoom and **Theme Style
in Preview** affect neither these font defaults nor exported typography. See
[`vtextedit`'s font contract](../../libs/vtextedit/AGENTS.md#typst-text-theme-defaults).
Bundled VNote themes set these two font properties to match their own
`markdown-editor-styles.Text`; keep the defaults aligned when updating a bundled theme.
These are initial values, not a live reference: custom Typst font overrides remain independent.

Publish compiled heading metadata in order, including repeated/generated/imported/deep
headings, with no producer-side gap fillers or Markdown parsing. The shared outline model
may display its own gap rows while preserving producer indices. Disable reorder and
automatic section numbering. `TypstViewWindowController` owns canonical source containment
and one-shot generation-bound external-link consent; the view owns guarded modal UI.
Destroy the compiler widget only after close is accepted. Printing is not exposed.

The Typst toolbar separates its common buttons from **Link**, then **Image**. Preview local-file
links use `BufferService::openBuffer()` for both indexed notes and unindexed/standalone files,
preserving the decoded fragment. `ViewAreaController` owns editor selection, configured external
program associations and unsupported-type fallback; the Typst controller must not launch local
files directly or fall back to the desktop after a cancelled/failed buffer open. HTTP(S), FTP and
mailto links retain generation-bound external-link confirmation.

Source, in-place cards, and the PDF preview augment their native menus with **View Image**,
**Copy Image**, project-backed **Copy Image Address**, and safe writable-source **Resize Image**.
Links offer **Open Link** and URI **Copy Link**; internal page destinations have no fabricated
URI. The native compiler owns resource identity, evaluated ranges and resize plans; never
parse Typst with host regexes or use a page crop as the original image. Resize preserves other
arguments/comments and commits one native Undo step. Viewing/copying also works in Read and
read-only notes, without source edits, asset writes, or network acquisition.

**Open Link** is queued on the owning `TypstViewWindow`, not on the temporary menu.
Let the context menu unwind before external-link consent enters a native modal loop;
otherwise Cocoa can lose the menu's exit interruption while starting its alert. Recheck
the captured resource generation and context at dispatch. Menu deletion must not cancel
an admitted link, but edit, retarget, close and owner destruction must revoke it. Other
resource actions retain direct admission; controller confirmation remains synchronous.

The shared `ImageViewer::Mode::Overlay` covers the composite editor and supports fit, zoom,
pan, Escape, Close, and backdrop dismissal. Plain preview-image double-click opens it; linked
images retain link navigation. Restore focus only on explicit dismissal, preserve the source
and PDF camera, and disable insertion while viewing. Decode bounded retained image bytes
through the controller. A GIF clipboard file reference is allowed only if its current bytes
still match the captured image; otherwise retain encoded GIF MIME alongside decoded pixels.

Source/lifecycle changes revoke menus, pending image replies and overlays. Captured native
contexts plus host generation and buffer identity must remain valid through modal/asynchronous
callbacks. `NativeInteraction` is the sole owner of deferred composite retirement, shared by
insertion and resource menus/actions: never delete the borrowed source editor independently.
Keep menus parented to the native editor for logical focus, and defer retired-composite deletion
until owned menus/modals and callbacks have unwound. Native GUI coverage lives in
`test_typstviewwindow_insertion` (`resource*`) and `test_typsteditor`.

#### Detached-window close

Closing a detached title bar (including Alt+F4) closes its hosted tabs in tab order
through `ViewAreaController::closeViewWindow(id, false)`, not by reattaching them.
Save/Discard/Cancel, close hooks and buffer cleanup follow normal tab closure. The
first cancellation stops the sequence: earlier tabs stay closed, while the cancelled
and unprocessed tabs remain visible in the same detached window.

`DetachedWindow` ignores the native close event and emits an owner-mediated
`closeRequested`; its transient reentrancy guard resets after cancellation. Keep
`QPointer` lifetime guards across modal prompts and snapshot IDs, not raw view pointers.
Final-tab cleanup belongs to `ViewArea2::closeViewWindow`, including deferred encrypted
view destruction; an empty host-close snapshot must not eagerly delete its workspace.
Application quit remains separate: reattach all detached tabs before session save and
the quit-specific buffer-close path.

#### Image viewers

`ImageViewWindow` is a read-only `ViewWindow2`: it decodes the first oriented frame directly
from the protected-validated borrowed buffer bytes, never from text or a separately opened
file. Image MIME inference applies only after the saved suffix registry returns `Others`;
explicit mappings (including standalone SVG as Text) and external programs keep precedence.
Decoder failure clears the previous image and disables all camera controls until a good reload.

`FileTypeCoreService` exposes an application-level **Image** built-in when the saved registry
has none. Installed Qt decoder aliases supply its suffixes, excluding existing assignments.
List/name/suffix lookups agree and never write configuration; Settings → File Associations
persists the row only on Apply. A saved Image row, including empty/custom suffixes and display
metadata, remains authoritative. Image is never newable or searchable. Unassociated image MIME
files still use the viewer even when their decoder is unavailable, producing its explicit error.

`ImageViewer` owns the native camera shared by `ImageViewWindow` and the mind-map image overlay.
Mind-map plain left double-click activates only decoded images, including in read-only maps;
placeholders, resize handles and modified/group gestures never open the overlay. The m3 activation
signal carries an implicitly shared pixel snapshot, not a resource URL: the host must not fetch
or decode again. The overlay is a child covering the editor, never a separate window. It follows
editor resizes and uses a 90%-opaque theme backdrop. Camera controls sit at the top right,
immediately left of Close. Escape, Close or a backdrop click dismiss it; image clicks and
pans do not. Explicit dismissal restores the previous focus. Tab stays within the controls;
editing shortcuts and Ctrl+wheel never reach the covered map. Claim native QWindow keys as well
as widget keys so the first Escape closes the image, not Presentation Mode.

Tab hide, disable, reload, buffer replacement and protected locking destroy the overlay.
Viewing never changes map history, content or zoom; protected revocation destroys the decoded
pixels synchronously before cache refresh. Overlay transparency overrides only its own surfaces,
not toolbars or native image tabs, and follows live theme changes.

Mind-map image right-clicks use m3's borrowed `nodeImageContextMenuRequested` menu, not
canvas event filters. **View Image** and **Copy Image** consume `cachedImage()` pixels without
refetching; **Copy Image Address** copies the exact authored source, including relative paths
and data URIs. Pending/unavailable images disable pixel actions but retain address copying
and writable size editing. **Resize Image** uses the shared `ImageSizeDialog` and one native
undoable image-metadata update; empty dimensions restore automatic sizing. Read-only maps
retain view/copy actions. Bind actions to the clicked node and generation, never later selection.
Menus and resize dialogs revoke on content/read-only changes and overlay lifecycle teardown;
callbacks must not retain decoded protected images or use the editor after clipboard helpers
that can pump events. Coverage: `test_mindmapeditor` and m3's `images` case.

Native tabs and the mind-map/Markdown popups share centered, non-upscaling Fit/Reset,
1.25× zoom steps, manual bounds `[min(0.05, fit), 16]`, and bounded left-button panning.
Only fit mode refits on resize; manual mode preserves scale and the image-space center.
Camera changes never dirty the buffer and are not persisted. Overlay controls stay outside
image transforms. Qt's optimized viewport scrolling also moves child widgets: the native
view explicitly restores the toolbar position after scrolling. Keep its 32×32 hit targets,
4px spacing (including the gap before Close), and explicit theme-icon refresh. Native overlays
anchor Close 12px from the top/right; embedded image tabs retain the 12px bottom/right inset.

The Markdown popup uses intrinsic dimensions and generation-scoped image handlers; close, replacement,
and protected-SVG authorization callbacks cannot revive an obsolete image. Keep the existing
`protectedImageUrl()` path and safe no-op setup when exported HTML lacks modal markup.
Web labels come from `WebUtils::translationScript()`, not note HTML or inline event attributes.

Markdown **View Image** context actions in Read and Edit use this same web popup, never the
system application. Read uses the already-authorized media URL; protected Edit reads through
`readProtectedImage()` and retains the validated passive MIME/bytes, including animation.
Ordinary relative/local, remote and data image URLs retain their normal resource policy.

Image requests do not rerender the note just to display a resolved URL. Pending live-preview
updates are deferred until normal popup close or a still-live tab is shown/re-enabled; teardown
must not restart the preview timer. EditPreview keeps its current split and draft/undo history.

Native-requested overlays stay invisible until the image/error is ready. Visible pages await
image decoding; hidden Qt WebEngine pages cannot settle `decode()` until shown, so a loaded
image with valid intrinsic dimensions uses synchronous first-paint decoding instead. EditOnly
supplies a final CSS viewport hint (including page zoom) and fits before revealing the existing
preview. Pre-open resizes retain that hint; the native-open acknowledgement permits correction
against the actual viewport. Never expose an unfitted image when both viewport sizes are zero.
Hide the editor before showing the preview to avoid an intermediate half-width split; restore
its exact visibility, sizes and focus afterward, without reparenting or changing editing mode.

Adapter request IDs survive resets; deferred requests wait for channel readiness and queued
rendering. Bind through both channel-first and ready-first initialization paths. Close, Escape,
tab hide, theme/config reload and protected locking cancel stale requests before restoring or
replacing the view. Render image rebinding must not discard an unopened request, and stale
decode/acknowledgement callbacks must not publish it.

#### Native Markdown HTML handoff

Reader, EditPreview and WebEngine export use native cmark HTML, not a JavaScript Markdown
parser. Read captures buffer source without constructing an editor. EditPreview uses the live
draft's canonical UTF-8 snapshot and exact-current immutable editor tree when available; join
an already scheduled full parse, otherwise parse directly without editor projections.

`MarkdownViewerAdapter::setRenderedHtml()` publishes a fragment plus an opaque producer token
into the existing page shell. `DocumentRenderer` sanitizes and installs it, discovers code nodes,
and starts the existing Prism/math/diagram workers. Keep ordinary XSS policy and protected
inert-DOM/resource restrictions: native HTML is not trusted HTML. Emoji shortcodes stay literal;
existing cmark syntax, including wikilinks, is enabled in reader and export.
Protected notes retain the established fixed profile: HTML/linkify on, soft newlines unless the
source requests a hard break. Ordinary HTML/linkify/auto-break preferences do not override it;
the protected editor snapshot and reader use the same effective grammar.

With ordinary XSS protection enabled, js-xss filters tags, attributes and CSS before
`DocumentRenderer::sanitizeFragment()` parses its output into an inert template. Validate
browser-decoded `href`, `src`, `cite` and `poster` with the browser URL parser; remove invalid
or active URLs before connecting the fragment. Only `img[src]` accepts the existing passive
image data types. Keep allowed relative/file/custom URLs as authored, not resolved absolute
URLs. Install the checked nodes directly: never validate raw entity spellings or serialize
and reparse between validation and installation. The intermediate js-xss string is not safe
for live insertion. This is XSS protection, not a ban on ordinary remote images or CSS URLs.
Qt WebEngine URL/activation regressions live in `tests/widgets/test_markdownviewer_js.cpp`.

Enhancement rounds are serialized; only the latest pending fragment survives, including empty
HTML. Completion, task clicks, protected image replies and DOM extraction are generation-scoped.
Task writes additionally verify the authoritative source revision. Superseded or unready DOM
extraction returns unavailable, never a successful empty document. Eviction drops retained
native snapshots and cancels admission without changing source, undo or save state.

Retained buffer replacement keeps the same bounded native controller and its reference to the
stable base buffer member. Suspend/invalidate before releasing the old document; install the
incoming source/profile/mode, clear rebinding, then resume admission and explicitly render
Read/EditPreview. EditOnly does not request HTML. Native source/results and delayed actions also
check the base buffer generation, including identical-text replacements. Adapter resets always
retire pending HTML actions; reset epochs suppress stale side effects without stranding the
latest reentrant fragment. Guard external signal delivery with object lifetime checks.
Use loadingChanged status to distinguish failed navigation from an intentionally stopped old
load: an outgoing stop must not clear incoming page state or force it into EditOnly.

Source fenced-code highlighting uses native KSyntax, not the web code-highlighter backend;
web math highlighting and graph/math previews remain separate assistance jobs. After accepted
close, config/theme/mode notifications must not access deleted helpers or reopen admission.
The export page must inject the mandatory job bridge before configured scripts and its providers
after the workers, matching the reader even when persisted resource lists omit those scripts.

AST reuse removes render-side parsing, not DOM/layout or enhancement work. HTML bridge payloads
can exceed the Markdown input. Measure first paint and full completion separately; conversion
microbenchmarks are not end-to-end speedups. Existing image-maintenance scans and synchronous
editor layout/projection work are outside the native HTML job and can still dominate latency.

`MarkdownViewerAdapter` admits isolated Typst math and markup through the application-owned VTextEdit
runtime, never a per-view compiler process. `reader:<documentGeneration>` requests require
the current reader token; `preview:<editorScope>` requests are independent of reader content.
One native facade flight plus 32 waiting requests and 2 MiB queued UTF-8 bound admission.
Duplicate IDs cannot replace another completion owner. Scope cancellation retires ownership
before signals; the cancelled facade flight drains before another queue entry starts.
Document replacement retires readers only; reset, protected transition and destruction retire
all work. Missing/revoked authorization and unavailable runtimes fail explicitly. Canonical web
`#RRGGBBAA` is parsed by channel, not QColor's ARGB parser. Gate: `test_markdownwebjobs::typstAdapterBoundary`.

The Math renderer setting selects KaTeX (default), MathJax or Typst globally. Typst uses
Typst math within the existing dollar delimiters and mathjax fences; failures keep source
and diagnostics without LaTeX fallback. Protected notes honor Typst; other preferences use
bundled KaTeX, never external MathJax. Reader SVGs use
intrinsic em geometry and a top-relative baseline; previews pin pixels before the existing
SVG-to-PNG path. Ordinary HTML keeps self-contained SVG; raster exports and cross-copy retain
alignment. Existing protected export and clipboard restrictions are unchanged.

Fenced `typst` blocks always compile full Typst markup, independently of the Math renderer
preference. Each fence is self-contained, with compact transparent page defaults; authored page
settings and all pages are preserved in one merged SVG. The native `TypstFragmentRenderer` shares
one queue/lifecycle across math and markup. Files/packages/images remain unavailable; link
annotations become inert without dropping visible text. Failed blocks retain code and diagnostics.

The mandatory `typst.js` worker owns native transport/SVG normalization and a sequential block
pass; MathRenderer looks it up lazily for native math. Load it after configured dependencies and
before job providers in readers and exporters, including old resource lists. Protected templates
bundle it under the existing nonce/resource policy. `PreviewHelper::Typst` belongs to code-block
previews, not the math toggle. Scope release cancels both native kinds regardless of math setting.
Block export flattens its SVG when math or diagram rasterization is requested; block accessibility
metadata never embeds the complete source program. HTML and generic SVG cross-copy remain portable.

Protected Typst admission, dispatch and delivery require the profile's weak authorization
capability and original lease generation. Checks acquire/drop a current lease; no lease follows
native work. Bind before loading either the reader or per-note EditOnly host. Profile revocation
closes state before signalling adapters; profile/service destruction also retires Typst work. Protected
jobs never use the ordinary shared page. Sensitive native calls purge current shared memoization
on actual return, not logical cancellation; this is not a heap-erasure guarantee.

Protected in-place Typst (math, mathjax fences and markup) uses the existing native passive SVG-to-PNG
validator, without widening CSP or loading SVG data/blob images in Chromium. Preserve Typst paint
attributes, normalize 8-digit colors into separate fill/stroke/stop opacities for QtSvg, and drop
source-only title/description/accessibility metadata before rasterization. Other graph policy
is unchanged. Real encrypted/profile coverage: `test_webengineprofileservice::protectedTypstMath`.

Native SVG is parsed in detached XML before insertion. Normalize viewport-free glyph symbols
to groups (dropping symbol overflow otherwise clips glyphs), then use the unchanged protected
SVG filter with computed-style copying disabled. Prefix IDs/local references per request;
only then import into the page and add accessibility text. No unchecked live SVG staging.

Shared endpoint invalidation listens to published `themeChanged`, connected before view
subscriptions. Cancellation callbacks may recreate the shared endpoint immediately. Its
bootstrap must force a current-theme template snapshot (the cache key is editor revision
only) and sample the published palette, not MainWindow's later-updated base background.
This keeps EditOnly in-place math legible even when no reader exists to refresh templates.

#### Lazy Markdown reader and editor assistance

Initial EditOnly creates no tab reader or full-note read render. `MarkdownWebJobHost` supplies
an application-lifetime bootstrap for the shared ordinary runtime and owns only per-view
protected fallback widgets. `MarkdownWebJobController` owns routing; PreviewHelper and the editor
consume its typed results, never the document adapter's former assistance signals.

Treat `m_viewer == nullptr` as normal in EditOnly and in inactive/unavailable Read/EditPreview.
Visible reader demand resumes suspended resources before preparing the reader; an image-only
request loads an empty page rather than rerendering the note.
Print follows the window mode: Read prints the rendered web document asynchronously; EditOnly
and EditPreview print the editor document synchronously, without creating a reader. The print
dialog's Selection option follows the same target. Editor printing includes unsaved Markdown
source, not widget chrome or interactive preview overlays; encrypted notes remain unprintable.
A retained reader can assist an EditOnly editor until hidden-window inactivity suspends its jobs.
Mode changes and theme refresh must preserve drafts, cursor and undo history. Replay preview
caches when the effective renderer changes.
Snapshot editor zoom/DPR/reference width for jobs; math rasters keep intrinsic formula geometry,
including when an inactive tab has only provisional layout dimensions.

`editor.markdown_editor.inactiveReaderTimeoutMinutes` defaults to 5; 0 means Never and values
normalize to 0..1440. The searchable Read setting applies live. A single-shot `inactiveReaderTimer`
counts whole-window hidden time, not focus loss or preview-pane visibility. A changed timeout
restarts the full interval; visible split/detached panes remain loaded. Printing/mode switches
postpone eviction. Disabled or successfully closing windows stop their timer.

Eviction suspends job admission before cancelling work, drops preview rasters/DevTools and deletes
the reader, its adapter/channel/session, and any per-view protected profile/fallback. Shared ordinary
WebEngine resources and the native editor/document/undo remain. Never retain HTML, plaintext or
pixels as a new snapshot, save the buffer, or change native revision bookkeeping just to unload.

Resume only while visible, enabled, authorized and not closing. Rebuild Read/EditPreview from current
Markdown; EditOnly resumes assistance without a reader. Hidden theme/config/source/mode changes
must not recreate web resources. Preserve source line, zoom and splitter ratio; restore panes after
adapter readiness and source position after document rendering. Buffer the latest explicit open
request during reconstruction and replay it before passive position restoration, including the
production show-then-apply order. Missing-reader Find/word-count actions report unavailability;
passive highlight clearing does nothing. Guard retired-page callbacks by suspension and identity.

Protected fallback profiles are per-view immutable generations, never transferred into readers.
Beginning protected locking revokes jobs/resources immediately but keeps the native draft through
the save barrier; successful close then destroys editor/cache state even without a reader.
Coverage: `test_markdownwebjobs`, `test_markdownviewwindow_lazy` and protected-profile tests.

#### Read-only session recovery

`ViewArea2` saves each buffer's read-only state in workspace `bufferMetadata.readOnly`,
including before hiding a workspace. Preserve other metadata keys when updating hidden
workspaces. `ViewAreaController::openRestoredBuffer()` passes the saved flag to
`BufferService::getBufferHandle()` before constructing any editor. Adoption ORs it with
the notebook flag and never downgrades an already-read-only shared buffer. Missing keys
in older sessions default to false; `ViewWindowMode::Read` is not a read-only buffer flag.

#### Search replacement projection

SearchPanel2 keeps its form, action buttons and status inside a frameless, resizable
QScrollArea so content-mode fields and wrapped messages do not impose their combined
minimum height on the dock. Scrollbars appear as needed; focus changes reveal the active
field, including direct keyword focus when the dock is activated.

SearchPanel2 owns confirmation/progress/error dialogs; SearchController owns completed-result
eligibility, immutable selected/all targets, sequential replacement, cancellation and refresh.
LocationList2 emits value selections from `selectedRows(0)`; empty selection never means All.
Content keywords preserve whitespace, and replacement text is literal (empty text deletes).
Confirmation discloses saving existing unsaved edits and no cross-file Undo guarantee.

ViewWindow2 uses `m_contentReplacementFrozen`, separate from encryption conversion. Freeze
focus sync/close/external prompts without changing permanent read-only state. On changed content,
refresh each split's editor and Markdown preview before BufferService restores the active writer;
failed writes display the transformed text as unsaved rather than restoring stale editor text.

The replacement progress dialog stays application-modal until terminal completion, including
Cancel/Escape/window close while a write is pending. Disconnect QProgressDialog's built-in
`canceled()` -> `cancel()` connection with matching SIGNAL/SLOT syntax: the typed disconnect does
not remove Qt's legacy connection, and would hide the dialog before the write settles. Preserve
the replacement outcome while the captured original search refreshes.

#### Right-hand toolbar slots

`addRightCommonToolBarActions()` keeps primary controls on the toolbar and puts
secondary actions in a three-dot **Menu** (`menu.svg`):

| Position | What |
|---|---|
| spacer | |
| `addAdditionalRightToolBarActions()` | Markdown and Typst: Outline; PDF: sidebar, Outline, Find And Replace, Presentation Mode, page and zoom controls |
| Find And Replace | Default position; PDF opts out because Find is beside Outline |
| `addAdditionalViewToolBarActions()` | Text and Markdown: Presentation Mode; Typst: Presentation Mode, page and zoom controls; Mind Map: Presentation Mode, Layout, then zoom controls |
| Menu | Readable Width and Print, when `isPrintSupported()` |

`addAdditionalToolBarMenuAction()` lets PDF reuse its existing menu action; the
base creates one otherwise. Readable Width is first, followed by a separator and
PDF's existing rotate/cursor/scroll/spread/properties entries. Only viewer-owned entries are
readiness-gated: Menu and Readable Width remain available during loading/reloading.

`addAdditionalMenuActions()` appends Markdown's Image Host, Live Preview, In-Place Preview,
Conceal long link destinations, Conceal Markdown Markers, Allow Auto Section Number and Debug
after a separator.
Edit-only/Read-only visibility and encrypted-note restrictions still apply.
Use the `addAction(QMenu *, ...)` overload for menu actions: it preserves
window-scoped shortcuts without creating temporary toolbar buttons. The toolbar
overload owns the remaining direct controls. Theme refresh reaches both.

Text and Markdown add Lucide Undo and Redo after the left common actions, matching Mind Map.
They use the native text document history without adding shortcuts; unavailable history and
read-only editors disable the buttons. Markdown hides them in Read mode and binds them once
its editor is lazily created. History signals, mode changes and status changes refresh availability;
connections use the editor as their context so protected-editor teardown disconnects them.

Text keeps **Find And Replace → Presentation Mode → Menu**. Markdown keeps
**Outline → Find And Replace → Presentation Mode → Menu**. PDF keeps
**Outline → Find And Replace → Presentation Mode** together before page/zoom controls.
Mind Map adds Lucide Undo and Redo after the left common actions and keeps
Outline → Find And Replace → Presentation Mode → Layout → Zoom Out → percentage combo → Zoom In → Menu.
Typst keeps Outline → Find And Replace → Presentation Mode → page navigation →
Zoom Out → percentage combo → Zoom In → Menu on the right of the common spacer, matching PDF.

`ZoomToolBar` owns the shared Typst/mind-map controls: Fit Width, Fit Page, 100%, 125%,
150% and 200%, plus fractional input such as `137.5` or `137.5%`. Localized fit labels
also work. The native editors retain geometry and limits (manual mind-map zoom is
10%–400%; Typst is 1%–6400%). Fit Width may overflow vertically; Fit Page fits the
whole map or the current PDF page. Custom values never add preset rows.
`ViewWindowToolBarHelper2::addZoomToolBar()` supplies themed icons; the pure control
retains their `iconName` properties for runtime theme refresh.

`ZoomToolBar` and `PdfViewerToolBar` (after `install()`) expose
`setShortcutTips(zoomIn, zoomOut)` with `QKeySequence` arguments. Tips use native key names;
empty sequences remove them. This API only changes button tooltips, never action labels,
zoom state or shortcut registration. Mind Map, Typst and PDF supply Ctrl+= / Ctrl+-.
Typst uses the same unqualified hints in Read and Edit with Live Preview; disabling Live
Preview in Edit still hides the controls. Existing keyboard behavior is unchanged: Typst
Edit-mode shortcuts change the source font, while Read-mode shortcuts change the PDF zoom.
Zoom dropdowns show only **Zoom**, with no Ctrl+0 hint because they are not reset buttons.
`ImageViewer` retains `setShortcutTips(zoomIn, zoomOut, reset)` for its three actual buttons.
Image tabs do not install zoom shortcuts, and image overlays suppress them, so both leave
shortcut tips unset by default.

Enter is consumed once before QComboBox can deliver it twice. An unchanged field never
reapplies its truncated display. Focus loss discards drafts before Qt can activate a
matching preset. Display synchronization uses cached camera state, including during
editor teardown. Manual display truncates fractional percentages, correcting floating-point
noise at whole percentages; the native factor and session value keep full precision.
Gate: `zoomToolbarPreservesFractionalInputAndDiscardsDrafts` in
`tests/widgets/test_mindmapeditor.cpp`, using the real native camera and session config.

The views observe native camera notifications, including context menus and Ctrl+wheel.
`session.json` stores `editor.mindMapZoomFactor` and `editor.typstZoomFactor`: positive
values are manual factors, zero is Fit Page, and -1 is Fit Width. Missing keys default
to 100%; existing mind-map zero values retain their whole-map fit. Restore before
accepting constructor/load notifications so defaults cannot overwrite remembered choices.
Typst preview zoom is separate from the existing source-font zoom preference.

Mind-map fits remain one-shot, including deferred hidden-window fits; manual zoom cancels
pending placement and later resizes preserve scale. Its empty-canvas menu orders Focus
Main Node → Fit Width → Fit Page → 100% → Zoom In → Zoom Out. Typst retains active fit
through resize, publication and Read/Edit transitions. Its page/zoom controls are visible
in Read or Live Preview and disabled before PDF publication. Invalid mind maps disable
zoom; read-only maps remain zoomable. The native ViewWindow toolbar
borrows the editor's editing command actions; the editor retains shortcuts,
inline-edit commits and history availability. Invalid content gates native mutations through read-only mode until
a successful reload restores the buffer policy. Widget-hosted Settings and Dashboard
retain their content-owned toolbars; locked-note placeholders have none.

The mind-map **Layout** action owns a menu with m3's **Balanced**, **Right**, **Left** and
**Outline** modes (Outline is the indented tree view). Keep it immediately after Presentation
Mode, before zoom controls, and reachable through toolbar overflow. Its exclusive checks follow `layoutDirectionChanged`
and the actual editor state after a rejected command; do not block QAction `changed` signals,
which maintain QActionGroup's current member. Accept an active inline draft before switching,
and reject application to a replacement buffer. Layout changes alone do not dirty the document,
consume undo history or reset zoom/selection. An actual change centers the first selected node,
or the main node when the selection is empty or link-only; group/link selection is retained.
Successful menu choices return keyboard focus to the native canvas. Native API calls retain
caller-owned focus and inline drafts; unchanged or rejected layouts do not recenter.
Read-only maps remain switchable; unloaded or
invalid content disables both the button and its menu actions. The button uses Lucide's
`layout_dashboard_editor.svg`. An explicit successful menu choice also updates
`editor.mindMapEditor.defaultLayoutDirection` in `vnotex.json`, not `session.json`, even when
that choice is already active. New/reopened editors use this default; existing maps retain
their local mode. Startup, API changes and rejected switches never overwrite the preference.
The stable tokens and fallback are documented in [core preferences](../core/AGENTS.md#mind-map-editor-preferences).

#### Mind-map printing

Print is available in the toolbar Menu, not in the empty-canvas context menu.
`MindMapViewWindow2` owns the print dialog/printer; m3's public `renderMap()` paints a detached,
unselected scene. Fit the current visible map, including orphan trees and links, centered on one
printable page without changing zoom, pan or folds. Printing uses the current palette and cached
images (pending/unavailable resources retain their visible placeholders), not a viewport screenshot
or the HTML export's node articles. Do not offer selection-only printing.

Accept the current inline draft before painting, but do not save or reset history. Reject failed
commits, cancelled printers, unloaded/invalid/disabled views and encrypted notes; read-only maps
remain printable. Revalidate window lifetime and buffer generation across the dialog, draft commit
and printer activation. Page ranges excluding page 1 produce no output; copies remain printer-owned.
Coverage: `test_mindmapviewwindow_print` checks vector PDF text, images, layout/fold/camera invariants,
drafts, replacement rejection and protected-note restrictions.

#### Mind-map resource insertion

`MindMapEditor` uses m3's MIME hooks, not canvas event filters. A single source updates
its captured selected/drop-hit node; a URL batch prepares complete child records and
calls native `addNodes()` once. URL lists take precedence over clipboard thumbnails.
`SelectDialog` owns quick choices; shared `ImageInsertDialog::setMindMapMode()` and
`LinkInsertDialog::setUrlOnly()` own acquisition. Hyperlinks never rename a topic.

Insertion captures buffer generation, operation serial, target ID and complete target
record after accepting the initiating draft. Revalidate after modal UI and IO/hooks;
selection changes never retarget an open operation. Locking, read-only, disabling,
reload and buffer replacement reject the active dialog. Roll back only resources made
by an uncommitted operation through its captured `Buffer2`; committed resources survive
Undo/Redo. The controller validates passive images up to 32 MiB and owns acquisition,
local asset/attachment preparation and encoded relative URLs. Protected display and
link-opening restrictions remain independent from explicit user-authorized acquisition.

Insert Link/Image toolbar actions use the adapter's `canInsert()` and
`insertionAvailabilityChanged()`, not m3's cross-link `addLink` command. Keep these
QActions in the toolbar so narrow-width overflow and theme refresh retain them.

#### Whole-view fullscreen

`ViewWindow2::setViewFullScreen(bool)` promotes the **existing ViewWindow** to
frameless fullscreen in place. `ContentFullScreenHost` changes its window flags,
not its parent or tab registration. Toolbar, content, Find, banners and scoped
shortcuts keep their normal layout; no container, placeholder or floating exit
button is created. Readable-width margins are zero during presentation.
PDF entry defers its adapter commands until native layout settles, activates the
layout, then sets page-scroll before Page Fit. The webview's remaining height
already excludes toolbar/Find rows; never subtract a fixed toolbar height.
A single-shot timer also coalesces native PDF viewport resizes while presenting
with Page Fit selected, including Find/banner changes. Explicit zoom selections
are preserved; every confirmed exit cancels the pending fit before restoring
normal zoom/scroll state.

The existing parent layout is disabled before promotion so QStackedLayout cannot
resize the fullscreen page during a background layout pass. Exit restores the
exact saved flags/state, QuitOnClose attribute and layout enabled state. An
originally disabled layout stays disabled. Normal exits restore visibility/focus;
external tab hides do not reopen or focus the hidden page. Ownership transfers
restore under the current parent rather than moving the view back.

- **Escape is filtered at application level**, including ShortcutOverride, to
  reach Chromium's deeply nested render widget. Only the promoted window's
  widgets/native target qualify, not separate child dialogs. An owned popup
  consumes the first Escape; the next Escape requests presentation exit.
- **Exit is an intent:** Escape, native Close, external hide and removal of native
  fullscreen emit `exitRequested` / `viewFullScreenExitRequested`. Native Close
  exits presentation instead of hiding/deleting a registered document tab.
  `viewFullScreenChanged` reports only confirmed transitions so every exit path
  restores the PDF zoom/scroll snapshot and toolbar state.
- **Restore before structural changes:** conversion/replacement freeze,
  `setCentralWidget()`, `aboutToClose()` and teardown leave fullscreen first.
  Text/PDF/Mind Map teardown exits while derived state and any adapter still exist. Guarded
  pointers and destruction handling restore surviving parent layouts without
  touching a dying widget.

Markdown, Text, PDF and Mind Map have a checkable **Presentation Mode** action: checked while presenting,
click again to exit. Its view-scoped shortcut defaults to **F9**, configured through
`editor.core.shortcuts.PresentationMode`; an empty string disables the shortcut.
PDF's action remains usable even if the viewer reloads. Mind Map presents the existing
editor without reloading content, changing read-only policy or resetting zoom.
Each confirmed mind map presentation entry collapses the embedded node-properties card through
`MindMapEditor::collapseNodeProperties()`, including with no selected node. Users may
expand it again while presenting; exit leaves their current card preference unchanged.
Separated node properties stay expanded in the Tools dock; presentation does not collapse them.
Only the QToolBar gets an opacity effect: **10% inactive, 100% active**. Active
means the presentation window is active and the toolbar is hovered, contains
keyboard focus, or owns an open popup (including submenus, zoom and extension
menus). The effect includes the toolbar's styled background, not only its buttons.
`PresentationToolBarEffect` shares this policy between Markdown, text, PDF and Mind Map. During presentation,
the views paint the parent backdrop with the theme's `base#content#bg`, so a matching
toolbar-colored parent cannot mask the fade.
Theme changes refresh that color; normal mode restores normal frame painting.
PDF, Find and popup windows remain opaque. Exit removes the effect,
application filter and focus tracking. Gates: `test_contentfullscreenhost` and
`test_pdfviewertoolbar`, including tab identity and rendered-alpha regressions.

#### Markdown slides

Markdown Presentation Mode switches Edit to Read through normal buffer synchronization, then
waits for the current native-render generation's document completion before requesting the deck.
Bridge readiness alone is insufficient: initial HTML delivery cancels an earlier deck request.
Both page readiness and validated document completion wake a pending request; reveal.js readiness
then confirms native fullscreen. F9 toggles; Escape exits. Rendering, page reload, mode/theme
changes and export tear the deck down first.
`presentation.js` moves rendered nodes reversibly; it never reparses or rewrites note source.
A sole leading H1 opens a title slide with its introductory content. H2 and H3 start slides;
H3 slides repeat their nearest preceding H2 in a fixed header above the scrollable body.
Preamble is retained, H4–H6 stay in-flow, and additional H1s reset the H2 association without
splitting. Headings inside atomic lists, tables and blockquotes are not slide boundaries.

Viewer fold wrappers are traversed without losing their state. Every remaining nested SECTION
(including display math) becomes a temporary DIV: reveal.js treats all descendant SECTIONs as
slides. Exit restores original node objects, wrappers, folding, styles and scroll positions.
The local core and shared black-theme fonts/layout are viewer-only, media-gated styles;
exported reader CSS excludes them. Each entry selects the black or white reveal.js palette from
the current VNote content background (`base#content#bg`, backed by system Base for Native,
with `getBaseBackground()` as fallback for custom themes); Qt `qGray < 128` selects dark.
The adapter passes that mode explicitly, including for protected previews. The white palette is
a small scoped CSS override, not a duplicate font bundle. Code surfaces and scrollbar color-scheme follow it;
the light class is restored with the reader's other classes on exit. Theme changes still end
presentation; the next entry picks the new palette. Protected previews retain their explicit
bundled-resource allowlist and unchanged CSP. JavaScript must never request HTML5 fullscreen.

Protected Markdown reading/live previews use the selected theme from the shipped RCC, with
its bundled palette resolved before CSS is served. Never load the editable installed theme,
custom CSS or palette. Custom themes use bundled `vscode-dark`/`pure` according to the same
content-background darkness rule; Native dark uses `vscode-dark`, while Native light keeps
its literal bundled reader CSS. Theme changes replace only the viewer with a fresh token,
nonce and off-the-record profile. Publish the replacement before revoking the old profile,
destroy old pages before their profile, and retain the editor, draft and undo history.
Restore saved splitter sizes after viewer readiness, which otherwise redistributes live-preview panes.
Discard pending sizes on read/edit or edit-view-mode changes so hidden-pane sizes cannot collapse a new layout.
Edit-mode external syntax highlighting parses the same template-owned bundled `highlight.css`
through the existing adapter; encrypted notes must never read the installed/custom stylesheet.
Install the per-editor code/math palette after viewer readiness, including a lazily created editor.
Theme changes reset stylesheet state before reload; generation and adapter checks reject obsolete
CSS callbacks. Installing changed colors invalidates vtextedit's source-text and applied-format
caches and rehighlights unchanged blocks without replacing text, selection or undo history.
Ordinary editing retains the same cache keys and lookups; no per-block theme hashing is needed.

**HTML5 fullscreen remains disabled.** Enabling
`QWebEngineSettings::FullScreenSupportEnabled` advertises it to every web view
without implementing their transitions, and Chromium requires transient renderer
user activation that a Qt QAction relayed over QWebChannel does not carry.
`PdfViewWindow2::setPresentationMode()` drives the native view transition instead.

**The same announce-only trap applies to printing**, and there it is worse:
`window.print()` in a page only reaches `QWebEngineView::printRequested`, and for
a pdf.js viewer `QWebEngineView::print()` prints only the pages Chromium has
rendered. `ViewWindow2::isPrintSupported()` (default **true**) is the opt-out
hook that keeps a window from inheriting a Print button its `handlePrint()`
cannot honour; `PdfViewWindow2` is the current opt-out. See
[pdf.js AGENTS.md § Printing and presentation mode](../data/extra/web/pdf.js/AGENTS.md)
for why neither print route works there.

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
  pdf.js's own toolbar strip is hidden; its controls are reproduced by
  `PdfViewerToolBar` (`pdfviewertoolbar.{h,cpp}`) on the view-window toolbar.
  **`PdfViewerAdapter` is the single source of truth** — the toolbar repaints
  from `viewerStateChanged`, never optimistically from its own clicks, because
  the page changes page/zoom/rotation/modes by itself. See
  [pdf.js AGENTS.md § The viewer-control bridge](../data/extra/web/pdf.js/AGENTS.md).
- `MindMapViewWindow2` — mind map viewer
- `WidgetViewWindow2` — generic widget-hosting window

`MindMapEditor::loadContent()` delegates blank-input initialization to m3's `loadJson()`.
Empty or JSON-whitespace-only files open with the selected **Central topic** root, not a
blank canvas. Loading leaves the buffer clean and file bytes unchanged; edits/save persist
the initialized map normally. Invalid nonblank input still fails closed.

`SettingsWidget` adds a right-aligned, text-only **Edit JSON** toolbar action. Resolve
`vnotex.json` through `ConfigMgr2::getFileFromConfigFolder()` and open it with `BufferService`
in forced edit mode; do not launch an external editor or depend on a `MainWindow2` pointer.
Configuration persistence remains owned by `ConfigMgr2`; this action only opens the file.

### Mind map editor settings

**Separate node properties panel** defaults on in Settings → Editor → Mind Map Editor.
Each editor owns exactly one properties widget: either hosted by Tools with no canvas toggle,
or embedded as an initially collapsed card with its expand/collapse toggle. Settings Apply
relocates that same widget live through the coalesced editor-config hook, including hidden
main-area maps. Detached maps always use the embedded card, regardless of this preference.
Returning embedded restores the card's expansion preference; documents and inline drafts are
not reloaded or committed. `MindMapViewWindow2` bridges m3's placement handoff to
`ViewWindow2::toolsPanelAboutToChange()` / `toolsPanelChanged()`. Only the current view is
subscribed by `MainWindow2`: release `ToolsPanelHost` synchronously before native reparenting,
then refresh the dock after the new accessor is ready. Background maps must not release or
steal the current dock. Never duplicate the panel. m3 gates external edits by editor enabled/read-only
policy and destroys the surviving panel before its controller/view, regardless of visual parent.
For focus transitions outside the ViewWindow subtree, Mind Map reuses the base writer/revision
hooks so editing Tools before focusing the canvas still saves correctly. Canvas transitions keep
the existing base handling; logical writer activation never moves keyboard focus from the dock.

Settings → Editor → **Mind Map Editor** exposes default fallback font families, point size,
undo limit, deletion confirmation, automatic colors for new main branches and automatic section
numbering (enabled by default). Numbering reuses Editor's **Section Number Pattern**, excludes
main/orphan roots and restarts below each root; it changes labels, not saved topics. Apply saves
`editor.mindMapEditor` and uses `EditorPage::notifyEditorConfigChange()`'s existing one-second
coalesced hook; editing controls alone changes nothing, and Reset reloads saved values.
The hook reaches all registered maps, including hidden/detached windows. The wrapper's
`applyConfig()` updates live policy and style without reloading documents or shortcut overrides;
the view passes `!isDetached()` to gate separate placement. `ViewSplit2::addViewWindow()` sets
the explicit detached state before showing or publishing the tab. Detaching and direct detached
opens embed the panel without changing global config; reattaching follows the current preference.
Parentless workspace parking and presentation do not change that hosting state.
Preserve inline drafts, selection, zoom, dirty state and per-node pixel-size overrides.
Theme changes still supply the host font/palette; explicit map defaults survive them.
Shortcuts are JSON-only, loaded on restart. Zoom remains ViewWindow-owned, and resource bases
and relative-URL resolution remain buffer/encryption-owned, never user preferences.


### Automatic section numbers

Markdown windows expose **Allow Auto Section Number** in **Menu**, immediately after the
concealment controls.
The default-on permission lives only in the window's `OutlineProvider`, never in configuration or
outline snapshots. OFF vetoes read decoration, edit-mode source numbering, and dock/popup outline
numbering; ON respects each existing global preference. Settings changes, mode changes and new
outline snapshots must not reset it. Explicitly switching this menu permission OFF also restores
only source prefixes changed by VNote in the current editor session, as one standalone undo step.
Overwritten manual prefixes are restored; already-matching/manual prefixes remain untouched.
Ownership is not inferred on reopen, or transferred through replacement/cut-paste/reorder of
tracked spans. Exact undo checkpoints can restore their previous lineage. Global preference
changes, provider clearing and temporary mode suspension remain nondestructive.

Restoration requires a writable, stable, current editor and must not overwrite another split's
unsaved content. Without an existing editor it does nothing. Unchanged restoration creates no
source edit, dirty state or revision/cache update; unavailable restoration shows guidance and
leaves the permission OFF so the user can re-toggle to retry. Read mode uses the same hidden
document, commits its encoded content and updates the viewer text in place, preserving undo
history and the top line rather than resetting the page.

Markdown read-mode numbering lives in `src/data/extra/web/js/sectionnumber.js`, not the parser.
`MarkdownViewerAdapter::sectionNumberOptions` retains per-view enablement and the shared
`EditorConfig::sectionNumberPattern` across page resets. Edit previews disable decoration;
mode and settings changes update the property even when the content revision is unchanged.
The worker runs before heading extraction and defers completion until the mapper has published.
It owns only its prefix spans, preserving source text, inline nodes and heading anchors.
The required script is appended after configured viewer dependencies by `HtmlTemplateService`;
older custom resource arrays must not omit it. Export resources do not include it.

The outline's independent Auto Section Number toggle uses `SectionNumberUtils` with the same
policy: exempt a sole leading H1, inspect up to five remaining actual headings for numeric
prefixes, and otherwise number from the minimum eligible level. `Outline::m_hasSectionNumber`
is an authoritative producer guarantee, not a preference; true bypasses panel detection and
numbering. `Heading::m_isPlaceholder` marks only synthesized level gaps: never infer it from
`[EMPTY]`, an empty anchor, or a missing PDF destination. Preserve both flags across snapshots.
The outline controller refreshes the shared pattern on `ConfigEditorChanged`, including open
popup outlines, without mutating producer names.

### Markdown concealment

Markdown's top-right **Menu** exposes two Edit-only checkable preferences immediately below
**In-Place Preview**; neither appears on the Settings page:

- **Conceal long link destinations** retains `editor.markdown_editor.concealment`, default `true`.
- **Conceal Markdown Markers** stores `editor.markdown_editor.concealMarkdownMarkers`, default
  `true`. A saved `false` remains off. It hides emphasis, strong, strikethrough, mark, inline-code,
  link, ATX-heading and inline-formula syntax through vtextedit's typed `MarkdownConcealHideSyntax`
  rules.

Both preferences are global and persist independently through `ConfigMgr2`; moving the original
control must preserve saved opt-outs. Menu actions call `MarkdownEditorController`, which emits
the existing `ConfigEditorChanged` notification to refresh open editors. Each menu refreshes its
checks from current configuration before opening, without emitting another toggle.
`applyMarkdownConfigFields()` supplies both editor-config builders: destination opt-out clears
URL rules before independently adding any enabled marker rules. Link-syntax hiding includes the
link destination suffix; image/reference URL shortening remains separately controlled.

Concealment is visual only: never rewrite Markdown or change dirty state, cursor or undo history.
Hover reveals a shortened destination; moving the caret into an element reveals its grouped
markers. Setext underlines and multiline math-block delimiters remain unchanged.

Every bundled `text-editor.theme` defines `markdown-editor-styles.ConcealedText` with foreground
and background colors. `ThemeService` resolves palette tokens before vtextedit loads the theme.

### PlantUML pagination

`src/data/extra/web/js/plantuml.js` owns page enumeration for both read mode and editor previews.
Read mode keeps separate, individually viewable images; previews stack PNG pages vertically into
one image, preserving page titles and pixels. Do not inject `ignore newpage`: it is invalid for
non-sequence diagrams. The note source is never rewritten.

Web sequence diagrams use indexed SVG/PNG requests. Other identified diagram types retain
single-image web rendering because some servers ignore their index. HTTP 400 without an image
marks the end; the legacy HTTP 509 sequence-title index exception is matched narrowly, not as a
generic server-error fallback. Local rendering uses `-pipeimageindex N` and successful empty
stdout as the end marker; custom PlantUML commands must accept the appended index argument.
`GraphHelper` carries success separately from image data and includes the image index in its
cache key, including cached empty end markers.

SVG responses are parsed as inert XML and validated before joining the page list; the SVG list
contains SVG elements, while PNG lists contain base64 strings. Never parse renderer responses as
HTML: a failed HTML response can execute image handlers even in a detached fragment. Raster-only
backends such as Ditaa may label PNG bytes as SVG; reject that mismatch locally and recommend PNG.
On failure without valid pages, preserve the original code subtree and annotate it with an error
tooltip. Do not rewrite its text or discard its highlighting. Failure to display partial results
must also remain local and settle the node exactly once.

Enumeration is bounded to 256 pages. Failures or the cap retain available read-mode pages with
an incomplete-render warning; editor previews do not silently publish partial results. Protected
notes remain blocked. `test_graphrenderer_js.cpp` covers web termination, error retention, the cap,
and preview pixels in QtWebEngine, since QJSEngine does not support async/await.

### Math font readiness

`MathRenderer.waitForMathFonts()` first measures the math node so its used web fonts begin loading,
then waits only for loading `KaTeX_*` faces through their `FontFace.loaded` promises. The raster
path measures again after those fonts settle. Read rendering, in-place rasterization and raster
export all use this boundary; they must not await `document.fonts.ready`. That document-wide
promise includes unrelated fonts and post-layout work, which can remain pending indefinitely in
the hidden Edit Only viewer. Do not replace this with a delay, a visibility toggle, or a blanket
prefetch of unused fonts. The native hidden-page regression in `test_markdownviewer_js.cpp` holds
global readiness and an unrelated font pending while requiring math to wait for its own font and
then finish reading, PNG preview and export.

### Markdown renderer lifetime

`DocumentRenderer.render()` emits `markdownContentWillChange(container)` before replacing content,
including plain-text, empty and protected renders. MathJax cleanup belongs at this boundary,
not at `fullMarkdownRendered`: current formulas must retain their menus and accessibility state.
`MathRenderer` clears the reader's MathItems while their old ancestors are still attached, then
releases its last target/output references and local SVG font cache. It never initializes math
just to clear content, touches editor preview scopes, or resets the reader's TeX declarations.
KaTeX has no corresponding retained document; its rendering and font-readiness paths are unchanged.

MathJax 3's context menu must be unposted only when posted. Clearing bindings alone retains its
selection, anchor, store focus and bound-handler table; retire those references too. The small
version-specific cleanup is isolated in `MathRenderer.clearReadingMath()` and covered with the
real pinned MathJax engine in `test_markdownwebjobs`, including menu use before replacement.
Cleanup failures are logged without stranding the Markdown worker's completion count.

`Graphviz.pendingRender` is an ordering/recovery barrier, not a result cache. Its settled value
must not retain an SVG/IMG or the detached section containing it; callers receive the separate
result promise. Keep recovery before the next queued render. `test_graphrenderer_js` covers
read/preview results and SVG/PNG failure recovery. Do not add unconditional post-render GC.

### Toolbar

- `ToolbarHelper2` — main window toolbar construction
- `ViewWindowToolbarHelper2` — per-view-window toolbar construction

Theme changes must retire `PreviewHelper`'s cached math/diagram rasters before applying editor
styles and reloading the viewer. Re-request previews through `MarkdownEditor::refreshPreviewHighlight()`
when the new viewer reports ready, as for renderer-setting changes. `PreviewMgr::refreshPreview()`
alone does not invalidate `PreviewHelper`'s renderer-dependent cache.

### Dialogs (`dialogs/`)

- `NewNoteDialog2`, `NewFolderDialog2`, `NewNotebookDialog2`
- `ManageNotebooksDialog2`, `ImportFolderDialog2`

Each dialog is driven by a corresponding controller in `../controllers/`.

`NewNotebookDialog2` and `ManageNotebooksDialog2` share `NotebookInfoWidget`. The form owns
field layout and editability; dialogs/controllers retain creation, sync, validation and persistence.
Root and type are immutable after creation. Sync method sits below Type, editable only during
creation; management shows selectable read-only text and hides Configure. Unsupported fields stay
visible but non-editable; read-only text remains selectable. `setNotebookInfo()` populates or resets
fields without emitting `inputEdited()`, so loading a notebook never dirties the management dialog.
Creation evaluates name snippets; editing an existing notebook preserves its literal name.
New-notebook sync failures display both `SyncErrorPresenter` guidance and nonempty backend
details in the existing plain-text information area; details must not be confined to debug logs.

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

#### Edit mode (unlocked): drag-move / drag-resize

Locked is the default and is unchanged: no overlay widget exists at all. **Unlocked
means edit mode**, and that has four normative consequences:

- Each realized sticker gets a `StickerDragOverlay` child of its chrome frame,
  covering everything **below the header band** (`ViewItem::m_header`, handed
  back by `buildFrame`). It **swallows every mouse event over the sticker
  content** — clicking a calendar date or a history row while unlocked does
  nothing — while the header stays live. The header holds **only Remove**: the
  Move menu (Move Up/Down/Left/Right + `Resize...`) and `ResizeStickerDialog`
  were deleted, because direct manipulation plus the overlay's keyboard route
  replaces both. `moveSticker()` on the controller is still the commit path for
  a keyboard move.
  The overlay is created on unlock and `hide()` + `deleteLater()`d on lock, in
  that order (a queued deletion would otherwise leave a swallowing overlay alive
  across a fast lock/unlock).
- **The controller is the single source of truth for normalization.**
  `DashboardController::previewStickerGeometry` applies the same `qBound` clamps
  `setStickerGeometry` applies (the setter now calls it), so the ghost
  (`StickerDropIndicator`) always shows exactly what a commit would produce.
  Out-of-bounds drops are **clamped**; drops onto an occupied region are
  **rejected** (ghost renders invalid, release is a no-op — the existing
  reject-and-noop collision policy). `stickerDragTarget()` in
  `stickerdraggeometry.{h,cpp}` is pure math and deliberately does **not** clamp
  to board bounds; do not add board knowledge there.
- Geometry is committed **once, on release** — exactly one `setStickerGeometry`
  (and therefore one persist) per gesture. `DashboardBoard::cancelDragSession()`
  is the single synchronous, idempotent teardown; it must be called from lock,
  sticker removal, layout reload / `clearAllViews`, `dragCancelled` (Esc or a
  right-click mid-drag) and the destructor.
- **The overlay is also the keyboard route, and must stay one.** It is
  focusable, so Tab reaches every unlocked sticker; arrow keys emit
  `moveRequested` and Shift+arrows `resizeRequested`, which the board commits
  through `moveSticker` / `setStickerGeometry` — the same clamp and collision
  policy as a drag, no ghost. A focused overlay paints a solid border instead of
  a dashed one, and carries an accessible name (the sticker title) plus a
  description of the keys. Direct manipulation removed the header Move menu and
  the resize dialog, which were the only keyboard-operable affordances; do not
  remove this replacement without providing another one. Keyboard input is inert
  while a mouse session owns the geometry.

`StickerDragOverlay` and `StickerDropIndicator` are leaf presentational widgets:
**no `ServiceLocator`** (precedent `InlineBanner`). The board resolves the accent
(`base#info#fg`) and invalid (`base#error#fg`) colors from `ThemeService` with a
`QPalette` fallback and injects them; both widgets paint with `QPainter` and
never touch a stylesheet, so nothing here can trip (or need to dodge) the
hardcoded-color gate. The centre move affordance is the themed `move.svg` icon,
tinted with the accent and injected the same way (`setMoveIcon`) — the overlay
cannot resolve a theme icon itself. `DashboardBoard::refreshEditModeColors()`
re-injects both on a theme change; without it live overlays keep the old accent.

While a drag targets rows below the occupied extent, the board reserves them via
`m_dragRowReservation` and re-runs `applyRowSizing()`, otherwise the container
has no height there and the ghost is clipped. The reservation only extends
`maxRow`; per-row height stays `kRowUnitHeight` so frames keep matching their
cells exactly.

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
- `NotificationButton2` (`QToolButton`) lives on the settings toolbar immediately after the Settings menu. It paints a red badge with `NotificationService::activeCount()` and refreshes its bell icon on `ThemeService::themeChanged`. It **does not auto-show the popup** — `showPopup()` is called on click, or by `MainWindow2` forwarding `NotificationToast::popupRequested`.
- `NotificationPopup2` (extends `ButtonPopup`) is the click-to-open **notification centre**: messages newest-first with severity icon + title + text + optional collapsible "Details" + optional progress bar + per-message action buttons + Dismiss, in a height-capped `QScrollArea`, under the shared `TitleBar` holding "Notifications" and Clear All. Do NOT hand-roll a titlebar.

Toast sizing chooses the width within the anchor first, then uses the polished layout's
`totalHeightForWidth()` so wrapped text and styled action buttons keep their full height.
Visible toasts remeasure after `LayoutRequest` (including font/theme changes); retired actions
are hidden before deferred deletion. Keep this independent of startup delays. Regression:
`test_notificationtoast::test_wrappedOfferFitsAfterLayoutChanges`.

The popup list has a minimum height of 12 font line spacings, clamped to its screen-height cap.
Keep this floor on the scroll area so the `QWidgetAction` size hint reserves usable list space.

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
(range `0..1000`, clamped). Permille rather than percent, so a long-running producer with a
byte count still moves the bar smoothly.

The popup rebuilds on `messageAdded` / `messageUpdated` / `messageDismissed` /
`messageRemoved` / `messagesCleared` / `themeChanged`, but **only when it is already
visible**. `messageAdded` is in that list deliberately: nothing auto-pops any more, so
without it an open popup would go stale.

`NotificationAction::m_dismissOnTrigger` (default `true`) controls whether triggering the
action dismisses the message. Producers that keep updating one message in place set it
`false`.

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
- The callback AND its `m_dismissOnTrigger` flag are snapshotted in that **same** lookup, before the callback runs. Do NOT re-resolve the action index afterwards: a callback may synchronously replace the action vector, so a second lookup would read a different action's flag.
- Because a callback may synchronously destroy the widget, every post-callback access to `this`/`m_services` is guarded with a `QPointer`. The callback may also have already triggered a rebuild via `messageUpdated`, so nothing after it may touch the row widgets that existed when the lambda started.

`Duration` controls only auto-hide (`Short` 3 s, `Medium` 10 s, `Long` 60 s, `Persist` = capped at 30 s **on the toast only**, so a toast is never permanently stuck); it does NOT affect memory retention. Messages stay in the in-memory list until dismissed, cleared, or evicted by the retention cap.



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
`tests/gui/test_themeservice.cpp` cover this for all 12 themes —
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

**Never put a literal color in a `setStyleSheet()` call.** VNote ships 12
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
  palette (`marknodedialog2.cpp`), the notebook avatar colors
  (`notebookselector2.cpp`), and `CommentColorSwatch`
  ([`../gui/AGENTS.md § CommentColorSwatch`](../gui/AGENTS.md#commentcolorswatch)),
  which paints the comment-colour chip with `QPainter` and offers colors *to*
  the user.
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

## Every Palette Token Must Exist in Every Theme

`ThemeService::paletteColor()` returns an **empty string** for a token no theme
defines — it only `qWarning`s. Interpolated into a stylesheet, Qt then drops the
declaration; handed to `QColor`, the color is invalid.

`tests/core/test_theme.cpp::testCppPaletteTokensDefined` scans `src/` for every
string literal shaped like a palette path (`base#…`, `widgets#…`, `palette#…`)
and fails the build if any bundled theme does not resolve it. It is shape-based,
so bare `"..."` literals, `QStringLiteral(...)` and file-scope `c_*` constants
are all covered. Its sibling `testInterfaceQssFullyResolved` does the same for
tokens named in `interface.qss`; both share one resolver so they cannot drift.

Adding a token means adding it to **all** themes under
`src/data/extra/themes/`. Prefer an existing role — `base#normal#border`, not a
new `base#border`. `native` is the smallest palette (no `base.content`, no
`base.hover`); a copy-paste from `pure` will produce dangling refs the gate
catches. Themes that declare `"backfill-system-palette": true` may leave
`@palette#active#<role>` / `@palette#disabled#<role>` leaves to
`ThemeUtils::backfillSystemPalette`, but only for the roles that function
actually creates — `@palette#inactive#…` and a misspelled role both fail.

If the token is a deliberate **probe** with its own fallback, call
`ThemeService::optionalPaletteColor()` (no warning) and append
`// palette-token-optional: <reason>` to any line the literal spans.
`DockWidgetHelper::getDockIcon` is the reference case.

## MVC Rule for Widgets

See [MVC Rules](../../AGENTS.md#mvc-rules-must-follow) — Key rule for widgets: **All layers receive `ServiceLocator&`** via constructor injection (enables DI and testing).

Widgets are the **View** layer. They display data and emit signals but **MUST NOT** modify data directly. Business logic belongs in controllers; data access belongs in services.

## ViewArea2 Framework

For ViewArea2 framework design decisions (splitter orientation, session layout persistence, etc.), see [Key Design Decisions (ViewArea2 Framework)](../AGENTS.md#key-design-decisions-viewarea2-framework).

The orchestrator for all open/close/split/move operations is `ViewAreaController` in `../controllers/`.

## NotebookExplorer2: Sync Button State

`NotebookExplorer2::updateSyncButtonState` (`src/widgets/notebookexplorer2.cpp:1512-1665`) paints the per-notebook sync button and Sync Info menu. It classifies the current notebook into one of the 8 sync states (see [Sync State Model](../core/services/AGENTS.md#sync-state-model)) and sets the `partialSyncConfig` QWidget property, which downstream QSS reacts to.

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

## Widget Construction Pattern

The migration off the legacy singleton architecture (`VNoteX::getInst()`, `ConfigMgr::getInst()`, the legacy `MainWindow`/`Buffer`) is complete; those types have been removed. All widgets, controllers, models, and views take their dependencies via constructor injection with `ServiceLocator&`. Follow this pattern for new code:

```cpp
// Receives dependencies via constructor
class MyWidget : public QWidget {
  Q_OBJECT
public:
  explicit MyWidget(ServiceLocator &p_services, QWidget *p_parent = nullptr);

private:
  ServiceLocator &m_services;

  void doSomething() {
    auto &config = m_services.get<ConfigCoreService>();
    auto &notebooks = m_services.get<NotebookCoreService>();
  }
};
```

### Checklist for a new widget

- [ ] Add `ServiceLocator &p_services` as the first constructor parameter
- [ ] Store reference: `ServiceLocator &m_services`
- [ ] Resolve dependencies via `m_services.get<XxxCoreService>()` — never global state
- [ ] Have the parent widget pass its `ServiceLocator&` down
- [ ] Register the file in the relevant `CMakeLists.txt`
- [ ] Write a unit test with mock services

## Related Modules

- [`../core/AGENTS.md`](../core/AGENTS.md) — ServiceLocator, services injected into widgets
- [`../controllers/AGENTS.md`](../controllers/AGENTS.md) — Controllers that handle widget-initiated actions
- [`../views/AGENTS.md`](../views/AGENTS.md) — Views embedded in widgets
- [`../gui/AGENTS.md`](../gui/AGENTS.md) — GUI services and utilities used by widgets
- [`../../AGENTS.md`](../../AGENTS.md) — Architecture overview, MVC rules, ViewArea2 design decisions

## Comment dock (`CommentPanel` + `CommentProvider`)

The comment dock is a deliberate clone of the Outline dock's shape, so that adding comments to a
second file type needs no dock work at all.

- `ViewWindow2::isCommentsSupported()` defaults to false; only `PdfViewWindow2` opts in.
  `getCommentProvider()` returns null by default. `MainWindow2` switches providers before updating
  visibility on `currentViewWindowChanged`, so pending edits flush to the original provider.
  Unsupported views (including no current view) hide Comments and disable its dock/menu toggle.
  Switching to a PDF shows Comments, except while Expand Content Area hides non-floating,
  non-kept docks. Startup restoration and expand/collapse apply the same capability gate.
  Remembered Comments dimensions are applied only after its dock is shown and laid out;
  unsupported views must not replace them with hidden or pre-layout geometry.
- `CommentProvider` is a pure DATA + SIGNALS object. It holds a `CommentSet` and a selection, and
  carries the view's intents (`activateRequested`, `textEditRequested`, `colorChangeRequested`,
  `deleteRequested`) outward. **It never writes the store and holds no service** — every mutation
  belongs to `CommentController` (`src/controllers/`), per the "views never modify data" rule.
- `CommentPanel` is a pure view over whatever provider it currently holds. Two details are
  load-bearing: it guards every programmatic repaint with `m_updating` so a `setPlainText` /
  `setCurrentIndex` cannot echo an intent straight back, and it **flushes a pending keystroke
  before switching providers**, or the edit would be applied to a different file's comment.
- A comment whose `anchor.type` this build does not implement is listed (with an explanatory
  tooltip) but not rendered on the page. It is still carried through `comments.json` untouched.
- **The dock is a destination, not an entry point.** Comments are created on the PDF page:
  select text, right-click, **Highlight ▸ <color>** (`PdfViewer::contextMenuEvent` emits
  `highlightSelectionRequested`, which `PdfViewWindow2` routes to the adapter). Alt+drag is a
  shortcut only. A `pdf-freetext` box is additionally edited **on the page** — its body is
  `Comment::m_text`, so the dock editor and the box's inline editor are the same operation
  (see [pdf.js AGENTS.md § The Text tool types ON THE PAGE](../data/extra/web/pdf.js/AGENTS.md)).
  If you add comments to another file type, give it a discoverable affordance too —
  an empty dock with no visible way to fill it reads as a broken feature.

Adding a dock is four edits — `DockType` enum (order must match `m_docks`), `MainWindow2::setup*`,
`MainWindow2::getDockWidget()`, `DockWidgetHelper::setupDocks()` + `iconFileName()` + an icon in
`src/data/core/icons/` registered in `core.qrc`.

Store contract: [`../core/services/AGENTS.md` § Comment store](../core/services/AGENTS.md#comment-store-commentsjson).