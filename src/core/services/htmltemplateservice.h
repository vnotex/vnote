#ifndef HTMLTEMPLATESERVICE_H
#define HTMLTEMPLATESERVICE_H

#include <QObject>
#include <QString>

#include <core/noncopyable.h>

namespace vnotex {

class ConfigMgr2;
class PdfViewerConfig;
class MarkdownEditorConfig;
class MindMapEditorConfig;
struct WebResource;

// Service for loading and caching HTML templates (PDF viewer, Markdown viewer, MindMap editor).
// Replaces HtmlTemplateHelper's singleton-dependent logic with DI via ConfigMgr2.
//
// Receives ConfigMgr2 via constructor for resolving config file paths.
// Theme-dependent data (style sheet paths) is passed as plain QString parameters
// by the caller (view layer), keeping this service free of GUI dependencies.
//
// Registered in ServiceLocator; all ViewWindow2 subclasses access via
// getServices().get<HtmlTemplateService>().
class HtmlTemplateService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  explicit HtmlTemplateService(ConfigMgr2 *p_configMgr, QObject *p_parent = nullptr);

  // ============ PDF Viewer Template ============

  // Update PDF viewer template from config. No-op if revision unchanged (unless forced).
  void updatePdfViewerTemplate(const PdfViewerConfig &p_config, bool p_force = false);

  // Get the cached PDF viewer template HTML.
  const QString &getPdfViewerTemplate() const;

  // Get the absolute path to the PDF viewer template file.
  const QString &getPdfViewerTemplatePath() const;

  // ============ Markdown Viewer Template ============

  // Update Markdown viewer template from config + theme data.
  // @p_webStyleSheetFile: path from ThemeService::getFile(Theme::File::WebStyleSheet).
  // @p_highlightStyleSheetFile: path from ThemeService::getFile(Theme::File::HighlightStyleSheet).
  void updateMarkdownViewerTemplate(const MarkdownEditorConfig &p_config,
                                    const QString &p_webStyleSheetFile,
                                    const QString &p_highlightStyleSheetFile,
                                    bool p_force = false);

  // Get the cached Markdown viewer template HTML.
  const QString &getMarkdownViewerTemplate() const;

  // ============ MindMap Editor Template ============

  // Update MindMap editor template from config + theme data.
  // @p_webStyleSheetFile: path from ThemeService (or empty).
  void updateMindMapEditorTemplate(const MindMapEditorConfig &p_config,
                                   const QString &p_webStyleSheetFile = QString(),
                                   bool p_force = false);

  // Get the cached MindMap editor template HTML.
  const QString &getMindMapEditorTemplate() const;

private:
  struct Template {
    int m_revision = -1;
    QString m_template;
    QString m_templatePath;
  };

  // Resolve a config-relative path to an absolute file path.
  QString resolveConfigFile(const QString &p_relativePath) const;

  // Read a text file. Returns empty string on failure (logs warning).
  QString readFile(const QString &p_filePath) const;

  static QString errorPage();

  // Fill resource placeholders (styles/scripts) in a template using resolved config paths.
  void fillResources(QString &p_template, const WebResource &p_resource) const;

  // Fill resource placeholders by inlining content (for export templates).
  void fillResourcesByContent(QString &p_template, const WebResource &p_resource) const;

  // Fill global styles (content of "global_styles" resource, inlined).
  void fillGlobalStyles(QString &p_template, const WebResource &p_resource,
                        const QString &p_additionalStyles) const;

  // Generate style tag for a file path.
  static QString fillStyleTag(const QString &p_styleFile);

  // Generate script tag for a file path.
  static QString fillScriptTag(const QString &p_scriptFile);

  // Generate theme style tags from file paths.
  static void fillThemeStyles(QString &p_template, const QString &p_webStyleSheetFile,
                              const QString &p_highlightStyleSheetFile);

  void generatePdfViewerTemplate(const PdfViewerConfig &p_config, Template &p_template) const;

  void generateMindMapEditorTemplate(const MindMapEditorConfig &p_config,
                                     const QString &p_webStyleSheetFile,
                                     Template &p_template) const;

  ConfigMgr2 *m_configMgr = nullptr;

  Template m_pdfViewerTemplate;
  Template m_markdownViewerTemplate;
  Template m_mindMapEditorTemplate;
};

} // namespace vnotex

#endif // HTMLTEMPLATESERVICE_H
