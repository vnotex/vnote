#include "htmltemplatehelper.h"

#include <QDebug>

#include <core/markdowneditorconfig.h>
#include <core/pdfviewerconfig.h>
#include <core/mindmapeditorconfig.h>
#include <core/configmgr.h>
#include <utils/utils.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <utils/htmlutils.h>
#include <core/thememgr.h>
#include <core/vnotex.h>
#include <core/exception.h>

using namespace vnotex;

HtmlTemplateHelper::Template HtmlTemplateHelper::s_markdownViewerTemplate;

HtmlTemplateHelper::Template HtmlTemplateHelper::s_pdfViewerTemplate;

HtmlTemplateHelper::Template HtmlTemplateHelper::s_mindMapEditorTemplate;

QString MarkdownWebGlobalOptions::toJavascriptObject() const
{
    return QStringLiteral("window.vxOptions = {\n")
           + QString("webPlantUml: %1,\n").arg(Utils::boolToString(m_webPlantUml))
           + QString("plantUmlWebService: '%1',\n").arg(m_plantUmlWebService)
           + QString("webGraphviz: %1,\n").arg(Utils::boolToString(m_webGraphviz))
           + QString("mathJaxScript: '%1',\n").arg(m_mathJaxScript)
           + QString("constrainImageWidthEnabled: %1,\n").arg(Utils::boolToString(m_constrainImageWidthEnabled))
           + QString("imageAlignCenterEnabled: %1,\n").arg(Utils::boolToString(m_imageAlignCenterEnabled))
           + QString("protectFromXss: %1,\n").arg(Utils::boolToString(m_protectFromXss))
           + QString("htmlTagEnabled: %1,\n").arg(Utils::boolToString(m_htmlTagEnabled))
           + QString("autoBreakEnabled: %1,\n").arg(Utils::boolToString(m_autoBreakEnabled))
           + QString("linkifyEnabled: %1,\n").arg(Utils::boolToString(m_linkifyEnabled))
           + QString("indentFirstLineEnabled: %1,\n").arg(Utils::boolToString(m_indentFirstLineEnabled))
           + QString("codeBlockLineNumberEnabled: %1,\n").arg(Utils::boolToString(m_codeBlockLineNumberEnabled))
           + QString("sectionNumberEnabled: %1,\n").arg(Utils::boolToString(m_sectionNumberEnabled))
           + QString("transparentBackgroundEnabled: %1,\n").arg(Utils::boolToString(m_transparentBackgroundEnabled))
           + QString("scrollable: %1,\n").arg(Utils::boolToString(m_scrollable))
           + QString("bodyWidth: %1,\n").arg(m_bodyWidth)
           + QString("bodyHeight: %1,\n").arg(m_bodyHeight)
           + QString("transformSvgToPngEnabled: %1,\n").arg(Utils::boolToString(m_transformSvgToPngEnabled))
           + QString("mathJaxScale: %1,\n").arg(m_mathJaxScale)
           + QString("removeCodeToolBarEnabled: %1,\n").arg(Utils::boolToString(m_removeCodeToolBarEnabled))
           + QString("sectionNumberBaseLevel: %1\n").arg(m_sectionNumberBaseLevel)
           + QStringLiteral("}");
}

// Read "global_styles" from resource and fill the holder with the content.
static void fillGlobalStyles(QString &p_template, const WebResource &p_resource, const QString &p_additionalStyles)
{
    QString styles;
    for (const auto &ele : p_resource.m_resources) {
        if (ele.isGlobal()) {
            if (ele.m_enabled) {
                for (const auto &style : ele.m_styles) {
                    // Read the style file content.
                    auto styleFile = ConfigMgr::getInst().getUserOrAppFile(style);
                    try {
                        styles += FileUtils::readTextFile(styleFile);
                    } catch (Exception &p_e) {
                        qWarning() << "failed to read global styles" << ele.m_name << styleFile << p_e.what();
                    }
                }
            }
            break;
        }
    }

    styles += p_additionalStyles;

    if (!styles.isEmpty()) {
        p_template.replace("/* VX_GLOBAL_STYLES_PLACEHOLDER */", styles);
    }
}

