# Views Module

Qt view widgets and custom item delegates. They display model data, capture user input, and emit signals — but **never modify data directly**.

See [MVC Rules](../../AGENTS.md#mvc-rules-must-follow) — Key rule for views: **Views MUST NOT modify data directly** (only display and emit signals).

## View & Delegate Inventory

| File | Role |
|------|------|
| `NotebookNodeView` | QTreeView for notebook node hierarchy |
| `NotebookNodeDelegate` | Item delegate for node rendering |
| `CombinedNodeExplorer` | Composite widget wiring MVC components together |
| `TwoColumnsNodeExplorer` | Two-column node explorer layout |
| `FileNodeDelegate` | Item delegate for file list rendering |
| `FileListView` | File list view |
| `OutlineView` | Document outline tree view |
| `SearchResultView` | Search results display |
| `SearchResultDelegate` | Search result rendering delegate |
| `TagView` | Tag hierarchy view |
| `TagNodeListView` | Tag-associated node list |
| `NodeIconHelper` | Helper for node icon resolution |
| `INodeExplorer` | Interface for node explorers |

## Related Modules

- [`../models/AGENTS.md`](../models/AGENTS.md) — Models displayed by views
- [`../controllers/AGENTS.md`](../controllers/AGENTS.md) — Controllers that respond to view signals
- [`../widgets/AGENTS.md`](../widgets/AGENTS.md) — Higher-level widgets containing views
- [`../../AGENTS.md`](../../AGENTS.md) — Full MVC rules table, architecture overview
