#include "wkhtmltopdfhtmlpatch.h"

#include <QPageLayout>
#include <QRectF>

namespace vnotex {
const char *const c_wkhtmltopdfMermaidFixStyle =
    "<style>svg[id^=\"vx-mermaid-graph\"] .marker, svg[id^=\"vx-mermaid-graph\"] marker, "
    "svg[id^=\"vx-mermaid-graph\"] .marker.cross { stroke: none !important; }\n"
    "div.vx-mermaid-graph { overflow: visible !important; }</style>";

// %1 is the text column width and %2 the printable page height, in CSS pixels (0 disables that
// clamp).
static const char *const c_mermaidSizeFixScriptTemplate =
    "<script type=\"text/javascript\">\n"
    "(function () {\n"
    "  var maxWidth = %1, maxHeight = %2;\n"
    "  function fit(w, h) {\n"
    "    if (maxWidth > 0 && w > maxWidth) { h = h * maxWidth / w; w = maxWidth; }\n"
    "    if (maxHeight > 0 && h > maxHeight) { w = w * maxHeight / h; h = maxHeight; }\n"
    "    return [w, h];\n"
    "  }\n"
    "  function pin() {\n"
    "    var svgs = document.querySelectorAll('svg[id^=\"vx-mermaid-graph\"]');\n"
    "    for (var i = 0; i < svgs.length; ++i) {\n"
    "      var svg = svgs[i];\n"
    "      var vb = (svg.getAttribute('viewBox') || '').split(/[\\s,]+/);\n"
    "      if (vb.length < 4) { continue; }\n"
    "      var w = parseFloat(vb[2]), h = parseFloat(vb[3]);\n"
    "      if (!(w > 0) || !(h > 0)) { continue; }\n"
    "      var wh = fit(w, h); w = wh[0]; h = wh[1];\n"
    "      svg.style.maxWidth = 'none';\n"
    "      svg.setAttribute('width', Math.round(w));\n"
    "      svg.setAttribute('height', Math.round(h));\n"
    "    }\n"
    "    var imgs = document.querySelectorAll('img[data-mermaid-png]');\n"
    "    for (var j = 0; j < imgs.length; ++j) {\n"
    "      var img = imgs[j];\n"
    "      var iw = parseFloat(img.getAttribute('data-mermaid-width'));\n"
    "      var ih = parseFloat(img.getAttribute('data-mermaid-height'));\n"
    "      if (!(iw > 0) || !(ih > 0)) { continue; }\n"
    "      var ss = fit(iw, ih);\n"
    "      img.style.maxWidth = 'none';\n"
    "      img.style.width = Math.round(ss[0]) + 'px';\n"
    "      img.style.height = Math.round(ss[1]) + 'px';\n"
    "    }\n"
    "  }\n"
    "  if (document.readyState === 'complete' || document.readyState === 'interactive') {\n"
    "    pin();\n"
    "  } else {\n"
    "    document.addEventListener('DOMContentLoaded', pin);\n"
    "  }\n"
    "})();\n"
    "</script>";

// The Bootstrap gutters the export template puts around the text column, in CSS pixels. Measured
// constant at 50 px across page sizes, orientations and margins; 10 px of slack is added.
static const int c_contentGutterPx = 60;

QByteArray wkhtmltopdfMermaidSizeFixScript(int p_maxWidthPx, int p_maxHeightPx) {
  QByteArray script(c_mermaidSizeFixScriptTemplate);
  script.replace("%1", QByteArray::number(p_maxWidthPx > 0 ? p_maxWidthPx : 0));
  script.replace("%2", QByteArray::number(p_maxHeightPx > 0 ? p_maxHeightPx : 0));
  return script;
}

int wkhtmltopdfContentWidthPx(const QPageLayout &p_layout) {
  const auto rectMM = p_layout.paintRect(QPageLayout::Millimeter);
  if (!(rectMM.width() > 0)) {
    return 0;
  }

  const int widthPx = static_cast<int>(rectMM.width() / 25.4 * 96.0) - c_contentGutterPx;
  return widthPx > 0 ? widthPx : 0;
}

int wkhtmltopdfPageContentHeightPx(const QPageLayout &p_layout) {
  const auto rectMM = p_layout.paintRect(QPageLayout::Millimeter);
  if (!(rectMM.height() > 0)) {
    return 0;
  }

  return static_cast<int>(rectMM.height() / 25.4 * 96.0 * 0.98);
}

// Sanitize a CSS font family name so it can be embedded inside a double-quoted CSS string. The
// caller resolves families from the font database, so this only guards against a pathological name
// breaking out of the rule (and against non-ASCII, which the byte-level patch cannot re-encode).
static QByteArray sanitizeFamily(const QByteArray &p_family) {
  QByteArray out;
  out.reserve(p_family.size());
  for (char ch : p_family) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (uch < 0x20 || uch > 0x7e || ch == '"' || ch == '\\' || ch == '<' || ch == '>' ||
        ch == '{' || ch == '}' || ch == ';') {
      continue;
    }
    out.append(ch);
  }
  return out.trimmed();
}

QByteArray wkhtmltopdfFontOverrideStyle(const QByteArray &p_textFamily,
                                        const QByteArray &p_monoFamily) {
  const QByteArray text = sanitizeFamily(p_textFamily);
  const QByteArray mono = sanitizeFamily(p_monoFamily);
  if (text.isEmpty() && mono.isEmpty()) {
    return QByteArray();
  }

  QByteArray style("<style type=\"text/css\">\n");
  if (!text.isEmpty()) {
    // `body *` so the theme's per-element stacks (headings, tables, blockquotes, SVG <text>, ...)
    // are overridden too: the FIRST family is the only one QtWebKit ever uses.
    style += "body, body * { font-family: \"" + text + "\", sans-serif !important; }\n";
  }
  if (!mono.isEmpty()) {
    // Higher specificity than the rule above so code keeps a monospace face; declared after it as
    // well, for the selectors whose specificity merely ties.
    style += "body pre, body pre *, body code, body code *, body kbd, body samp "
             "{ font-family: \"" +
             mono + "\", monospace !important; }\n";
  }
  style += "</style>";
  return style;
}

bool insertWkhtmltopdfHtmlPatches(QByteArray &p_html, int p_maxWidthPx, int p_maxHeightPx,
                                  const QByteArray &p_textFamily, const QByteArray &p_monoFamily) {
  const int idx = p_html.toLower().lastIndexOf("</head>");
  if (idx < 0) {
    return false;
  }

  p_html.insert(idx, wkhtmltopdfFontOverrideStyle(p_textFamily, p_monoFamily));
  p_html.insert(idx, wkhtmltopdfMermaidSizeFixScript(p_maxWidthPx, p_maxHeightPx));
  p_html.insert(idx, c_wkhtmltopdfMermaidFixStyle);
  return true;
}
} // namespace vnotex