static QString fillStyleTag(const QString &p_styleFile)
{
    if (p_styleFile.isEmpty()) {
        return "";
    }
    auto url = PathUtils::pathToUrl(p_styleFile);
    return QString("<link rel=\"stylesheet\" type=\"text/css\" href=\"%1\">\n").arg(url.toString());
}

static QString fillScriptTag(const QString &p_scriptFile)
{
    if (p_scriptFile.isEmpty()) {
        return "";
    }
    auto url = PathUtils::pathToUrl(p_scriptFile);
    return QString("<script type=\"text/javascript\" src=\"%1\"></script>\n").arg(url.toString());
}

static void fillThemeStyles(QString &p_template, const QString &p_webStyleSheetFile, const QString &p_highlightStyleSheetFile)
{
    QString styles;
    styles += fillStyleTag(p_webStyleSheetFile);
    styles += fillStyleTag(p_highlightStyleSheetFile);

    if (!styles.isEmpty()) {
        p_template.replace(QStringLiteral("<!-- VX_THEME_STYLES_PLACEHOLDER -->"),
                           styles);
    }
}

static void fillGlobalOptions(QString &p_template, const MarkdownWebGlobalOptions &p_opts)
{
    p_template.replace(QStringLiteral("/* VX_GLOBAL_OPTIONS_PLACEHOLDER */"),
                       p_opts.toJavascriptObject());
}

// Read all other resources in @p_resource and fill the holder with proper resource path.
static void fillResources(QString &p_template, const WebResource &p_resource)
{
    QString styles;
    QString scripts;

    for (const auto &ele : p_resource.m_resources) {
        if (ele.m_enabled && !ele.isGlobal()) {
            // Styles.
            for (const auto &style : ele.m_styles) {
                auto styleFile = ConfigMgr::getInst().getUserOrAppFile(style);
                styles += fillStyleTag(styleFile);
            }

            // Scripts.
            for (const auto &script : ele.m_scripts) {
                auto scriptFile = ConfigMgr::getInst().getUserOrAppFile(script);
                scripts += fillScriptTag(scriptFile);
            }
        }
    }

    if (!styles.isEmpty()) {
        p_template.replace(QStringLiteral("<!-- VX_STYLES_PLACEHOLDER -->"),
                           styles);
    }

    if (!scripts.isEmpty()) {
        p_template.replace(QStringLiteral("<!-- VX_SCRIPTS_PLACEHOLDER -->"),
                           scripts);
    }
}

static void fillResourcesByContent(QString &p_template, const WebResource &p_resource)
{
    QString styles;
    QString scripts;

    for (const auto &ele : p_resource.m_resources) {
        if (ele.m_enabled && !ele.isGlobal()) {
            try {
                // Styles.
                for (const auto &style : ele.m_styles) {
                    auto styleFile = ConfigMgr::getInst().getUserOrAppFile(style);
                    styles += FileUtils::readTextFile(styleFile);
                }

                // Scripts.
                for (const auto &script : ele.m_scripts) {
                    auto scriptFile = ConfigMgr::getInst().getUserOrAppFile(script);
                    scripts += FileUtils::readTextFile(scriptFile);
                }
            } catch (Exception &p_e) {
                qWarning() << "failed to read resource" << ele.m_name << p_e.what();
            }
        }
    }

    if (!styles.isEmpty()) {
        p_template.replace(QStringLiteral("/* VX_STYLES_PLACEHOLDER */"), styles);
    }

    if (!scripts.isEmpty()) {
        p_template.replace(QStringLiteral("/* VX_SCRIPTS_PLACEHOLDER */"), scripts);
    }
}

const QString &HtmlTemplateHelper::getMarkdownViewerTemplate()
{
    return s_markdownViewerTemplate.m_template;
}

