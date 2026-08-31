#!/usr/bin/env python3
"""Generate Markdown fixtures for MANUAL in-place preview performance testing.

These are not unit tests. They are notes you open in VNote to watch the editor
behave (or misbehave) under load: scroll them, type in them, toggle previews.
The automated counterpart is libs/vtextedit/tests/test_previewbenchmark, which
gates on counters; these files are for the things a counter cannot show you,
like whether scrolling *feels* smooth or whether a preview visibly jumps when
it is realized.

Each fixture isolates ONE pipeline, because the two preview pipelines are
independent and a change to one says nothing about the other:

  * interactive previews  - tables, rendered as live QTextEdit sheets by
                            InteractivePreviewHost (lazy realization, band
                            reservation, measurement caching);
  * painted previews      - graphs and math, rasterized out of process and
                            drawn as pixmaps by PreviewMgr/DocumentResourceMgr
                            through PreviewHelper's LRU caches.

Regenerate with:  python tests/data/perf/generate_perf_notes.py
Tune the counts below; they are chosen to hurt without being absurd.
"""

import os

# --- Fixture sizes -----------------------------------------------------------

# Interactive table pipeline.
MANY_SMALL_TABLES = 300  # 4x4 each
LARGE_TABLE_COUNT = 40  # each just under the 300-cell per-sheet limit
LARGE_TABLE_ROWS = 24
LARGE_TABLE_COLS = 12  # 288 cells
WIDE_CELL_TABLES = 80
MERGED_TABLES = 60

# The document-wide per-cell highlighting budget in markdownastwalker.cpp is
# 5000 cells. 400 tables x 16 cells = 6400 crosses it on purpose, so the
# fixture shows the degradation boundary: the first ~312 tables get cell
# syntax colouring and the rest render without it.
HTML_TABLE_COUNT = 400
HTML_TABLE_ROWS = 4
HTML_TABLE_COLS = 4

# Painted graph pipeline.
GRAPH_COUNT = 200
DUPLICATE_GRAPH_COUNT = 300
DUPLICATE_DISTINCT_SOURCES = 10

# Painted math pipeline.
MATH_BLOCK_COUNT = 200

OUT_DIR = os.path.dirname(os.path.abspath(__file__))


def header(title, what, how):
    return f"# {title}\n\n> **What this stresses:** {what}\n>\n> **How to use it:** {how}\n\n"


def prose(i):
    # Real prose between the elements, so each one is its own block and the
    # document has somewhere to type that is NOT inside a preview.
    return (
        f"Paragraph {i}. This filler exists so the element above is a separate "
        f"block and so there is ordinary text to type in while previews are "
        f"live. Typing here must not re-lay-out the previews.\n\n"
    )


def pipe_table(index, rows, cols, cell=lambda r, c, i: None):
    lines = []
    lines.append("| " + " | ".join(f"h{c}" for c in range(cols)) + " |")
    lines.append("| " + " | ".join("---" for _ in range(cols)) + " |")
    for r in range(1, rows):
        cells = []
        for c in range(cols):
            custom = cell(r, c, index)
            cells.append(custom if custom is not None else f"t{index}r{r}c{c}")
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n\n"


def write(name, text):
    path = os.path.join(OUT_DIR, name)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return path, len(text.encode("utf-8"))


def gen_tables_many_small():
    out = header(
        "Many small tables",
        "Lazy realization and band reservation at scale. Every table is BOUND "
        "(reserves space, folds its source) but only those near the viewport "
        "are REALIZED into live sheets.",
        "Open it and watch how long the first paint takes. Then scroll top to "
        "bottom: previews should appear without the text below them jumping. "
        "Then type in a paragraph - it must stay responsive.",
    )
    for i in range(MANY_SMALL_TABLES):
        out += prose(i)
        out += pipe_table(i, 4, 4)
    return out


def gen_tables_many_large():
    out = header(
        "Fewer, much larger tables",
        "Sheet construction cost. Each table is just under the 300-cell "
        "per-sheet limit, so every realization builds a large QTextTable.",
        "Scroll slowly. Each newly realized table costs a full document "
        "layout, so this is where realization stutter shows up if it exists.",
    )
    for i in range(LARGE_TABLE_COUNT):
        out += prose(i)
        out += pipe_table(i, LARGE_TABLE_ROWS, LARGE_TABLE_COLS)
    return out


