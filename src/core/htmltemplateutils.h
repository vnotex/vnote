#ifndef HTMLTEMPLATEUTILS_H
#define HTMLTEMPLATEUTILS_H

#include <QString>

namespace vnotex {
struct WebResource;

// Pure, config-free HTML template helpers extracted from HtmlTemplateHelper so that
// new-architecture consumers (e.g. WebViewExporter, HtmlTemplateService) can use them
// without dragging in the legacy singletons (ConfigMgr/ThemeMgr/VNoteX) that the rest of
// HtmlTemplateHelper depends on.
namespace HtmlTemplateUtils {

// Markdown viewer template parameters (config-agnostic value type).
struct MarkdownParas {
  QString m_webStyleSheetFile;

  QString m_highlightStyleSheetFile;

  bool m_transparentBackgroundEnabled = false;

  bool m_scrollable = true;

  int m_bodyWidth = -1;

  int m_bodyHeight = -1;

  bool m_transformSvgToPngEnabled = false;

  qreal m_mathJaxScale = -1;

  bool m_removeCodeToolBarEnabled = false;
};

void fillTitle(QString &p_template, const QString &p_title);

void fillStyleContent(QString &p_template, const QString &p_styles);

void fillHeadContent(QString &p_template, const QString &p_head);

void fillContent(QString &p_template, const QString &p_content);

void fillBodyClassList(QString &p_template, const QString &p_classList);

void fillOutlinePanel(QString &p_template, WebResource &p_exportResource, bool p_addOutlinePanel);

} // namespace HtmlTemplateUtils
} // namespace vnotex

#endif // HTMLTEMPLATEUTILS_H
