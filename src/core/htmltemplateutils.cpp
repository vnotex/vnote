// Pure, config-free HTML template helpers extracted from htmltemplatehelper.cpp so that
// new-architecture consumers can link them without dragging in the legacy singletons
// (ConfigMgr/ThemeMgr/VNoteX). Belongs to the core_services library.

#include "htmltemplateutils.h"

#include <core/webresource.h>
#include <utils/htmlutils.h>

using namespace vnotex;

void HtmlTemplateUtils::fillOutlinePanel(QString &p_template, WebResource &p_exportResource,
                                         bool p_addOutlinePanel) {
  for (auto &ele : p_exportResource.m_resources) {
    if (ele.m_name == QStringLiteral("outline")) {
      ele.m_enabled = p_addOutlinePanel;
      break;
    }
  }

  // Remove static content to make the page clean.
  if (!p_addOutlinePanel) {
    int startIdx = p_template.indexOf("<!-- VX_OUTLINE_PANEL_START -->");
    QString endMark("<!-- VX_OUTLINE_PANEL_END -->");
    int endIdx = p_template.lastIndexOf(endMark);
    Q_ASSERT(startIdx > -1 && endIdx > startIdx);
    p_template.remove(startIdx, endIdx + endMark.size() - startIdx);

    startIdx = p_template.indexOf("<!-- VX_OUTLINE_BUTTON_START -->");
    endMark = "<!-- VX_OUTLINE_BUTTON_END -->";
    endIdx = p_template.lastIndexOf(endMark);
    Q_ASSERT(startIdx > -1 && endIdx > startIdx);
    p_template.remove(startIdx, endIdx + endMark.size() - startIdx);
  }
}

void HtmlTemplateUtils::fillTitle(QString &p_template, const QString &p_title) {
  if (!p_title.isEmpty()) {
    p_template.replace("<!-- VX_TITLE_PLACEHOLDER -->",
                       QStringLiteral("<title>%1</title>").arg(HtmlUtils::escapeHtml(p_title)));
  }
}

void HtmlTemplateUtils::fillStyleContent(QString &p_template, const QString &p_styles) {
  p_template.replace("/* VX_STYLES_CONTENT_PLACEHOLDER */", p_styles);
}

void HtmlTemplateUtils::fillHeadContent(QString &p_template, const QString &p_head) {
  p_template.replace("<!-- VX_HEAD_PLACEHOLDER -->", p_head);
}

void HtmlTemplateUtils::fillContent(QString &p_template, const QString &p_content) {
  p_template.replace("<!-- VX_CONTENT_PLACEHOLDER -->", p_content);
}

void HtmlTemplateUtils::fillBodyClassList(QString &p_template, const QString &p_classList) {
  p_template.replace("<!-- VX_BODY_CLASS_LIST_PLACEHOLDER -->", p_classList);
}