def gen_tables_wide_cells():
    out = header(
        "Tables with long, wrapping cell text",
        "The size ESTIMATOR's accuracy. An unrealized table's band is "
        "estimated from the grid shape and an average character width; long "
        "cells wrap, which is exactly where that approximation is weakest.",
        "Scroll down steadily and watch for the document shifting under you as "
        "tables realize. A visible jump means the estimate disagreed with the "
        "real sheet height. The estimator declines to guess when a cell would "
        "wrap more than four lines, so the worst rows here fall back to eager "
        "measurement.",
    )
    lorem = (
        "this cell deliberately contains a long run of words so that it has to "
        "wrap across several lines inside its column"
    )
    for i in range(WIDE_CELL_TABLES):
        out += prose(i)

        def cell(r, c, idx, _lorem=lorem):
            if c == 1:
                return _lorem
            if c == 2 and r == 1:
                return " ".join([_lorem] * 3)
            return None

        out += pipe_table(i, 5, 4, cell)
    return out


def gen_tables_html():
    out = header(
        "Markdown-backed HTML tables (crosses the highlighting budget)",
        "Per-cell cmark parsing. Each Markdown-backed cell is parsed as its "
        f"own document. This file holds {HTML_TABLE_COUNT} tables x "
        f"{HTML_TABLE_ROWS * HTML_TABLE_COLS} cells = "
        f"{HTML_TABLE_COUNT * HTML_TABLE_ROWS * HTML_TABLE_COLS} cells, which "
        "deliberately crosses the document-wide 5000-cell budget.",
        "The first 312 tables should show bold/italic cell syntax colouring "
        "and the rest should not. 312 is exact, not approximate: a table is "
        "charged to the budget whole or not at all, so 312 x 16 = 4992 cells "
        "fit, the 313th needs 16 more than the 8 remaining, and every table "
        "after it is refused for the same reason. They must all still render "
        "and stay editable past that point.",
    )
    for t in range(HTML_TABLE_COUNT):
        out += prose(t)
        out += "<table>\n"
        for r in range(HTML_TABLE_ROWS):
            tag = "th" if r == 0 else "td"
            out += "<tr>"
            for c in range(HTML_TABLE_COLS):
                md = f"**t{t}r{r}c{c}**" if (r + c) % 2 == 0 else f"*t{t}r{r}c{c}*"
                em = "strong" if (r + c) % 2 == 0 else "em"
                out += f"<{tag}><!--vte-md:{md}--><p><{em}>t{t}r{r}c{c}</{em}></p></{tag}>"
            out += "</tr>\n"
        out += "</table>\n\n"
    return out


def gen_tables_merged():
    out = header(
        "HTML tables with merged cells",
        "The logical grid: colspan/rowspan mean the rendered shape is not the "
        "raw cell matrix, and merge spans are part of the measurement cache "
        "key.",
        "Check that merged cells render correctly after scrolling away and "
        "back, and that editing a cell writes back without destroying the "
        "spans.",
    )
    for t in range(MERGED_TABLES):
        out += prose(t)
        out += "<table>\n"
        out += f'<tr><th colspan="3">t{t} spanning header</th></tr>\n'
        out += f'<tr><td rowspan="2">t{t} tall</td><td>b</td><td>c</td></tr>\n'
        out += "<tr><td>e</td><td>f</td></tr>\n"
        out += f'<tr><td>g</td><td colspan="2">t{t} wide</td></tr>\n'
        out += "</table>\n\n"
    return out


MERMAID_SHAPES = [
    "graph TD\n  A{i}[Start {i}] --> B{i}[Middle {i}]\n  B{i} --> C{i}[End {i}]",
    "sequenceDiagram\n  Alice->>Bob{i}: request {i}\n  Bob{i}-->>Alice: reply {i}",
    "graph LR\n  X{i}(( {i} )) --> Y{i}[[step {i}]]\n  Y{i} --> Z{i}{{done {i}}}",
]

PUML_SHAPES = [
    "@startuml\nAlice -> Bob{i} : message {i}\nBob{i} --> Alice : reply {i}\n@enduml",
    "@startuml\nclass Node{i} {{\n  +int id\n  +run()\n}}\nNode{i} --> Leaf{i}\n@enduml",
    "@startuml\nstart\n:step {i};\nif (ok {i}?) then (yes)\n  :done;\nendif\nstop\n@enduml",
]


def fenced(lang, body):
    return f"```{lang}\n{body}\n```\n\n"


def gen_graphs(lang_cycle, name, what, how, count=GRAPH_COUNT):
    out = header(name, what, how)
    for i in range(count):
        out += prose(i)
        lang, shapes = lang_cycle[i % len(lang_cycle)]
        out += fenced(lang, shapes[i % len(shapes)].format(i=i))
    return out


