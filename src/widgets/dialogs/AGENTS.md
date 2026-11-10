# Dialogs Module

Dialog widgets (`*Dialog2`) for VNote. Each dialog is paired with a controller in `../../controllers/` that owns business logic; the dialog is the View layer and receives `ServiceLocator&` via constructor.

## Label Capitalization Convention

VNote dialogs use a mixed capitalization scheme. The rules below are the project standard; the sweep applying them is complete (see [Capitalization Cleanup](#capitalization-cleanup-completed)).

| Element | Style | Examples |
|---|---|---|
| **Form / field labels** (`addRow`, a `QLabel` naming an adjacent control) | **sentence case, NO trailing colon** | `"Local root folder"`, `"Remote URL"`, `"Output directory"`, `"Cursor mark"` |
| **Form labels** that are a proper noun or established technical term | **Title Case** preserved, still no colon | `"Personal Access Token"` |
| **`QInputDialog` prompts** (the `label` argument) | sentence form, **keep the trailing colon** | `"Workspace name:"`, `"Enter new parent tag (empty for root):"` |
| **Initialisms** (URL, PAT, JSON, HTTP) | preserve their canonical casing inside any label | `"Remote URL"`, `"JSON path"` |
| **Placeholders** (the gray helper text inside `QLineEdit`) | **sentence case**, no trailing period | `"Folder to clone into (must not exist or be empty)"`, `"Optional (empty to open as read-only)"` |
| **Buttons** (`QPushButton`, `QDialogButtonBox` buttons) | **Title Case** | `"Open"`, `"Browse"`, `"Disable Sync"`, `"Close Notebook"` |
| **Window / dialog titles** (`setWindowTitle`, `QFileDialog` title) | **Title Case** | `"Open Notebook"`, `"Select Local Root Folder"`, `"Manage Notebooks"` |
| **Tooltips** | sentence form, **no terminal punctuation** | `"Remote git URL. Only HTTPS and file:// schemes are supported"` |
| **Banner / info-text messages** (`setInformationText`) | sentence form, **end with a period** | `"Local root folder must be empty (contains 3 item(s))."`, `"Cloning..."` |
| **Radio button / checkbox labels** | **sentence case** | `"Local folder"`, `"Keep both"`, `"Expand Tab"` |
| **ComboBox item labels** | **sentence case** (first word only) | `"Bundled notebook"`, `"No wrap"`, `"Web service"`, `"Local JAR"` |

### Why these rules

- **Sentence-case form labels** match the OS-native conventions on macOS and modern GNOME/KDE, and they read faster in dense forms. They also visually disambiguate from buttons.
- **No trailing colon on field labels** matches the Settings pages, which are the largest labeled-field surface in the app and build every row through `SettingsPageHelper::createSettingRow` with colon-free labels (`"Auto save policy"`, `"Line ending"`, `"Content layout"`). A colon is redundant when the label already sits in a form-layout column next to its control.
- **`QInputDialog` prompts keep their colon** because they are a prompt sentence introducing an entry field, not a column label, and Qt's own dialogs are written that way.
- **Title-Case buttons + titles** match Windows and Qt's built-in widgets (`QDialogButtonBox` ships with `"Open"`, `"Cancel"`, `"Save"`, etc., already in Title Case).
- **Tooltips** follow the root tooltip-copy rule: keep punctuation between clauses, but no terminal punctuation; use lowercase parenthetical qualifiers such as `(restart required)`.
- **Initialism preservation** prevents the OWASP-style "Personal access token" stripping that confuses GitHub/GitLab users searching for "PAT".

### When in doubt

1. Look at `src/widgets/dialogs/opennotebookdialog2.cpp` and `src/widgets/dialogs/exportdialog2.cpp` — those are the reference dialogs for sentence-case, colon-free labels.
2. If your new dialog is dominated by proper nouns or domain-specific multi-word terms (sync state, credentials), match the convention of the closest sibling dialog rather than mechanically forcing sentence case.
3. Never change a label string without also grep'ing the codebase for the old string and the translation `.ts` files — labels are user-visible and may be referenced in tests via `findChild<>(...)` (use **object names**, not label text, for test lookups).

### Test-discovery rule

Tests find dialog widgets via `findChild<>("objectName")` — NOT by label text. Every interactive widget in a `*Dialog2` MUST have an `objectName` set via `setObjectName(QLatin1String(kFooName))` where `kFooName` is a top-of-file constant in the dialog's `.cpp`. Changing label TEXT is a UX change; changing an `objectName` is a TEST change. Keep them decoupled.

## Settings Form Layout

Settings pages use `settings/SettingsPageHelper` for card rows. This is a desktop form
convention, not a rule that every input must occupy the same row as its label.

View-window options remain in their menus/toolbars. Searchable, read-only cards at the top of
**Markdown Editor** explain preview/concealment toggles; **Editor** covers PDF viewing modes
and Typst preview/color options; **Mind Map Editor** explains **Layout** and **Readable Width**.
Use the shared title **Editor Toolbar Options** and description **Options in the toolbar of the
editor**. Each option explains its behavior without repeating its toolbar/menu location. Write
each description as one sentence without a trailing period, putting supplementary details in
parentheses. Do not duplicate these controls in Settings or mark a page changed when
showing/searching the hints.
Use control-free `createSettingRow()` rows and
register their title labels as search targets: the row/description styles suppress hit borders.
Reuse the menu's translation context for existing action titles where available; new explanatory
copy belongs to the settings page.

- **Compact is the default:** ordinary combo boxes, spin boxes and short line edits keep
  their label and input on one row when space permits. Use `createSettingRow()`; never
  choose orientation by recursively searching for a `QLineEdit` (spin boxes contain one).
- **FullWidth is deliberate:** pass `RowLayout::FullWidth` for paths, URLs, commands,
  multiline editors and composites containing a multiline preview, including the Quick Note
  template selector. Put the persistent label above the field. Choose based on content,
  not whether the control is editable.
- **Align, do not float:** compact fields share a font-relative preferred label column and
  start at its trailing edge. Keep combo/spin controls at their natural width; let text fields
  expand. Do not independently right-align fields, stretch numeric inputs across a card,
  or add page-specific fixed-pixel label widths.
- **Wrap rather than squeeze:** each compact row owns its `QFormLayout` with `WrapLongRows`;
  labels also word-wrap. The shared column is a preferred width, not a minimum imposed on
  the scroll area. Preserve height-for-width behavior at narrow sizes and after font changes.
- **Labels remain visible:** placeholders show examples, never replace labels. Keep labels
  sentence case without trailing colons. Bind the label to the actual focusable input; pass
  `p_focusControl` for composites (the browse widget's line edit or the selector's combo),
  not an unfocusable wrapper. Preserve explicit accessible names and descriptions. Helper
  label clicks focus that input without changing its value.
- **Descriptions and grouping:** place concise helper text below its row and allow it to
  wrap across the available width. Use existing themed description/card properties, never
  inline colors. Keep checkbox text beside the checkbox and use `createCheckBoxRow()`;
  related checkbox lists retain a wrapping group heading rather than duplicate field labels.
- **Card surfaces:** the `SettingsCard` owns its background. Set
  `PropertyDefs::c_settingsContainer` on layout-only nested wrappers before first show;
  every bundled theme makes only those opted-in card descendants transparent. Do not mark
  inputs, buttons, lists or preview editors, apply blanket descendant transparency, or
  change enabled state to obtain a colour. White/grey is not a saved/disabled/advanced
  settings distinction. Keep borders and geometry: Native has a narrowly scoped explicit
  frame rule for opted-in group boxes because a stylesheet background replaces native
  frame painting. Page-level surfaces and other dialogs remain outside this rule. Custom
  themes need the same container rule; a same-version development profile may need its
  deployed bundled QSS refreshed, since Theme Refresh does not reinstall cached assets.
- **Action rows:** standalone section and item actions are direction-aware leading-aligned
  (left in LTR, right in RTL) and naturally sized, never stretched across the row. Keep
  related buttons together, with trailing stretch; use `Qt::AlignLeading` for a single
  button in a vertical layout. Match the container's content inset (16 horizontal / 6
  vertical for Settings card action rows); destructive Remove/Delete actions use the same
  rule and stay within their item group. Field-specific Browse/Test/Manage/Add Dictionary
  actions remain adjacent to their input, or leading-aligned with that input when wrapped
  below. Do not move the Apply/Reset toolbar or override native dialog-footer conventions.
- **Provider-field exception:** Image Host's generated credential/URL/path/command forms
  always use `WrapAllRows` and expanding fields. Never infer their layout from config keys
  or credential values. Keep their edits directly parented to the provider `QGroupBox`:
  reload finds the group through the first field's parent.
- **Dynamic ownership is load-bearing:** File Associations' `programRow` / `systemRow`
  flags stay on direct children of the card; inner helper rows may nest below them. Keep
  Add Program after the program groups and Remove within its own group. Quick Note's
  existing group remains the hide/disable owner, and its card keeps the `quicknote` name.

For a settings layout change, inspect the native Settings surface at normal and narrow
widths and enlarged fonts, in light and dark themes. Check aligned compact fields, stacked
long fields, label-to-input focus, and no avoidable horizontal scrolling. Exercise Apply,
Reset and search; for dynamic groups also verify add/remove and saved-value reloads, not
just widget counts. Widths below a control's intrinsic usable minimum may require scrolling;
do not clip labels or values to hide that limit.

## Windows system context menu

Settings → File Associations exposes the **System Context Menu** card only on Windows.
Keep it compact: section title, action buttons and operation feedback, without a static description.
**Add to Context Menu** and **Remove from Context Menu** call the application-owned
`WindowsContextMenuController`, disable while busy, and show its retained success/error result.
They apply immediately for the current Windows user, independently of Apply/Reset; opening or
resetting Settings never mutates the registry. Read in VNote requests Read in a detached window;
Edit in VNote requests Edit in the main window. Windows 11 may list them under **Show more options**.
Only applied **Built-In File Types** suffixes are registered (including Image, excluding Others
and external-program mappings). Apply suffix edits first, then click Add again to reconcile the
registered types and remove old all-files entries. Unsaved edits are ignored; an empty supported
suffix list clears registration. Remove works even after the suffix configuration or install path
changes. The script does not change default file associations or require administrator access.

## Banner Suppression Pattern (Quiet Dialog UX)

Some dialogs (currently `opennotebookdialog2` post `refine-open-notebook-dialog`) intentionally suppress the `ScrollDialog::setInformationText` banner on certain field changes to keep the dialog quiet and stable in size while the user is typing.

Pattern:
1. The validation result struct (`RemoteValidation` inside the dialog) carries an extra `bool surfaceInBanner = false;` flag alongside `valid` and `message`.
2. Each validator branch decides whether the message is "actionable enough to surface". URL-scheme errors are silent (the user is still typing — they don't want a banner blinking). Folder-content errors (non-empty existing dir) surface immediately because the user has stopped typing and clicked a folder.
3. `updateOpenButtonState` reads `surfaceInBanner` and either calls `setInformationText(message, Error)` (surface) or `setInformationText(QString(), Info)` (clear).
4. Clone start / progress / failure / cancel events ALWAYS surface, regardless of `surfaceInBanner`. These are not "while typing" events.

Use this pattern when the dialog has both keystroke-driven validation (noisy) and discrete-action validation (e.g., folder selection). Do NOT use it when every validation message is equally actionable — the regular `setInformationText` flow is simpler.

## Markdown XSS Protection

Settings → Markdown Editor → Read exposes **Protect Markdown from XSS**, backed by the
existing `editor.markdown_editor.protectFromXss` flag (default `true`). Apply uses the normal
editor-configuration notification; Reset discards unsaved checkbox changes. The setting
guards filtering in ordinary Markdown views and exports, not protected notes, which
always retain their separate filtering. Disabling it is only appropriate for trusted notes.

## Markdown List Settings

Settings → Markdown Editor → Edit exposes **Automatically renumber ordered lists**.
`MarkdownEditorConfig` persists it as `editor.markdown_editor.autoNumberOrderedLists`
with a C++ default of `true`. The normal `ConfigMgr2` defaults merge gives older
configs that default while preserving an explicit `false`. Both editor builders
consume it through the shared `applyMarkdownConfigFields()` mapping. Loading a
note or toggling the option does not renumber unchanged source; later structural
list edits do.

List decorations are independent of numbering. Every bundled
`src/data/extra/themes/*/text-editor.theme` supplies `ListItemGuide.text-color`
and `ActiveListItem.background-color` in `markdown-editor-styles`. Keep the guide
visible and the active fill subtler than the cursor-line fill in each palette.
Colors belong in theme assets, not C++ stylesheet literals. A custom theme that
omits a style leaves that decoration disabled; do not inject light-theme defaults.

## Git Sync Username

**Configure Sync / Sync Info** and **Open Notebook (Remote URL)** expose a
`gitUsernameEdit` field. Gitee requires the PAT owner's account login, which may
be different from the repository owner. Do not infer it from the URL path.

`WidgetsFactory::createUrlUserNameEdit` binds the field to the HTTPS URL's
non-secret username component, using `QUrl` for encoding. The URL remains the
source of truth, so load/reset/cancel flows also restore the username and existing
controllers persist it without a second config key. The token is still separate,
password-masked, never prefilled, and stored only through the credentials store.
The username input is disabled for local-file remotes and during remote opening.
Changing only the HTTPS username re-enables authentication in place and preserves
local Git history; it does not use the destructive repository-change flow.
OK and Apply wait for `applyComplete` before clearing edits, and OK closes only
on success, so saved-token retrieval cannot be cancelled by dialog destruction.

## New Note Template Resolution

`NewNoteDialog2` picks the initial template from two sources, in this order:

1. **Session cache** — `NewNoteDialog2::s_lastTemplateByFileType`, a process-lifetime
   `QHash<fileTypeName, templateName>` written only after a note is successfully created (a
   rejected name or a failed creation is not a "last used template"). A present key wins even
   when its value is empty, so an explicit "None" survives the rest of the run.
2. **Configured default** — `WidgetConfig::getNewNoteDefaultTemplate(fileTypeName)`, backed by
   the `newNoteDefaultTemplates` object in `vnotex.json` (a `{fileTypeName: templateName}` map).
   VNote seeds `{"Markdown": "title.md", "MindMap": "title.mmm", "Typst": "title.typst"}` when
   the key is absent; a present object — including an empty one — is the user's choice and is
   never re-seeded. The Typst template inserts the filename without its extension as a heading.

If neither yields a template that still exists on disk (`NoteTemplateSelector::setCurrentTemplate`
returns `false`), the selector falls back to "None" and a stale session entry is dropped so the
configured default gets another chance.

The resolution re-runs on **every** file-type change, including the implicit one driven by typing
a suffix in the Name field — the default is per file type, so it has to follow the type. It stops
following once the user picks a template by hand (`m_templateChosenByUser`); programmatic
selections are excluded from that via `m_templateSelectorMuted`.

Capture dialogs (`BodyMode::LiteralContent`) have no selector and therefore neither read nor write
the session cache. The quick-note path is unrelated: its template name is persisted per scheme in
`SessionConfig::QuickNoteScheme::m_template`.

## Quick Note Window Preference

Settings → Quick Access → Quick Note persists **Open in detached window** per scheme
as `quickNoteSchemes[].detachedView` in `session.json`. Missing means `false`, preserving
older schemes. Include this flag in scheme equality: a toggle-only edit must persist.
Tray, toolbar, keyboard and tab-bar requests use the only scheme directly when exactly
one exists, and show the picker when multiple exist. They honor the chosen scheme without
overriding its destination. Quick notes still open in Edit mode.

Fresh `SessionConfig` instances start with **Quick Note in Current Notebook**, using the
notebook-relative folder `Quick Notes` and filename `quick_note_%da%.md`, with no template.
On a version change from below `4.14.2`, an empty saved list receives this default once;
nonempty custom schemes and empty lists stamped `4.14.2` or newer remain unchanged.
Same-version launches do not migrate. See the
[configuration migration contract](../../core/AGENTS.md#configuration-migration-contract).

Manually added schemes use `ConfigMgr2::getDefaultNotebookPath()`, shared with `FirstRunController`:
`vnote_notebook` under Qt's writable Documents location, without an explicit home-folder fallback.
The helper only calculates the path; startup creation still requires a version change
and zero open notebooks. Existing schemes, including an explicitly empty Folder, are
not migrated. `SystemTrayHelper` registers the configured `NewQuickNote` binding as an
OS-global `QHotkey`, owned by `MainWindow2`, just like the wake-up hotkey. Tray and toolbar
show shortcut text only; the toolbar must not register a duplicate window-local shortcut.
Global activation triggers the guarded tray action, so startup readiness and reentrancy
apply equally to both entry points. Requests during an active modal dialog are ignored.

The tray item stays disabled until `ViewArea2::corePropagationReady` and during its
synchronous request. Hidden/minimized-main pickers and errors are parentless; cancellation
and creation failure never open a buffer. Empty schemes reveal the existing settings
route. Detached schemes leave the main window alone; successful non-detached captures
request the normal `MainWindow2::showMainWindow()` path when it is hidden/minimized.
A parentless picker queues `raise()` and `activateWindow()` after `exec()` shows it:
a global hotkey alone does not give the dialog foreground focus. The callback is scoped
to the picker and skips a picker that has already closed; never activate the hidden
main window or make the picker permanently stay on top to work around focus.
`MessageBoxHelper` uses the same deferred activation for parentless messages, including
quick-note creation errors. Parented message boxes retain their existing behavior.

## Dialog Inventory

| Dialog | Controller | Purpose |
|---|---|---|
| `NewNoteDialog2` | `NewNoteController` | Create a new note |
| `NewFolderDialog2` | `NewFolderController` | Create a new folder |
| `NewNotebookDialog2` | `NewNotebookController` | Create a new notebook |
| `OpenNotebookDialog2` | `OpenNotebookController` | Open an existing notebook (local OR remote clone); hands off to the V3 import flow via the `OpenV3NotebookRequested` result code |
| `ManageNotebooksDialog2` | `ManageNotebooksController` | Notebook management |
| `ImportFolderDialog2` | `ImportFolderController` | Import an external folder as a notebook |
| `OpenVNote3NotebookDialog2` | (legacy migration) | Import a VNote3 notebook |
| `NotebookSyncInfoDialog2` | `NotebookSyncInfoController` | View / edit notebook sync configuration |
| `ExportDialog2` | (`export` controller / inline) | Export notes |
| `NewQuickAccessItemDialog` | (inline) | Add a quick-access entry (used inside Settings) |
| `SnippetInfoWidget2` / snippet dialogs | `SnippetController` | Snippet metadata |
| `ImageInsertDialog` | (inline, `MarkdownEditor`) | Insert an image; also the size-authoring surface (see below) |
| `ImageSizeDialog` | (inline, `MarkdownEditor`) | `Resize Image` on an existing image |

## Inserted Image Encoding

Settings → Editor → **Default image format** persists `editor.core.defaultImageFormat` as
`jpeg` (default) or `png`; missing or invalid values fall back to JPEG. The image-insert dialog
reads the current preference through `ConfigMgr2`, so no restart is needed.

`ImageInsertDialog::getImageData()` preserves original bytes from files and URLs. For a
clipboard `QImage`, image-file insertion uses the selected format: JPEG uses Qt's default
quality and composites alpha onto white; PNG preserves pixels and alpha. Both retain the
original pixel dimensions (independent of device pixel ratio). Base64 insertion keeps lossless
PNG with alpha regardless of the preference; choosing an image file must not mutate that
source. `MarkdownEditor` derives the saved/uploaded filename suffix from the encoded bytes.

## Image Size Authoring

Two surfaces, both legacy-style dialogs (no `2` suffix, no `ServiceLocator`, driven directly by
`MarkdownEditor`) rather than the `*Dialog2` controller pattern — they follow the shape of the
`ImageInsertDialog` that was already there.

- **`ImageInsertDialog`** gains optional **Width (px)** / **Height (px)** fields. They are left
  **empty by default**; the source image's natural size appears only as *placeholder* text.
  Prefilling would turn every single insert into an HTML `<img>`.
- **`ImageSizeDialog`** is the `Resize Image` action, prefilled from the image under the
  cursor. Leaving **both** fields empty means "no size".
  Its **Scale (%)** slider writes both pixel fields proportionally, with 100% representing
  the current dimensions. Manual field edits establish a new baseline; slider moves always
  scale that fixed baseline, not the previously rounded output. The range is 1–200%, reduced
  when needed to keep both dimensions within six digits. Opening the dialog never fills
  unspecified fields, and only OK changes the note.

For missing dimensions, use local image metadata, protected bytes through `readProtectedImage`,
or an already-loaded image preview's **logical** size, matched in block-relative coordinates.
Never fetch a remote image just to resize it or read a protected image through a normal file path.
If a complete positive size cannot be inferred, manual fields remain usable and the slider is
unavailable until a valid width and height establish a baseline.

Any nonzero size makes the emitted reference an HTML `<img …/>` rather than a Markdown link:
Markdown has no portable way to express one (`=WxH` is understood by this editor but by few other
tools). `vte::MarkdownUtils::generateImageLink(title, url, alt, w, h)` makes that choice in one
place.

### `Resize Image` conversion table

`MarkdownEditor::setImageSize()`. All edits go through a single `QTextCursor` edit block (one undo
step), applied in **descending span order** so earlier spans stay valid.

| Current | New size | Result |
|---|---|---|
| Markdown | non-empty | replace the region with `generateImageTag(alt, dest, title, w, h)` |
| Markdown carrying a `=WxH` size | empty | replace the region with `![alt](dest "title")` — drop the size, **stay Markdown** |
| Markdown with no size | empty | no-op |
| HTML | non-empty | edit `width`/`height` **in place**; insert a missing one right after `src`; **remove every occurrence** of a dimension whose new value is 0, and when setting one, update the first occurrence and remove all later duplicates |
| HTML, `!hasUnknownAttrs() && !hasDuplicateAttrs()` **and** the round trip verifies | empty | replace the region with `![alt](dest "title")` |
| HTML, otherwise | empty | remove all `width`/`height` in place; keep the tag |

A Markdown image is not necessarily unsized: the `=WxH` extension is parsed by this
editor's cmark fork and honored by `PreviewMgr`, so `ImageSizeDialog` opens prefilled for one
and its "Leave both empty to remove the size" hint has to actually work.

**Never regenerate an HTML tag VNote did not author** — `class`, `style`, `data-*`, `loading` and
anything else the user wrote must survive. That is why the sized case edits attributes rather than
re-emitting the tag.

**HTML → Markdown is gated on a verified round trip, not a character blacklist.** Build the
candidate Markdown, parse it back with `fetchImageLinks()`, and require exactly one image covering
the whole candidate with the same decoded url, alt and title. A blacklist is provably insufficient:
a bare `a\_b.png` destination parses back as `a_b.png`, a silently different file. The round trip
compares only the *effective* (first-wins) attribute values, so it cannot observe a discarded
duplicate — hence the separate `!hasDuplicateAttrs()` precondition.

Clearing `width="100" width="200"` must remove BOTH: unmasking only the second would leave the
image silently still sized.

Parsing and generation live in vtextedit; see
[`libs/vtextedit/AGENTS.md` § Image References](../../../libs/vtextedit/AGENTS.md#image-references-markdown-and-html).

## Capitalization Cleanup (Completed)

The sentence-case sweep across the new-architecture Settings pages
(`settings/`, `settingswidget.cpp`) and the `*Dialog2` dialogs has been completed.
Content strings — form labels, checkbox/radio labels, combobox items, group/section
body labels, tooltips, and placeholders — use sentence case. Buttons, window/dialog
titles, settings page titles, and `SettingsPageHelper::addSection` card titles remain
Title Case. Proper nouns / product names (PlantUml, MathJax, Graphviz, VNote, Vi, Git),
initialisms (URL, PAT, JSON, JAR, HTML, PDF), keyboard key names (Tab, Ctrl), and the
established terms `Personal Access Token` and `Remote URL` keep their canonical casing.

The colon sweep is likewise complete: no form/field label under `src/widgets/` ends
in `:`. The only remaining trailing colons are `QInputDialog` prompts (which keep
theirs by rule), message-body headings in `markdowneditor.cpp`, sentence-form intro
labels that introduce a list (`syncconflictdialog2.cpp:64`), and the non-visual
settings-search term in `quickaccesspage.cpp:72` (that string is passed to
`addSearchItem` only and is never rendered).

Translation `.ts` files in `src/data/core/translations/` were intentionally left
untouched; a future `lupdate` pass will refresh the source entries.

## Related Modules

- [`../AGENTS.md`](../AGENTS.md) — Widget module overview, ViewArea2 framework, `2` suffix convention
- [`../../controllers/AGENTS.md`](../../controllers/AGENTS.md) — Controllers paired with these dialogs
- [`../../../AGENTS.md`](../../../AGENTS.md) — Project-level architecture, MVC rules, code style
