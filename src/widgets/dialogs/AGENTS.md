# Dialogs Module

Dialog widgets (`*Dialog2`) for VNote. Each dialog is paired with a controller in `../../controllers/` that owns business logic; the dialog is the View layer and receives `ServiceLocator&` via constructor.

## Label Capitalization Convention

VNote dialogs use a mixed capitalization scheme. The rules below are the project standard going forward; older dialogs that diverge are tracked under [Known Inconsistencies](#known-inconsistencies-cleanup-backlog) and will be normalized in a follow-up pass.

| Element | Style | Examples |
|---|---|---|
| **Form labels** (multi-word, ending in `:`) | **sentence case** | `"Local root folder:"`, `"Remote URL:"`, `"Output directory:"`, `"Cursor mark:"` |
| **Form labels** that are a proper noun or established technical term | **Title Case** preserved | `"Personal Access Token:"` |
| **Initialisms** (URL, PAT, JSON, HTTP) | preserve their canonical casing inside any label | `"Remote URL:"`, `"JSON path:"` |
| **Placeholders** (the gray helper text inside `QLineEdit`) | **sentence case**, no trailing period | `"Folder to clone into (must not exist or be empty)"`, `"Optional (empty to open as read-only)"` |
| **Buttons** (`QPushButton`, `QDialogButtonBox` buttons) | **Title Case** | `"Open"`, `"Browse"`, `"Disable Sync"`, `"Close Notebook"` |
| **Window / dialog titles** (`setWindowTitle`, `QFileDialog` title) | **Title Case** | `"Open Notebook"`, `"Select Local Root Folder"`, `"Manage Notebooks"` |
| **Tooltips** | sentence form, **end with a period** | `"Remote git URL. Only HTTPS and file:// schemes are supported."` |
| **Banner / info-text messages** (`setInformationText`) | sentence form, **end with a period** | `"Local root folder must be empty (contains 3 item(s))."`, `"Cloning..."` |
| **Radio button / checkbox labels** | **Title Case** | `"Local Folder"`, `"Remote URL"` |

### Why these rules

- **Sentence-case form labels** match the OS-native conventions on macOS and modern GNOME/KDE, and they read faster in dense forms. They also visually disambiguate from buttons.
- **Title-Case buttons + titles** match Windows and Qt's built-in widgets (`QDialogButtonBox` ships with `"Open"`, `"Cancel"`, `"Save"`, etc., already in Title Case).
- **Tooltip periods** make tooltip strings reusable as `qInfo()` log lines and as accessible-name / accessible-description sources.
- **Initialism preservation** prevents the OWASP-style "Personal access token" stripping that confuses GitHub/GitLab users searching for "PAT".

### When in doubt

1. Look at `src/widgets/dialogs/opennotebookdialog2.cpp` and `src/widgets/dialogs/exportdialog2.cpp` — those are the reference dialogs for sentence-case labels.
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

## Dialog Inventory

| Dialog | Controller | Purpose |
|---|---|---|
| `NewNoteDialog2` | `NewNoteController` | Create a new note |
| `NewFolderDialog2` | `NewFolderController` | Create a new folder |
| `NewNotebookDialog2` | `NewNotebookController` | Create a new notebook |
| `OpenNotebookDialog2` | `OpenNotebookController` | Open an existing notebook (local OR remote clone) |
| `ManageNotebooksDialog2` | `ManageNotebooksController` | Notebook management |
| `ImportFolderDialog2` | `ImportFolderController` | Import an external folder as a notebook |
| `OpenVNote3NotebookDialog2` | (legacy migration) | Import a VNote3 notebook |
| `NotebookSyncInfoDialog2` | `NotebookSyncInfoController` | View / edit notebook sync configuration |
| `ExportDialog2` | (`export` controller / inline) | Export notes |
| `NewQuickAccessItemDialog` | (inline) | Add a quick-access entry (used inside Settings) |
| `SnippetInfoWidget2` / snippet dialogs | `SnippetController` | Snippet metadata |

## Known Inconsistencies (Cleanup Backlog)

These labels violate the sentence-case convention and SHOULD be normalized in a focused follow-up plan. Listed here so a future contributor doesn't accidentally copy the wrong style:

| File | Line | Current | Target |
|---|---|---|---|
| `newnotebookdialog2.cpp` | 72 | `"Root Folder:"` | `"Root folder:"` |
| `newnotebookdialog2.cpp` | 107 | `"Sync Method:"` | `"Sync method:"` |
| `newnotebookdialog2.cpp` | 169 | `"Assets Folder:"` | `"Assets folder:"` |
| `managenotebooksdialog2.cpp` | 89 | `"Root Folder:"` | `"Root folder:"` |
| `openvnote3notebookdialog2.cpp` | 35 | `"Source Folder:"` | `"Source folder:"` |
| `openvnote3notebookdialog2.cpp` | 41 | `"Destination Folder:"` | `"Destination folder:"` |
| `openvnote3notebookdialog2.cpp` | 43 | `"Notebook Name:"` | `"Notebook name:"` |
| `notebooksyncinfodialog2.cpp` | 220 | `"Last Sync:"` | `"Last sync:"` |
| `notebooksyncinfodialog2.cpp` | 226 | `"Current State:"` | `"Current state:"` |

`"Personal Access Token:"` and `"Remote URL:"` are NOT in this list — they are correctly preserved per the proper-noun / initialism rules above.

The cleanup should also update each dialog's translation `.ts` entries (`translations/`) and re-run `lupdate`. A single PR is fine because each change is a one-line label edit with no behavior change.

## Related Modules

- [`../AGENTS.md`](../AGENTS.md) — Widget module overview, ViewArea2 framework, `2` suffix convention
- [`../../controllers/AGENTS.md`](../../controllers/AGENTS.md) — Controllers paired with these dialogs
- [`../../../AGENTS.md`](../../../AGENTS.md) — Project-level architecture, MVC rules, code style
