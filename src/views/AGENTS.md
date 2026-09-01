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

## Item Heights: the delegate owns content, the theme owns padding

Qt applies a theme's `QTreeView::item { padding: 4px 8px; }` inside
`QStyleSheetStyle::sizeFromContents(CT_ItemViewItem, ...)`, which is reachable
**only** through `QStyledItemDelegate::sizeHint()`. A delegate that computes its
own height never gets there, so the theme's padding is silently dropped — that is
how the dock rows drifted apart, and why `native`'s deliberately tighter 2px rule
had no effect at all.

**Rule:** a custom delegate computes only the *content* height and adds the
theme's chrome from the shared helper:

```cpp
QStyleOptionViewItem opt(p_option);
initStyleOption(&opt, p_index);          // protected — only the delegate can call it
const int chrome = ItemViewUtils::verticalChrome(opt);   // <gui/utils/itemviewutils.h>
return QSize(width, opt.fontMetrics.height() + chrome);
```

Use `opt.fontMetrics` (post-`initStyleOption`) for the content maths, never the
raw `p_option` — otherwise `Qt::FontRole` is ignored. Paint paths that need a top
offset use `chrome / 2`.

`verticalChrome()` measures **differentially**, against a synthetic single-line
probe with no decoration and no check indicator. Do not "simplify" it to
`measured - qMax(fontHeight, decorationSize)`: `NotebookNodeModel` and
`SearchResultModel` expose no `Qt::DecorationRole` (their icons are resolved and
painted privately), so that baseline over-subtracts.

**Symmetry assumption:** the helper returns one combined top+bottom figure,
because no public style API exposes the two separately. Every shipped theme uses
symmetric `padding: Npx Mpx`. An asymmetric theme would need the helper split
into a `QMargins` derived from
`QStyleSheetStyle::subElementRect(SE_ItemViewItemText, ...)`.

Gates: `tests/utils/test_itemheight_drift.cpp` (grep gate over the five
overriding delegates), `tests/gui/test_itemviewutils.cpp` (behavioural),
`tests/gui/test_uniformrowheight_invalidation.cpp` (asserts Qt resamples uniform
row heights on `QEvent::StyleChange`, which is why the views need no
`changeEvent` override).

## Related Modules

- [`../models/AGENTS.md`](../models/AGENTS.md) — Models displayed by views
- [`../controllers/AGENTS.md`](../controllers/AGENTS.md) — Controllers that respond to view signals
- [`../widgets/AGENTS.md`](../widgets/AGENTS.md) — Higher-level widgets containing views
- [`../../AGENTS.md`](../../AGENTS.md) — Full MVC rules table, architecture overview
