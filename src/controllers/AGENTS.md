# Controllers

Controllers are QObject-based business logic mediators that sit between models, views, and services. They translate user actions into service calls and service results into UI feedback, orchestrating operations without containing any UI logic themselves.

## MVC Rules for Controllers

See [MVC Rules](../../AGENTS.md#mvc-rules-must-follow) — Key rule for controllers: **Controllers MUST NOT inherit from QWidget** (testable without GUI).

See [MVC Example](../../AGENTS.md#mvc-example-notebook-node-operations) for the Controller → View → Model flow.

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

## Related Modules

- [`../core/AGENTS.md`](../core/AGENTS.md) — ServiceLocator and services used by controllers
- [`../models/AGENTS.md`](../models/AGENTS.md) — Models manipulated by controllers
- [`../views/AGENTS.md`](../views/AGENTS.md) — Views that signal controllers
- [`../widgets/AGENTS.md`](../widgets/AGENTS.md) — Widgets containing MVC wiring
- [`../../AGENTS.md`](../../AGENTS.md) — Full MVC rules, architecture overview, hook system