void HtmlTemplateHelper::updateMarkdownViewerTemplate(const MarkdownEditorConfig &p_config, bool p_force)
{
    if (!p_force && p_config.revision() == s_markdownViewerTemplate.m_revision) {
        return;
    }

    s_markdownViewerTemplate.m_revision = p_config.revision();

    MarkdownParas paras;
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    paras.m_webStyleSheetFile = themeMgr.getFile(Theme::File::WebStyleSheet);
    paras.m_highlightStyleSheetFile = themeMgr.getFile(Theme::File::HighlightStyleSheet);

    s_markdownViewerTemplate.m_template = generateMarkdownViewerTemplate(p_config, paras);
}

QString HtmlTemplateHelper::generateMarkdownViewerTemplate(const MarkdownEditorConfig &p_config,
                                                           const MarkdownParas &p_paras)
{
    const auto &viewerResource = p_config.getViewerResource();
    const auto templateFile = ConfigMgr::getInst().getUserOrAppFile(viewerResource.m_template);
    QString htmlTemplate;
    try {
        htmlTemplate = FileUtils::readTextFile(templateFile);
    } catch (Exception &p_e) {
        qWarning() << "failed to read HTML template" << templateFile << p_e.what();
        return errorPage();
    }

    fillGlobalStyles(htmlTemplate, viewerResource, "");

    fillThemeStyles(htmlTemplate, p_paras.m_webStyleSheetFile, p_paras.m_highlightStyleSheetFile);

    {
        MarkdownWebGlobalOptions opts;
        opts.m_webPlantUml = p_config.getWebPlantUml();
        opts.m_plantUmlWebService = p_config.getPlantUmlWebService();
        opts.m_webGraphviz = p_config.getWebGraphviz();
        opts.m_mathJaxScript = p_config.getMathJaxScript();
        opts.m_sectionNumberEnabled = p_config.getSectionNumberMode() == MarkdownEditorConfig::SectionNumberMode::Read;
        opts.m_sectionNumberBaseLevel = p_config.getSectionNumberBaseLevel();
        opts.m_constrainImageWidthEnabled = p_config.getConstrainImageWidthEnabled();
        opts.m_imageAlignCenterEnabled = p_config.getImageAlignCenterEnabled();
        opts.m_protectFromXss = p_config.getProtectFromXss();
        opts.m_htmlTagEnabled = p_config.getHtmlTagEnabled();
        opts.m_autoBreakEnabled = p_config.getAutoBreakEnabled();
        opts.m_linkifyEnabled = p_config.getLinkifyEnabled();
        opts.m_indentFirstLineEnabled = p_config.getIndentFirstLineEnabled();
        opts.m_codeBlockLineNumberEnabled = p_config.getCodeBlockLineNumberEnabled();
        opts.m_transparentBackgroundEnabled = p_paras.m_transparentBackgroundEnabled;
        opts.m_scrollable = p_paras.m_scrollable;
        opts.m_bodyWidth = p_paras.m_bodyWidth;
        opts.m_bodyHeight = p_paras.m_bodyHeight;
        opts.m_transformSvgToPngEnabled = p_paras.m_transformSvgToPngEnabled;
        opts.m_mathJaxScale = p_paras.m_mathJaxScale;
        opts.m_removeCodeToolBarEnabled = p_paras.m_removeCodeToolBarEnabled;
        fillGlobalOptions(htmlTemplate, opts);
    }

    fillResources(htmlTemplate, viewerResource);

    return htmlTemplate;
}

QString HtmlTemplateHelper::generateMarkdownExportTemplate(const MarkdownEditorConfig &p_config,
                                                           bool p_addOutlinePanel)
{
    auto exportResource = p_config.getExportResource();
    const auto templateFile = ConfigMgr::getInst().getUserOrAppFile(exportResource.m_template);
    QString htmlTemplate;
    try {
        htmlTemplate = FileUtils::readTextFile(templateFile);
    } catch (Exception &p_e) {
        qWarning() << "failed to read Markdown export HTML template" << templateFile << p_e.what();
        return errorPage();
    }

    fillGlobalStyles(htmlTemplate, exportResource, "");

    fillOutlinePanel(htmlTemplate, exportResource, p_addOutlinePanel);

    fillResourcesByContent(htmlTemplate, exportResource);

    return htmlTemplate;
}

