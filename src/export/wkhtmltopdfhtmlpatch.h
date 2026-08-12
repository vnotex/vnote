#ifndef WKHTMLTOPDFHTMLPATCH_H
#define WKHTMLTOPDFHTMLPATCH_H

#include <QByteArray>

class QPageLayout;

namespace vnotex {
// Five independent wkhtmltopdf (QtWebKit) defects break Mermaid diagrams and CJK text in the
// exported PDF. All workarounds are injected into the intermediate HTML, so they apply to every
// wkhtmltopdf route (single file and all-in-one) and to no other export path.

// 1. The hang. QtWebKit's SVG renderer never settles when a `stroke` is applied to a <marker>
//    element: the conversion stays at "Loading pages 50%" forever. Mermaid emits exactly that
//    (`#vx-mermaid-graph-N .marker{fill:#333333;stroke:#333333;}`, both in each SVG's own inline
//    <style> and in the serialized page stylesheet), so any note containing a Mermaid diagram
//    hangs the PDF export indefinitely. Neutralizing only the `stroke` keeps the arrowheads (they
//    are filled) and lets the conversion finish. Scoped to Mermaid-generated SVG so unrelated
//    inline SVG in the note keeps its intended strokes.
// 2. The scrollbar. `div.vx-mermaid-graph` is styled `overflow-y: hidden`, and CSS turns the other
//    axis into `auto` whenever one axis is not `visible`. On screen that is a convenience; in a
//    PDF it paints a dead horizontal scrollbar and CLIPS the diagram at the column edge. The
//    wrapper is forced back to `overflow: visible` so an oversized diagram overflows visibly
//    instead of being silently cut off.
extern const char *const c_wkhtmltopdfMermaidFixStyle;

// 3. The vanishing diagram. Mermaid emits `<svg width="100%" style="max-width: Wpx">` and relies
//    on the viewBox for the intrinsic aspect ratio. QtWebKit has no intrinsic-aspect-ratio support
//    for SVG, so the element resolves to zero height and the whole diagram is silently dropped
//    (`height: auto` does not help — it renders blank too).
// 4. The overflowing diagram. An <svg> is an unbreakable block, so one wider than the text column
//    is clipped (see 2) and one taller than the printable page height is pushed whole onto the
//    next page, leaving most of the previous page empty.
//
// The script writes concrete pixel width/height derived from the viewBox, scaled down to
// p_maxWidthPx and then, if still too tall, to p_maxHeightPx — aspect ratio preserved in both
// steps. Both limits are passed in rather than measured in the page: wkhtmltopdf's QtWebKit
// reports `clientWidth == 0` for every element at every point of the document lifecycle, so a
// diagram sized from the DOM would keep its natural (often multi-thousand pixel) width.
//
// p_maxWidthPx / p_maxHeightPx are the text column width and the printable page height in CSS
// pixels; pass 0 to disable the corresponding clamp.
QByteArray wkhtmltopdfMermaidSizeFixScript(int p_maxWidthPx, int p_maxHeightPx);

// The width of the exported document's text column, in CSS pixels — the value to pass as
// p_maxWidthPx above. That column is narrower than the paint rect by the export template's
// Bootstrap gutters, measured at a constant 50 px across A4/A5, portrait/landscape and 10/40 mm
// horizontal margins; 60 px is subtracted to keep 10 px of slack. Returns 0 when the layout has no
// usable width, or when the gutters would consume it entirely.
int wkhtmltopdfContentWidthPx(const QPageLayout &p_layout);

// The printable height of p_layout in CSS pixels, i.e. the value to pass as p_maxHeightPx above.
// wkhtmltopdf lays out at a fixed 96 CSS px per inch — verified invariant under `--dpi` and
// `--zoom`, which scale the rendered output rather than the CSS layout — so the paint rect converts
// directly. The result carries an empirical 2% pagination safety margin (measured on A4 with
// 16/10 mm vertical margins) that absorbs rounding and the few pixels the Mermaid wrapper's own
// margins add around the <svg>. Returns 0 when the layout has no usable height.
int wkhtmltopdfPageContentHeightPx(const QPageLayout &p_layout);

// 5. The tofu. wkhtmltopdf's QtWebKit does NOT fall back per glyph along the CSS font-family list:
//    it picks the FIRST INSTALLED family and renders everything with it. VNote's themes start
//    their stack with Latin-only families ("YaHei Consolas Hybrid", "Noto Sans", "Segoe UI", ...)
//    and only name CJK families much later, so on a machine that has, say, Segoe UI, every CJK
//    codepoint misses and is served by Qt's system fallback (MS UI Gothic on Windows), whose JIS
//    coverage lacks most simplified-only forms -> squares. Moving "Microsoft YaHei"/"SimSun" first
//    does not help either: they are .ttc collections, which this QtWebKit cannot load at all.
//
// The fix is to put a family that is BOTH installed and CJK-capable at the head of the stack;
// the caller resolves it against the real font database (see WebViewExporter). Pass an empty
// family to skip the corresponding rule. p_monoFamily keeps <pre>/<code> monospace.
QByteArray wkhtmltopdfFontOverrideStyle(const QByteArray &p_textFamily,
                                        const QByteArray &p_monoFamily);

// Insert every workaround right before the last `</head>` of p_html, operating on raw bytes so the
// document's encoding is never decoded and re-encoded. Return false and leave p_html untouched
// when there is no `</head>` (the injected markup is pure ASCII, and `</head>` is ASCII in every
// encoding VNote's HTML templates can produce). p_textFamily/p_monoFamily are the resolved font
// families for workaround 5; both must be ASCII (a family named in CJK would be re-encoded).
bool insertWkhtmltopdfHtmlPatches(QByteArray &p_html, int p_maxWidthPx, int p_maxHeightPx,
                                  const QByteArray &p_textFamily = QByteArray(),
                                  const QByteArray &p_monoFamily = QByteArray());
} // namespace vnotex

#endif // WKHTMLTOPDFHTMLPATCH_H
