# Models — Qt Model/View Data Representations

Models are Qt Model/View data representations (`QAbstractItemModel` subclasses and proxy models). They hold data and expose it to views via Qt's model/view framework. Models are pure data layers — they do not depend on any specific view or widget.

See [MVC Rules](../../AGENTS.md#mvc-rules-must-follow) — Key rule for models: **Models MUST NOT contain UI logic** (reusable across different views).

## Model Inventory

| Model | Base Class | Purpose |
|-------|-----------|---------|
| `NotebookNodeModel` | `QAbstractItemModel` | Node hierarchy (folders and files) |
| `NotebookNodeProxyModel` | `QSortFilterProxyModel` | Sorting/filtering nodes |
| `OutlineModel` | `QAbstractItemModel` | Document heading outline |
| `SearchResultModel` | `QAbstractItemModel` | Search result data |
| `SnippetListModel` | `QAbstractItemModel` | Snippet list data |
| `TagModel` | `QAbstractItemModel` | Tag hierarchy |
| `TagFileModel` | `QAbstractItemModel` | Files associated with tags |
| `AttachmentListModel` | `QAbstractItemModel` | Attachment list data |
| `TreeFilterProxyModel` | `QSortFilterProxyModel` | Generic tree filtering proxy |
| `INodeListModel` | — | Interface for node list models |

## Related Modules

- [`../controllers/AGENTS.md`](../controllers/AGENTS.md) — Controllers that manipulate models
- [`../views/AGENTS.md`](../views/AGENTS.md) — Views that display model data
- [`../../AGENTS.md`](../../AGENTS.md) — Full MVC rules table, architecture overview