def gen_graphs_duplicates():
    out = header(
        "Many graphs, only a few distinct sources",
        "The preview LRU caches in PreviewHelper. Only "
        f"{DUPLICATE_DISTINCT_SOURCES} distinct diagrams appear, repeated "
        f"{DUPLICATE_GRAPH_COUNT // DUPLICATE_DISTINCT_SOURCES} times each, so "
        "almost every block should be a cache HIT rather than a fresh render.",
        "Watch how long it takes to fully render, then edit one character in a "
        "paragraph and watch again. If the second pass re-renders every "
        "diagram instead of hitting the cache, the cache is too small or is "
        "being sized after the requests are dispatched.",
    )
    for i in range(DUPLICATE_GRAPH_COUNT):
        out += prose(i)
        k = i % DUPLICATE_DISTINCT_SOURCES
        if k % 2 == 0:
            out += fenced("mermaid", MERMAID_SHAPES[0].format(i=k))
        else:
            out += fenced("puml", PUML_SHAPES[0].format(i=k))
    return out


def gen_math():
    out = header(
        "Many math formulas",
        "The painted math pipeline: every block formula is rasterized out of "
        "process and cached as a pixmap, exactly like a diagram.",
        "Scroll through and then change the editor zoom - every formula has to "
        "be re-rasterized at the new scale, which is the zoom path through the "
        "same cache.",
    )
    for i in range(MATH_BLOCK_COUNT):
        out += prose(i)
        out += f"Inline $a_{{{i}}} + b^{{{i}}} = c_{{{i}}}$ in a sentence.\n\n"
        out += (
            "$$\n"
            f"\\int_0^{{{i + 1}}} x^{{{i % 5 + 1}}}\\,dx = "
            f"\\frac{{{i + 1}^{{{i % 5 + 2}}}}}{{{i % 5 + 2}}}\n"
            "$$\n\n"
        )
    return out


def gen_mixed():
    out = header(
        "Everything at once",
        "Both pipelines competing in one note, which is the realistic worst "
        "case: interactive table sheets and painted diagram pixmaps are "
        "produced by independent machinery that shares one editor.",
        "This is the note to keep open while profiling. Scroll it, type in it, "
        "toggle previews off and on, and change the zoom.",
    )
    for i in range(120):
        out += prose(i)
        out += pipe_table(i, 4, 4)
        out += fenced("mermaid", MERMAID_SHAPES[i % len(MERMAID_SHAPES)].format(i=i))
        out += f"Some inline math $x_{{{i}}}^2$ and a formula:\n\n"
        out += f"$$\nE_{{{i}}} = m c^2\n$$\n\n"
        out += fenced("puml", PUML_SHAPES[i % len(PUML_SHAPES)].format(i=i))
    return out


def main():
    fixtures = [
        ("tables-many-small.md", gen_tables_many_small()),
        ("tables-many-large.md", gen_tables_many_large()),
        ("tables-wide-cells.md", gen_tables_wide_cells()),
        ("tables-html-budget.md", gen_tables_html()),
        ("tables-merged.md", gen_tables_merged()),
        (
            "graphs-mermaid.md",
            gen_graphs(
                [("mermaid", MERMAID_SHAPES)],
                "Many Mermaid diagrams",
                "The painted graph pipeline end to end: one out-of-process "
                "render per distinct block, then a pixmap per element.",
                "Time the first full render, then scroll away and back - the "
                "second pass should be served from the cache.",
            ),
        ),
        (
            "graphs-plantuml.md",
            gen_graphs(
                [("puml", PUML_SHAPES)],
                "Many PlantUML diagrams",
                "The same pipeline through a different, much slower renderer. "
                "PlantUML starts a JVM, so this is the fixture where request "
                "batching and caching matter most.",
                "Expect a slow first render. What matters is that the editor "
                "stays responsive while it happens, and that a later edit does "
                "not re-render everything.",
            ),
        ),
        (
            "graphs-mixed.md",
            gen_graphs(
                [("mermaid", MERMAID_SHAPES), ("puml", PUML_SHAPES)],
                "Mermaid and PlantUML interleaved",
                "Two renderers competing for the same preview machinery.",
                "Check that a slow PlantUML render never blocks the Mermaid "
                "diagrams around it, and vice versa.",
            ),
        ),
        ("graphs-duplicates.md", gen_graphs_duplicates()),
        ("math-many.md", gen_math()),
        ("mixed-heavy.md", gen_mixed()),
    ]

    total = 0
    for name, text in fixtures:
        path, size = write(name, text)
        total += size
        print(f"{name:28} {size / 1024:8.1f} KiB")
    print(f"{'TOTAL':28} {total / 1024:8.1f} KiB")


if __name__ == "__main__":
    main()
