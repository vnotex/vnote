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

## Tag hierarchy dragging

`TagView` moves one tag and its existing subtree within the current notebook. Dropping
anywhere on a tag row, including its edges, makes that tag the parent; empty viewport
space moves to root. Siblings remain alphabetical. Extended selection still means note
intersection, not a batch move; dimmed incompatible tags remain valid drop destinations.
The matching-file list does not accept drags or drops.

`TagModel` validates the notebook/reload revision and cached ancestor chain, then emits
`moveTagRequested`; it never mutates rows optimistically. `TagController` persists through
the cancellable `TagService` move path. Drop acceptance acknowledges dispatch, not storage
success. `TagExplorer2` reveals successful moves by name after both model resets, with a
notebook guard on queued delivery; it leaves search text unchanged.

Keep the custom `TagView::startDrag()`: Qt's base Move implementation can clear the source
after the controller has already moved it. Native Qt handlers own target feedback, auto-expand
and cancellation. On-item drops through `TreeFilterProxyModel` require **both** row and column
`-1`; Qt otherwise maps an insertion coordinate rather than just the destination index.

Coverage: `tests/gui/test_tag_view_drag_drop.cpp` uses real storage, hooks, proxy and view.
Native mouse smoke is also required to prove source preservation after `QDrag::exec()` and
whole-row feedback over disabled destinations; MIME-only tests cannot prove those behaviors.

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
row heights on `QEvent::StyleChange`, so no height-specific cache invalidation
is needed).

## Notebook tree compact spacing

`NotebookNodeView::compactSpacing` is a transient QSS opt-in, not saved config.
Only `pure` currently enables it: 12px hierarchy indentation and 4px delegate
horizontal padding, with `::item { padding: 2px 4px; }` for row height and editor
geometry. Keep these scoped rules after the generic `QTreeView::item` rule: Qt
gives base and subclass selectors equal specificity. File icons remain 16px.
The notebook inline rename editor also drops the general form-field minimum
height and padding so it fits the compact row. Other trees and themes keep
their existing spacing; no configuration migration is needed.

QSS does not restore omitted `qproperty-*` values on theme switches. The view
handles `StyleChange` in `event()`: call `setCompactSpacing(false)` to restore
both the flag and shared indentation, then polish with the current style before
`QTreeView::event()` relayouts the rows. Do not move this after the base event or
into `changeEvent()`. The delegate uses the same horizontal padding for painting
and width measurement, falling back to 8px without an opted-in widget.

## Search result selection colors

`SearchResultDelegate` keeps `QPalette::Text` for file names and result text when
selected. Selection/hover backgrounds and keyboard focus still come from the style.
Draw the background with `CE_ItemViewItem` and empty text, leaving custom text and
focus painting to the delegate: Windows 11 does not draw selection through
`PE_PanelItemViewItem` alone.
Do not switch to the desktop's `HighlightedText`: it can be white over a light
VNote selection background. `tests/gui/test_themeservice.cpp` renders file, plain-line
and matched-line selections across themes and active/inactive states.

## Related Modules

- [`../models/AGENTS.md`](../models/AGENTS.md) — Models displayed by views
- [`../controllers/AGENTS.md`](../controllers/AGENTS.md) — Controllers that respond to view signals
- [`../widgets/AGENTS.md`](../widgets/AGENTS.md) — Higher-level widgets containing views
- [`../../AGENTS.md`](../../AGENTS.md) — Full MVC rules table, architecture overview
