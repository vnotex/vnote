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

### Other

- `LocationList2` — results list (search hits, backlinks, etc.)
- `FindAndReplaceWidget2` — in-editor find/replace bar
- `AttachmentPopup2` — attachment management popup
- `WordCountPopup2` — word/character count display

## MVC Rule for Widgets

See [MVC Rules](../../AGENTS.md#mvc-rules-must-follow) — Key rule for widgets: **All layers receive `ServiceLocator&`** via constructor injection (enables DI and testing).

Widgets are the **View** layer. They display data and emit signals but **MUST NOT** modify data directly. Business logic belongs in controllers; data access belongs in services.

## ViewArea2 Framework

For ViewArea2 framework design decisions (splitter orientation, session layout persistence, etc.), see [Key Design Decisions (ViewArea2)](../../AGENTS.md#key-design-decisions-viewarea2-framework) in root.

The orchestrator for all open/close/split/move operations is `ViewAreaController` in `../controllers/`.

## Related Modules

- [`../core/AGENTS.md`](../core/AGENTS.md) — ServiceLocator, services injected into widgets
- [`../controllers/AGENTS.md`](../controllers/AGENTS.md) — Controllers that handle widget-initiated actions
- [`../views/AGENTS.md`](../views/AGENTS.md) — Views embedded in widgets
- [`../gui/AGENTS.md`](../gui/AGENTS.md) — GUI services and utilities used by widgets
- [`../../AGENTS.md`](../../AGENTS.md) — Architecture overview, MVC rules, ViewArea2 design decisions
