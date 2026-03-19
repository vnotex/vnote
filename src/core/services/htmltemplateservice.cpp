#include "htmltemplateservice.h"

#include <QDebug>

#include <core/configmgr2.h>
#include <core/exception.h>
#include <core/htmltemplatehelper.h>
#include <core/markdowneditorconfig.h>
#include <core/mindmapeditorconfig.h>
#include <core/pdfviewerconfig.h>
#include <core/webresource.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>

using namespace vnotex;

HtmlTemplateService::HtmlTemplateService(ConfigMgr2 *p_configMgr, QObject *p_parent)
    : QObject(p_parent), m_configMgr(p_configMgr) {}

// ============ Helpers ============

QString HtmlTemplateService::resolveConfigFile(const QString &p_relativePath) const {
  return m_configMgr->getFileFromConfigFolder(p_relativePath);
}

QString HtmlTemplateService::readFile(const QString &p_filePath) const {
  try {
    return FileUtils::readTextFile(p_filePath);
  } catch (Exception &p_e) {
    qWarning() << "HtmlTemplateService: failed to read file" << p_filePath << p_e.what();
    return QString();
  }
}

QString HtmlTemplateService::errorPage() {
  return QStringLiteral("Failed to load HTML template. Check the logs for details. "
                        "Try deleting the user configuration file and the default configuration file.");
}

QString HtmlTemplateService::fillStyleTag(const QString &p_styleFile) {
  if (p_styleFile.isEmpty()) {
    return QString();
  }
  auto url = PathUtils::pathToUrl(p_styleFile);
  return QStringLiteral("<link rel=\"stylesheet\" type=\"text/css\" href=\"%1\">\n")
      .arg(url.toString());
}

QString HtmlTemplateService::fillScriptTag(const QString &p_scriptFile) {
  if (p_scriptFile.isEmpty()) {
    return QString();
  }
  auto url = PathUtils::pathToUrl(p_scriptFile);
  return QStringLiteral("<script type=\"text/javascript\" src=\"%1\"></script>\n")
      .arg(url.toString());
}

void HtmlTemplateService::fillThemeStyles(QString &p_template, const QString &p_webStyleSheetFile,
                                          const QString &p_highlightStyleSheetFile) {
  QString styles;
  styles += fillStyleTag(p_webStyleSheetFile);
  styles += fillStyleTag(p_highlightStyleSheetFile);

  if (!styles.isEmpty()) {
    p_template.replace(QStringLiteral("<!-- VX_THEME_STYLES_PLACEHOLDER -->"), styles);
  }
}

void HtmlTemplateService::fillGlobalStyles(QString &p_template, const WebResource &p_resource,
                                           const QString &p_additionalStyles) const {
  QString styles;
  for (const auto &ele : p_resource.m_resources) {
    if (ele.isGlobal()) {
      if (ele.m_enabled) {
        for (const auto &style : ele.m_styles) {
          auto styleFile = resolveConfigFile(style);
          auto content = readFile(styleFile);
          if (!content.isEmpty()) {
            styles += content;
          }
        }
      }
      break;
    }
  }

  styles += p_additionalStyles;

  if (!styles.isEmpty()) {
    p_template.replace(QStringLiteral("/* VX_GLOBAL_STYLES_PLACEHOLDER */"), styles);
  }
}

void HtmlTemplateService::fillResources(QString &p_template, const WebResource &p_resource) const {
  QString styles;
  QString scripts;

  for (const auto &ele : p_resource.m_resources) {
    if (ele.m_enabled && !ele.isGlobal()) {
      for (const auto &style : ele.m_styles) {
        auto styleFile = resolveConfigFile(style);
        styles += fillStyleTag(styleFile);
      }
      for (const auto &script : ele.m_scripts) {
        auto scriptFile = resolveConfigFile(script);
        scripts += fillScriptTag(scriptFile);
      }
    }
  }

  if (!styles.isEmpty()) {
    p_template.replace(QStringLiteral("<!-- VX_STYLES_PLACEHOLDER -->"), styles);
  }
  if (!scripts.isEmpty()) {
    p_template.replace(QStringLiteral("<!-- VX_SCRIPTS_PLACEHOLDER -->"), scripts);
  }
}

