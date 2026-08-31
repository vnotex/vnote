# In-place preview performance notes

Markdown fixtures for **manual** performance testing of the in-place preview.
Open them in VNote and watch the editor behave under load.

Regenerate (and tune the sizes) with:

```bash
python tests/data/perf/generate_perf_notes.py
```

These are not automated tests. The automated counterpart is
`libs/vtextedit/tests/test_previewbenchmark`, which gates on counters rather
than on the clock. These files cover what a counter cannot: whether scrolling
*feels* smooth, whether a preview visibly jumps when it is built, whether the
window stays responsive while a slow renderer works.

## Two independent pipelines

A change to one says nothing about the other, which is why the fixtures are
split along that line:

| Pipeline | Elements | Machinery |
|---|---|---|
| **Interactive** | tables | `InteractivePreviewHost` builds a live `QTextEdit` sheet per table; lazy realization, band reservation, measurement caching |
| **Painted** | diagrams, math | rendered out of process, cached as pixmaps by `PreviewHelper` and drawn by `TextDocumentLayout` |

## The fixtures

### Interactive — tables

| File | Stresses |
|---|---|
| `tables-many-small.md` | 300 small tables. Lazy realization at scale: every table reserves space and folds its source, but only those near the viewport become live sheets. |
| `tables-many-large.md` | 40 tables of 288 cells, just under the 300-cell per-sheet limit. Sheet *construction* cost — this is where realization stutter appears. |
| `tables-wide-cells.md` | Long wrapping cell text. Probes the size **estimator**, whose band for an unrealized table is approximated from grid shape and average character width. |
| `tables-html-budget.md` | 400 Markdown-backed HTML tables = 6400 cells, crossing the document-wide 5000-cell highlighting budget. |
| `tables-merged.md` | `colspan`/`rowspan`. The rendered grid is not the raw cell matrix, and spans are part of the measurement cache key. |

### Painted — diagrams and math

| File | Stresses |
|---|---|
| `graphs-mermaid.md` | 200 Mermaid diagrams: the render-and-cache path end to end. |
| `graphs-plantuml.md` | 200 PlantUML diagrams. PlantUML starts a JVM, so this is where batching and caching matter most. |
| `graphs-mixed.md` | Mermaid and PlantUML interleaved — two renderers sharing one pipeline. |
| `graphs-duplicates.md` | 300 blocks, only 10 distinct sources. Almost everything should be a cache **hit**. |
| `math-many.md` | 200 inline + block formulas, plus the zoom path through the same cache. |
| `mixed-heavy.md` | Both pipelines at once — the realistic worst case, and the note to profile against. |

## What to look for

- **Opening.** Time to first paint. Only the previews near the viewport should
  be built; the rest reserve space without existing yet.
- **Scrolling.** Previews should appear without the text below them jumping. A
  visible jump means a reserved band disagreed with the real size.
- **Typing.** Type in a *paragraph*, not in a table. Previews must not be
  re-laid-out — editing prose is unrelated to what a table renders.
- **Toggling.** Turn table previews off and on (editor settings). Disabling is
  served from cached snapshots and should not trigger a reparse.
- **Zooming.** Changes the width available to every preview and the raster
  scale of every diagram, so it re-measures and re-rasterizes everything.

### `tables-html-budget.md` specifically

The first **312** tables show per-cell syntax colouring and the rest do not.
That number is exact, not approximate: a table is charged to the budget whole
or not at all, so 312 x 16 = 4992 cells fit, the 313th needs 16 more than the 8
remaining, and every table after it is refused for the same reason. Past that
point the tables must still render and stay editable — only the colouring is
dropped. This is a deliberate, documented degradation (see `changes.md`).