void HtmlTemplateHelper::fillOutlinePanel(QString &p_template, WebResource &p_exportResource, bool p_addOutlinePanel)
{
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

void HtmlTemplateHelper::fillTitle(QString &p_template, const QString &p_title)
{
    if (!p_title.isEmpty()) {
        p_template.replace("<!-- VX_TITLE_PLACEHOLDER -->",
                           QString("<title>%1</title>").arg(HtmlUtils::escapeHtml(p_title)));
    }
}

void HtmlTemplateHelper::fillStyleContent(QString &p_template, const QString &p_styles)
{
    p_template.replace("/* VX_STYLES_CONTENT_PLACEHOLDER */", p_styles);
}

void HtmlTemplateHelper::fillHeadContent(QString &p_template, const QString &p_head)
{
    p_template.replace("<!-- VX_HEAD_PLACEHOLDER -->", p_head);
}

void HtmlTemplateHelper::fillContent(QString &p_template, const QString &p_content)
{
    p_template.replace("<!-- VX_CONTENT_PLACEHOLDER -->", p_content);
}

void HtmlTemplateHelper::fillBodyClassList(QString &p_template, const QString &p_classList)
{
    p_template.replace("<!-- VX_BODY_CLASS_LIST_PLACEHOLDER -->", p_classList);
}

QString HtmlTemplateHelper::errorPage()
{
    return VNoteX::tr("Failed to load HTML template. Check the logs for details. "
                      "Try deleting the user configuration file and the default configuration file.");
}

const QString &HtmlTemplateHelper::getPdfViewerTemplate()
{
    return s_pdfViewerTemplate.m_template;
}

const QString &HtmlTemplateHelper::getPdfViewerTemplatePath()
{
    return s_pdfViewerTemplate.m_templatePath;
}

void HtmlTemplateHelper::updatePdfViewerTemplate(const PdfViewerConfig &p_config, bool p_force)
{
    if (!p_force && p_config.revision() == s_pdfViewerTemplate.m_revision) {
        return;
    }

    s_pdfViewerTemplate.m_revision = p_config.revision();
    generatePdfViewerTemplate(p_config, s_pdfViewerTemplate);
}

void HtmlTemplateHelper::generatePdfViewerTemplate(const PdfViewerConfig &p_config, Template& p_template)
{
    const auto &viewerResource = p_config.getViewerResource();
    p_template.m_templatePath = ConfigMgr::getInst().getUserOrAppFile(viewerResource.m_template);
    try {
        p_template.m_template = FileUtils::readTextFile(p_template.m_templatePath);
    } catch (Exception &p_e) {
        qWarning() << "failed to read HTML template" << p_template.m_templatePath << p_e.what();
        p_template.m_template = errorPage();
        return;
    }

    fillResources(p_template.m_template, viewerResource);
}

const QString &HtmlTemplateHelper::getMindMapEditorTemplate()
{
    return s_mindMapEditorTemplate.m_template;
}

void HtmlTemplateHelper::updateMindMapEditorTemplate(const MindMapEditorConfig &p_config, bool p_force)
{
    if (!p_force && p_config.revision() == s_mindMapEditorTemplate.m_revision) {
        return;
    }

    s_mindMapEditorTemplate.m_revision = p_config.revision();

    generateMindMapEditorTemplate(p_config,
                                  QString() /* Use empty theme style for now */,
                                  s_mindMapEditorTemplate);
}

void HtmlTemplateHelper::generateMindMapEditorTemplate(const MindMapEditorConfig &p_config,
                                                       const QString &p_webStyleSheetFile,
                                                       Template& p_template)
{
    const auto &editorResource = p_config.getEditorResource();
    p_template.m_templatePath = ConfigMgr::getInst().getUserOrAppFile(editorResource.m_template);
    try {
        p_template.m_template = FileUtils::readTextFile(p_template.m_templatePath);
    } catch (Exception &p_e) {
        qWarning() << "failed to read HTML template" << p_template.m_templatePath << p_e.what();
        p_template.m_template = errorPage();
        return;
    }

    fillThemeStyles(p_template.m_template, p_webStyleSheetFile, QString());

    fillResources(p_template.m_template, editorResource);
}
