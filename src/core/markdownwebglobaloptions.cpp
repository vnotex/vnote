// MarkdownWebGlobalOptions::toJavascriptObject() — extracted from htmltemplatehelper.cpp
// so that HtmlTemplateService (in core_services) can link this symbol without dragging
// in the legacy singletons (ConfigMgr/ThemeMgr/VNoteX) that the rest of HtmlTemplateHelper
// depends on. Belongs to the core_services library.

#include "htmltemplatehelper.h"

#include <utils/utils.h>

using namespace vnotex;

QString MarkdownWebGlobalOptions::toJavascriptObject() const {
  return QStringLiteral("window.vxOptions = {\n") +
         QStringLiteral("webPlantUml: %1,\n").arg(Utils::boolToString(m_webPlantUml)) +
         QStringLiteral("plantUmlWebService: '%1',\n").arg(m_plantUmlWebService) +
         QStringLiteral("webGraphviz: %1,\n").arg(Utils::boolToString(m_webGraphviz)) +
         QStringLiteral("mathJaxScript: '%1',\n").arg(m_mathJaxScript) +
         QStringLiteral("constrainImageWidthEnabled: %1,\n")
             .arg(Utils::boolToString(m_constrainImageWidthEnabled)) +
         QStringLiteral("imageAlignCenterEnabled: %1,\n")
             .arg(Utils::boolToString(m_imageAlignCenterEnabled)) +
         QStringLiteral("protectFromXss: %1,\n").arg(Utils::boolToString(m_protectFromXss)) +
         QStringLiteral("htmlTagEnabled: %1,\n").arg(Utils::boolToString(m_htmlTagEnabled)) +
         QStringLiteral("autoBreakEnabled: %1,\n").arg(Utils::boolToString(m_autoBreakEnabled)) +
         QStringLiteral("linkifyEnabled: %1,\n").arg(Utils::boolToString(m_linkifyEnabled)) +
         QStringLiteral("indentFirstLineEnabled: %1,\n")
             .arg(Utils::boolToString(m_indentFirstLineEnabled)) +
         QStringLiteral("codeBlockLineNumberEnabled: %1,\n")
             .arg(Utils::boolToString(m_codeBlockLineNumberEnabled)) +
         QStringLiteral("sectionNumberEnabled: %1,\n")
             .arg(Utils::boolToString(m_sectionNumberEnabled)) +
         QStringLiteral("transparentBackgroundEnabled: %1,\n")
             .arg(Utils::boolToString(m_transparentBackgroundEnabled)) +
         QStringLiteral("scrollable: %1,\n").arg(Utils::boolToString(m_scrollable)) +
         QStringLiteral("bodyWidth: %1,\n").arg(m_bodyWidth) +
         QStringLiteral("bodyHeight: %1,\n").arg(m_bodyHeight) +
         QStringLiteral("transformSvgToPngEnabled: %1,\n")
             .arg(Utils::boolToString(m_transformSvgToPngEnabled)) +
         QStringLiteral("mathJaxScale: %1,\n").arg(m_mathJaxScale) +
         QStringLiteral("mathJaxMinScale: %1,\n").arg(m_mathJaxMinScale) +
         QStringLiteral("removeCodeToolBarEnabled: %1,\n")
             .arg(Utils::boolToString(m_removeCodeToolBarEnabled)) +
         QStringLiteral("sectionNumberBaseLevel: %1\n").arg(m_sectionNumberBaseLevel) +
         QStringLiteral("}");
}
