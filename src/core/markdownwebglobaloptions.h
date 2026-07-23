#ifndef MARKDOWNWEBGLOBALOPTIONS_H
#define MARKDOWNWEBGLOBALOPTIONS_H

#include <QString>

namespace vnotex {

// Global options to be passed to Web side at the very beginning for Markdown.
// Lives in its own header (not htmltemplatehelper.h) so that new-architecture
// consumers (e.g. HtmlTemplateService) can use it without dragging in the legacy
// singletons (ConfigMgr/ThemeMgr/VNoteX) that HtmlTemplateHelper depends on.
struct MarkdownWebGlobalOptions {
  bool m_webPlantUml = true;

  QString m_plantUmlWebService;

  bool m_webGraphviz = true;

  QString m_mathJaxScript;

  bool m_constrainImageWidthEnabled = true;

  bool m_imageAlignCenterEnabled = true;

  bool m_protectFromXss = false;

  bool m_htmlTagEnabled = true;

  bool m_autoBreakEnabled = false;

  bool m_linkifyEnabled = true;

  bool m_indentFirstLineEnabled = false;

  bool m_codeBlockLineNumberEnabled = true;

  bool m_codeBlockLineWrapEnabled = false;

  // Force to use transparent background.
  bool m_transparentBackgroundEnabled = false;

  // Whether the content elements are scrollable. Like PDF, it is false.
  bool m_scrollable = true;

  int m_bodyWidth = -1;

  int m_bodyHeight = -1;

  // Whether transform inlie SVG to PNG.
  // For wkhtmltopdf converter, it could not render some inline SVG correctly.
  // This is just a hint not mandatory. For now, PlantUML and Graphviz needs this.
  bool m_transformSvgToPngEnabled = false;

  // wkhtmltopdf renders MathJax at the surrounding (Latin) text size, which looks too
  // small next to CJK glyphs; the export path scales it up. -1 => MathJax default 1.
  // NOTE: MathJax v3 SVG output has no matchFontHeight, so its `minScale` option is a
  // no-op -- `scale` is the only effective lever (verified by measuring rendered height).
  qreal m_mathJaxScale = -1;

  // Whether remove the tool bar of code blocks added by Prism.js.
  bool m_removeCodeToolBarEnabled = false;

  QString toJavascriptObject() const;
};

} // namespace vnotex

#endif // MARKDOWNWEBGLOBALOPTIONS_H