void HtmlTemplateService::fillResourcesByContent(QString &p_template,
                                                 const WebResource &p_resource) const {
  QString styles;
  QString scripts;

  for (const auto &ele : p_resource.m_resources) {
    if (ele.m_enabled && !ele.isGlobal()) {
      for (const auto &style : ele.m_styles) {
        auto styleFile = resolveConfigFile(style);
        auto content = readFile(styleFile);
        if (!content.isEmpty()) {
          styles += content;
        }
      }
      for (const auto &script : ele.m_scripts) {
        auto scriptFile = resolveConfigFile(script);
        auto content = readFile(scriptFile);
        if (!content.isEmpty()) {
          scripts += content;
        }
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

// ============ PDF Viewer Template ============

void HtmlTemplateService::updatePdfViewerTemplate(const PdfViewerConfig &p_config, bool p_force) {
  if (!p_force && p_config.revision() == m_pdfViewerTemplate.m_revision) {
    return;
  }

  m_pdfViewerTemplate.m_revision = p_config.revision();
  generatePdfViewerTemplate(p_config, m_pdfViewerTemplate);
}

const QString &HtmlTemplateService::getPdfViewerTemplate() const {
  return m_pdfViewerTemplate.m_template;
}

const QString &HtmlTemplateService::getPdfViewerTemplatePath() const {
  return m_pdfViewerTemplate.m_templatePath;
}

void HtmlTemplateService::generatePdfViewerTemplate(const PdfViewerConfig &p_config,
                                                    Template &p_template) const {
  const auto &viewerResource = p_config.getViewerResource();
  p_template.m_templatePath = resolveConfigFile(viewerResource.m_template);

  p_template.m_template = readFile(p_template.m_templatePath);
  if (p_template.m_template.isEmpty()) {
    p_template.m_template = errorPage();
    return;
  }

  fillResources(p_template.m_template, viewerResource);
}

// ============ Markdown Viewer Template ============

void HtmlTemplateService::updateMarkdownViewerTemplate(const MarkdownEditorConfig &p_config,
                                                       const QString &p_webStyleSheetFile,
                                                       const QString &p_highlightStyleSheetFile,
                                                       bool p_force) {
  if (!p_force && p_config.revision() == m_markdownViewerTemplate.m_revision) {
    return;
  }

  m_markdownViewerTemplate.m_revision = p_config.revision();

  const auto &viewerResource = p_config.getViewerResource();
  auto templateFile = resolveConfigFile(viewerResource.m_template);

  m_markdownViewerTemplate.m_templatePath = templateFile;
  m_markdownViewerTemplate.m_template = readFile(templateFile);
  if (m_markdownViewerTemplate.m_template.isEmpty()) {
    m_markdownViewerTemplate.m_template = errorPage();
    return;
  }

  fillGlobalStyles(m_markdownViewerTemplate.m_template, viewerResource, QString());
  fillThemeStyles(m_markdownViewerTemplate.m_template, p_webStyleSheetFile,
                  p_highlightStyleSheetFile);

  {
    MarkdownWebGlobalOptions opts;
    opts.m_webPlantUml = p_config.getWebPlantUml();
    opts.m_plantUmlWebService = p_config.getPlantUmlWebService();
    opts.m_webGraphviz = p_config.getWebGraphviz();
    opts.m_mathJaxScript = p_config.getMathJaxScript();
    opts.m_sectionNumberEnabled =
        p_config.getSectionNumberMode() == MarkdownEditorConfig::SectionNumberMode::Read;
    opts.m_sectionNumberBaseLevel = p_config.getSectionNumberBaseLevel();
    opts.m_constrainImageWidthEnabled = p_config.getConstrainImageWidthEnabled();
    opts.m_imageAlignCenterEnabled = p_config.getImageAlignCenterEnabled();
    opts.m_protectFromXss = p_config.getProtectFromXss();
    opts.m_htmlTagEnabled = p_config.getHtmlTagEnabled();
    opts.m_autoBreakEnabled = p_config.getAutoBreakEnabled();
    opts.m_linkifyEnabled = p_config.getLinkifyEnabled();
    opts.m_indentFirstLineEnabled = p_config.getIndentFirstLineEnabled();
    opts.m_codeBlockLineNumberEnabled = p_config.getCodeBlockLineNumberEnabled();
    m_markdownViewerTemplate.m_template.replace(
        QStringLiteral("/* VX_GLOBAL_OPTIONS_PLACEHOLDER */"), opts.toJavascriptObject());
  }

  fillResources(m_markdownViewerTemplate.m_template, viewerResource);
}

const QString &HtmlTemplateService::getMarkdownViewerTemplate() const {
  return m_markdownViewerTemplate.m_template;
}

// ============ MindMap Editor Template ============

void HtmlTemplateService::updateMindMapEditorTemplate(const MindMapEditorConfig &p_config,
                                                      const QString &p_webStyleSheetFile,
                                                      bool p_force) {
  if (!p_force && p_config.revision() == m_mindMapEditorTemplate.m_revision) {
    return;
  }

  m_mindMapEditorTemplate.m_revision = p_config.revision();
  generateMindMapEditorTemplate(p_config, p_webStyleSheetFile, m_mindMapEditorTemplate);
}

const QString &HtmlTemplateService::getMindMapEditorTemplate() const {
  return m_mindMapEditorTemplate.m_template;
}

void HtmlTemplateService::generateMindMapEditorTemplate(const MindMapEditorConfig &p_config,
                                                        const QString &p_webStyleSheetFile,
                                                        Template &p_template) const {
  const auto &editorResource = p_config.getEditorResource();
  p_template.m_templatePath = resolveConfigFile(editorResource.m_template);

  p_template.m_template = readFile(p_template.m_templatePath);
  if (p_template.m_template.isEmpty()) {
    p_template.m_template = errorPage();
    return;
  }

  fillThemeStyles(p_template.m_template, p_webStyleSheetFile, QString());
  fillResources(p_template.m_template, editorResource);
}
