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
| **Tooltips** | sentence form, **end with a period** | `"Remote git URL. Only HTTPS and file:// schemes are supported."` |
| **Banner / info-text messages** (`setInformationText`) | sentence form, **end with a period** | `"Local root folder must be empty (contains 3 item(s))."`, `"Cloning..."` |
| **Radio button / checkbox labels** | **sentence case** | `"Local folder"`, `"Keep both"`, `"Expand Tab"` |
| **ComboBox item labels** | **sentence case** (first word only) | `"Bundled notebook"`, `"No wrap"`, `"Web service"`, `"Local JAR"` |

### Why these rules

- **Sentence-case form labels** match the OS-native conventions on macOS and modern GNOME/KDE, and they read faster in dense forms. They also visually disambiguate from buttons.
- **No trailing colon on field labels** matches the Settings pages, which are the largest labeled-field surface in the app and build every row through `SettingsPageHelper::createSettingRow` with colon-free labels (`"Auto save policy"`, `"Line ending"`, `"Content layout"`). A colon is redundant when the label already sits in a form-layout column next to its control.
- **`QInputDialog` prompts keep their colon** because they are a prompt sentence introducing an entry field, not a column label, and Qt's own dialogs are written that way.
- **Title-Case buttons + titles** match Windows and Qt's built-in widgets (`QDialogButtonBox` ships with `"Open"`, `"Cancel"`, `"Save"`, etc., already in Title Case).
- **Tooltip periods** make tooltip strings reusable as `qInfo()` log lines and as accessible-name / accessible-description sources.
- **Initialism preservation** prevents the OWASP-style "Personal access token" stripping that confuses GitHub/GitLab users searching for "PAT".

### When in doubt

1. Look at `src/widgets/dialogs/opennotebookdialog2.cpp` and `src/widgets/dialogs/exportdialog2.cpp` — those are the reference dialogs for sentence-case, colon-free labels.
2. If your new dialog is dominated by proper nouns or domain-specific multi-word terms (sync state, credentials), match the convention of the closest sibling dialog rather than mechanically forcing sentence case.
3. Never change a label string without also grep'ing the codebase for the old string and the translation `.ts` files — labels are user-visible and may be referenced in tests via `findChild<>(...)` (use **object names**, not label text, for test lookups).

### Test-discovery rule

Tests find dialog widgets via `findChild<>("objectName")` — NOT by label text. Every interactive widget in a `*Dialog2` MUST have an `objectName` set via `setObjectName(QLatin1String(kFooName))` where `kFooName` is a top-of-file constant in the dialog's `.cpp`. Changing label TEXT is a UX change; changing an `objectName` is a TEST change. Keep them decoupled.

## Banner Suppression Pattern (Quiet Dialog UX)

Some dialogs (currently `opennotebookdialog2` post `refine-open-notebook-dialog`) intentionally suppress the `ScrollDialog::setInformationText` banner on certain field changes to keep the dialog quiet and stable in size while the user is typing.

Pattern:
1. The validation result struct (`RemoteValidation` inside the dialog) carries an extra `bool surfaceInBanner = false;` flag alongside `valid` and `message`.
2. Each validator branch decides whether the message is "actionable enough to surface". URL-scheme errors are silent (the user is still typing — they don't want a banner blinking). Folder-content errors (non-empty existing dir) surface immediately because the user has stopped typing and clicked a folder.
3. `updateOpenButtonState` reads `surfaceInBanner` and either calls `setInformationText(message, Error)` (surface) or `setInformationText(QString(), Info)` (clear).
4. Clone start / progress / failure / cancel events ALWAYS surface, regardless of `surfaceInBanner`. These are not "while typing" events.

Use this pattern when the dialog has both keystroke-driven validation (noisy) and discrete-action validation (e.g., folder selection). Do NOT use it when every validation message is equally actionable — the regular `setInformationText` flow is simpler.

## New Note Template Resolution

`NewNoteDialog2` picks the initial template from two sources, in this order:

1. **Session cache** — `NewNoteDialog2::s_lastTemplateByFileType`, a process-lifetime
   `QHash<fileTypeName, templateName>` written only after a note is successfully created (a
   rejected name or a failed creation is not a "last used template"). A present key wins even
   when its value is empty, so an explicit "None" survives the rest of the run.
2. **Configured default** — `WidgetConfig::getNewNoteDefaultTemplate(fileTypeName)`, backed by
   the `newNoteDefaultTemplates` object in `vnotex.json` (a `{fileTypeName: templateName}` map).
   VNote seeds `{"Markdown": "title.md"}` when the key is absent; a present object — including
   an empty one — is the user's choice and is never re-seeded.

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
| `ImageSizeDialog` | (inline, `MarkdownEditor`) | `Image > Set Size…` on an existing image |

## Image Size Authoring

Two surfaces, both legacy-style dialogs (no `2` suffix, no `ServiceLocator`, driven directly by
`MarkdownEditor`) rather than the `*Dialog2` controller pattern — they follow the shape of the
`ImageInsertDialog` that was already there.

- **`ImageInsertDialog`** gains optional **Width (px)** / **Height (px)** fields. They are left
  **empty by default**; the source image's natural size appears only as *placeholder* text.
  Prefilling would turn every single insert into an HTML `<img>`.
- **`ImageSizeDialog`** is the `Image > Set Size…` action, prefilled from the image under the
  cursor. Leaving **both** fields empty means "no size".

Any nonzero size makes the emitted reference an HTML `<img …/>` rather than a Markdown link:
Markdown has no portable way to express one (`=WxH` is understood by this editor but by few other
tools). `vte::MarkdownUtils::generateImageLink(title, url, alt, w, h)` makes that choice in one
place.

### `Set Size…` conversion table

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
