#include "webviewexporter.h"

#include "imgsrcrewriter.h"

#include "exportstyleresolver.h"
#include "wkhtmltopdfhtmlpatch.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QWebEnginePage>
#include <QWidget>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/exception.h>
#include <core/htmltemplateutils.h>
#include <core/markdowneditorconfig.h>
#include <core/markdownwebglobaloptions.h>
#include <core/servicelocator.h>
#include <core/services/htmltemplateservice.h>
#include <gui/services/themeservice.h>
#include <gui/utils/themeutils.h>
#include <utils/fileutils2.h>
#include <utils/htmlutils.h>
#include <utils/pathutils.h>
#include <utils/utils.h>
#include <utils/webutils.h>
#include <widgets/editors/graphvizhelper.h>
#include <widgets/editors/markdownviewer.h>
#include <widgets/editors/markdownvieweradapter.h>
#include <widgets/editors/plantumlhelper.h>
using namespace vnotex;

// image-parser-allow: this scans RENDERED html produced by the web view, not
// note source. Note-source `<img>` parsing has exactly one implementation,
// vte::scanHtmlImgTags()
// (libs/vtextedit/src/include/vtextedit/htmlimgscanner.h). The `([^>]*)` groups
// around `src` are what preserve every other attribute -- including the
// `width` / `height` a sized image carries -- across the rewrites below.

namespace {

QString resolveConfigFile(ConfigMgr2 &p_configMgr, const QString &p_filePath) {
  return p_configMgr.getFileFromConfigFolder(p_filePath);
}

QString readTemplateFile(const QString &p_filePath, const QString &p_logPrefix) {
  QString text;
  Error err = FileUtils2::readTextFile(p_filePath, &text);
  if (err) {
    qWarning() << p_logPrefix << p_filePath << err.what();
    return QString();
  }
  return text;
}

QString errorPage() {
  return QStringLiteral("Failed to load HTML template. Check the logs for details. "
                        "Try deleting the user configuration file and the default configuration "
                        "file.");
}

QString fillStyleTag(const QString &p_styleFile) {
  if (p_styleFile.isEmpty()) {
    return QString();
  }

  const auto url = PathUtils::pathToUrl(p_styleFile);
  return QStringLiteral("<link rel=\"stylesheet\" type=\"text/css\" href=\"%1\">\n")
      .arg(url.toString());
}

QString fillScriptTag(const QString &p_scriptFile) {
  if (p_scriptFile.isEmpty()) {
    return QString();
  }

  const auto url = PathUtils::pathToUrl(p_scriptFile);
  return QStringLiteral("<script type=\"text/javascript\" src=\"%1\"></script>\n")
      .arg(url.toString());
}

void fillGlobalOptions(QString &p_template, const MarkdownWebGlobalOptions &p_opts) {
  p_template.replace(QStringLiteral("/* VX_GLOBAL_OPTIONS_PLACEHOLDER */"),
                     p_opts.toJavascriptObject());
}

void fillGlobalStyles(QString &p_template, const WebResource &p_resource, ConfigMgr2 &p_configMgr,
                      const QString &p_additionalStyles) {
  QString styles;
  for (const auto &ele : p_resource.m_resources) {
    if (ele.isGlobal()) {
      if (ele.m_enabled) {
        for (const auto &style : ele.m_styles) {
          const auto styleFile = resolveConfigFile(p_configMgr, style);
          const auto content = readTemplateFile(styleFile, "failed to read global styles");
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

void fillResources(QString &p_template, const WebResource &p_resource, ConfigMgr2 &p_configMgr) {
  QString styles;
  QString scripts;

  for (const auto &ele : p_resource.m_resources) {
    if (ele.m_enabled && !ele.isGlobal()) {
      for (const auto &style : ele.m_styles) {
        styles += fillStyleTag(resolveConfigFile(p_configMgr, style));
      }

      for (const auto &script : ele.m_scripts) {
        scripts += fillScriptTag(resolveConfigFile(p_configMgr, script));
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

void fillResourcesByContent(QString &p_template, const WebResource &p_resource,
                            ConfigMgr2 &p_configMgr) {
  QString styles;
  QString scripts;

  for (const auto &ele : p_resource.m_resources) {
    if (ele.m_enabled && !ele.isGlobal()) {
      for (const auto &style : ele.m_styles) {
        const auto styleFile = resolveConfigFile(p_configMgr, style);
        const auto content = readTemplateFile(styleFile, "failed to read resource");
        if (!content.isEmpty()) {
          styles += content;
        }
      }

      for (const auto &script : ele.m_scripts) {
        const auto scriptFile = resolveConfigFile(p_configMgr, script);
        const auto content = readTemplateFile(scriptFile, "failed to read resource");
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

QString generateMarkdownViewerTemplate(ConfigMgr2 &p_configMgr,
                                       const MarkdownEditorConfig &p_config,
                                       const HtmlTemplateUtils::MarkdownParas &p_paras,
                                       const QString &p_webStyleContent) {
  const auto &viewerResource = p_config.getViewerResource();
  const auto templateFile = resolveConfigFile(p_configMgr, viewerResource.m_template);
  auto htmlTemplate = readTemplateFile(templateFile, "failed to read HTML template");
  if (htmlTemplate.isEmpty()) {
    return errorPage();
  }

  fillGlobalStyles(htmlTemplate, viewerResource, p_configMgr, QString());
  if (!p_paras.m_webStyleSheetFile.isEmpty()) {
    // Standalone custom CSS keeps its original relative-resource base.
    const auto placeholder = QStringLiteral("<!-- VX_THEME_STYLES_PLACEHOLDER -->");
    htmlTemplate.replace(placeholder, fillStyleTag(p_paras.m_webStyleSheetFile) + placeholder);
  }
  HtmlTemplateService::fillThemeStylesWithContent(htmlTemplate, p_webStyleContent,
                                                  p_paras.m_highlightStyleSheetFile);

  MarkdownWebGlobalOptions opts;
  opts.m_webPlantUml = p_config.getWebPlantUml();
  opts.m_plantUmlFormat = p_config.getPlantUmlFormat();
  opts.m_plantUmlWebService = p_config.getPlantUmlWebService();
  opts.m_webGraphviz = p_config.getWebGraphviz();
  opts.m_mathRenderer = p_config.getMathRenderer();
  opts.m_mathJaxScript = p_config.getMathJaxScript();
  opts.m_constrainImageWidthEnabled = p_config.getConstrainImageWidthEnabled();
  opts.m_imageAlignCenterEnabled = p_config.getImageAlignCenterEnabled();
  opts.m_headingFoldingEnabled = p_paras.m_headingFoldingEnabled;
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

  fillResources(htmlTemplate, viewerResource, p_configMgr);
  return htmlTemplate;
}

QString generateMarkdownExportTemplate(ConfigMgr2 &p_configMgr,
                                       const MarkdownEditorConfig &p_config, bool p_addOutlinePanel,
                                       bool p_headingFoldingEnabled) {
  auto exportResource = p_config.getExportResource();
  const auto templateFile = resolveConfigFile(p_configMgr, exportResource.m_template);
  auto htmlTemplate =
      readTemplateFile(templateFile, "failed to read Markdown export HTML template");
  if (htmlTemplate.isEmpty()) {
    return errorPage();
  }

  fillGlobalStyles(htmlTemplate, exportResource, p_configMgr, QString());
  HtmlTemplateUtils::fillOutlinePanel(htmlTemplate, exportResource, p_addOutlinePanel);
  htmlTemplate.replace(QStringLiteral("/* VX_SCRIPTS_PLACEHOLDER */"),
                       WebUtils::translationScript() +
                           QStringLiteral("\n/* VX_SCRIPTS_PLACEHOLDER */"));

  // Always inject the shared code toolbar and self-contained page handlers, independent
  // of the user-mutable exportResource config (existing persisted config would otherwise omit
  // them). Injected ahead of the placeholder so fillResourcesByContent() still appends any
  // config resources.
  {
    const auto copyScript = readTemplateFile(
        resolveConfigFile(p_configMgr, QStringLiteral("web/js/codeblockactions.js")),
        "failed to read code-block toolbar script");
    if (!copyScript.isEmpty()) {
      htmlTemplate.replace(QStringLiteral("/* VX_SCRIPTS_PLACEHOLDER */"),
                           copyScript + QStringLiteral("\n/* VX_SCRIPTS_PLACEHOLDER */"));
    }

    const auto linkFallbackScript = readTemplateFile(
        resolveConfigFile(p_configMgr, QStringLiteral("web/js/exportlinkfallback.js")),
        "failed to read export link fallback script");
    if (!linkFallbackScript.isEmpty()) {
      htmlTemplate.replace(QStringLiteral("/* VX_SCRIPTS_PLACEHOLDER */"),
                           linkFallbackScript + QStringLiteral("\n/* VX_SCRIPTS_PLACEHOLDER */"));
    }

    if (p_headingFoldingEnabled) {
      const auto headingFoldingScript = readTemplateFile(
          resolveConfigFile(p_configMgr, QStringLiteral("web/js/nodelinemapper.js")),
          "failed to read heading folding script");
      if (!headingFoldingScript.isEmpty()) {
        htmlTemplate.replace(QStringLiteral("/* VX_SCRIPTS_PLACEHOLDER */"),
                             headingFoldingScript +
                                 QStringLiteral("\n/* VX_SCRIPTS_PLACEHOLDER */"));
      }
    }

    const auto copyStyle = readTemplateFile(
        resolveConfigFile(p_configMgr, QStringLiteral("web/css/codeblockactions.css")),
        "failed to read code-block toolbar style");
    if (!copyStyle.isEmpty()) {
      htmlTemplate.replace(QStringLiteral("/* VX_STYLES_PLACEHOLDER */"),
                           copyStyle + QStringLiteral("\n/* VX_STYLES_PLACEHOLDER */"));
    }
  }

  fillResourcesByContent(htmlTemplate, exportResource, p_configMgr);
  return htmlTemplate;
}

} // namespace

WebViewExporter::WebViewExporter(ServiceLocator &p_services, QWidget *p_widgetParent,
                                 QObject *p_parent)
    : ExportRenderer(p_services, p_widgetParent, p_parent) {}

WebViewExporter::~WebViewExporter() { clear(); }

void WebViewExporter::clear() {
  m_askedToStop = false;

  delete m_viewer;
  m_viewer = nullptr;

  m_htmlTemplate.clear();
  m_exportHtmlTemplate.clear();
  m_syntaxHighlightStyleFile.clear();

  m_exportOngoing = false;
}

bool WebViewExporter::renderMarkdown(const ExportInput &p_input, ExportMarkdown &p_output) {
  p_output.content = p_input.content;
  p_output.fileName = p_input.file.fileName.isEmpty() ? QFileInfo(p_input.file.filePath).fileName()
                                                      : p_input.file.fileName;
  p_output.copyMedia =
      p_input.file.fileTypeName.compare(QStringLiteral("Markdown"), Qt::CaseInsensitive) == 0;
  return true;
}

bool WebViewExporter::renderHtml(const ExportInput &p_input, const ExportHtmlRequest &p_request,
                                 const QString &p_outputFile) {
  Q_ASSERT(!m_exportOngoing);
  // A template contains request-specific geometry, scripts and rendering flags. Recreate the
  // hidden page so neither an incompatible request nor an earlier document can leak into it.
  clear();
  if (!m_services.get<ThemeService>() || !m_services.get<ConfigMgr2>()) {
    emit logRequested(tr("Export rendering services are unavailable."));
    return false;
  }
  const auto &option = p_request.options;
  if (option.m_htmlOption.m_useMimeHtmlFormat) {
    emit logRequested(tr("MIME HTML export is not supported."));
    return false;
  }
  prepare(p_request);
  m_exportOngoing = true;
  m_webViewStates = WebViewState::Started;

  const auto &file = p_input.file;
  const auto contentPath = file.filePath.isEmpty() ? file.resourcePath : file.filePath;
  auto baseUrl = PathUtils::pathToUrl(contentPath);
  m_viewer->adapter()->reset();
  m_viewer->setHtml(m_htmlTemplate, baseUrl);

  const bool addToc =
      p_request.purpose == ExportHtmlPurpose::Pdf && option.m_pdfOption.m_addTableOfContents &&
      !option.m_pdfOption.m_useWkhtmltopdf &&
      file.fileTypeName.compare(QStringLiteral("Markdown"), Qt::CaseInsensitive) == 0;
  m_viewer->adapter()->setText(addToc ? QStringLiteral("[TOC]\n\n") + p_input.content
                                      : p_input.content);

  QElapsedTimer timer;
  timer.start();
  while (!isWebViewReady()) {
    Utils::sleepWait(100);
    if (m_askedToStop || isWebViewFailed() || timer.hasExpired(30000)) {
      emit logRequested(
          tr("Failed or cancelled while preparing HTML for (%1).").arg(file.fileName));
      m_exportOngoing = false;
      return false;
    }
  }

  // Let the diagram adapters finish their queued work before serializing the live DOM.
  Utils::sleepWait(200);
  RasterFlags rasterFlags;
  rasterFlags.setFlag(RasterizeMath, option.m_rasterizeMathEnabled);
  rasterFlags.setFlag(RasterizeDiagrams, option.m_rasterizeDiagramsEnabled);
  const bool success =
      !m_askedToStop && doExportHtml(option.m_htmlOption, p_outputFile, baseUrl, rasterFlags,
                                     option.m_pdfOption.m_layout.data());
  m_exportOngoing = false;
  return success;
}

void WebViewExporter::stop() { m_askedToStop = true; }

bool WebViewExporter::isWebViewReady() const {
  return m_webViewStates == (WebViewState::LoadFinished | WebViewState::WorkFinished);
}

bool WebViewExporter::isWebViewFailed() const { return m_webViewStates & WebViewState::Failed; }

bool WebViewExporter::doExportHtml(const ExportHtmlOption &p_htmlOption,
                                   const QString &p_outputFile, const QUrl &p_baseUrl,
                                   RasterFlags p_rasterFlags, const QPageLayout *p_layout) {
  ExportState state = ExportState::Busy;

  const auto connection =
      connect(
          m_viewer->adapter(), &MarkdownViewerAdapter::contentReady, this,
          [&, this](const QString &p_headContent, const QString &p_styleContent,
                    const QString &p_content, const QString &p_bodyClassList) {
            qDebug() << "doExportHtml contentReady";
            // Maybe unnecessary. Just to avoid duplicated signal connections.
            disconnect(m_viewer->adapter(), &MarkdownViewerAdapter::contentReady, this, 0);

            if (m_askedToStop) {
              state = ExportState::Failed;
              return;
            }

            if (!writeHtmlFile(p_outputFile, p_baseUrl, p_headContent, p_styleContent, p_content,
                               p_bodyClassList, p_htmlOption.m_embedStyles,
                               p_htmlOption.m_completePage, p_htmlOption.m_embedImages)) {
              state = ExportState::Failed;
              return;
            }

            state = ExportState::Finished;
          });

  // saveContent() serializes the LIVE DOM, so anything the page flattens here is captured. Direct
  // HTML export passes no flags and keeps math and diagrams vector.
  if (p_rasterFlags) {
    if (!prepareLivePageForExport(p_rasterFlags, p_layout)) {
      disconnect(m_viewer->adapter(), &MarkdownViewerAdapter::contentReady, this, 0);
      return false;
    }
  }

  m_viewer->adapter()->saveContent();

  QElapsedTimer timer;
  timer.start();
  while (state == ExportState::Busy) {
    Utils::sleepWait(100);

    if (m_askedToStop || timer.hasExpired(30000)) {
      emit logRequested(tr("Failed or cancelled while serializing HTML."));
      break;
    }
  }

  disconnect(connection);
  return !m_askedToStop && state == ExportState::Finished;
}

bool WebViewExporter::writeHtmlFile(const QString &p_file, const QUrl &p_baseUrl,
                                    const QString &p_headContent, QString p_styleContent,
                                    const QString &p_content, const QString &p_bodyClassList,
                                    bool p_embedStyles, bool p_completePage, bool p_embedImages) {
  const auto baseName = QFileInfo(p_file).completeBaseName();

  const QString resourceFolderName = baseName + "_files";
  auto resourceFolder =
      PathUtils::concatenateFilePath(PathUtils::parentDirPath(p_file), resourceFolderName);

  qDebug() << "HTML files folder" << resourceFolder;

  auto htmlContent = m_exportHtmlTemplate;

  const auto title = QStringLiteral("%1").arg(baseName);
  HtmlTemplateUtils::fillTitle(htmlContent, title);

  if (!p_styleContent.isEmpty() && p_embedStyles) {
    // Utils.fetchStyleContent() serializes the viewer's live stylesheets, but it can miss the
    // syntax-highlight <link> (leaving exported code blocks uncolored and the copy-button
    // toolbar unpositioned). Prepend the highlight stylesheet content so the static file is
    // self-contained. Prepended (not appended) so the serialized rules can still override.
    if (!m_syntaxHighlightStyleFile.isEmpty()) {
      const auto highlightContent =
          readTemplateFile(m_syntaxHighlightStyleFile, "failed to read highlight stylesheet");
      if (!highlightContent.isEmpty()) {
        p_styleContent = highlightContent + QStringLiteral("\n") + p_styleContent;
      }
    }
    embedStyleResources(p_styleContent);
    HtmlTemplateUtils::fillStyleContent(htmlContent, p_styleContent);
  }

  if (!p_headContent.isEmpty()) {
    HtmlTemplateUtils::fillHeadContent(htmlContent, p_headContent);
  }

  if (p_completePage) {
    QString content(p_content);
    if (p_embedImages) {
      embedBodyResources(p_baseUrl, content);
    } else {
      fixBodyResources(p_baseUrl, resourceFolder, content);
    }

    HtmlTemplateUtils::fillContent(htmlContent, content);
  } else {
    HtmlTemplateUtils::fillContent(htmlContent, p_content);
  }

  if (!p_bodyClassList.isEmpty()) {
    HtmlTemplateUtils::fillBodyClassList(htmlContent, p_bodyClassList);
  }

  Error err = FileUtils2::writeFile(p_file, htmlContent);
  if (err) {
    qWarning() << "failed to write exported HTML file" << p_file << err.what();
    emit logRequested(tr("Failed to write exported HTML file (%1): %2").arg(p_file, err.what()));
    return false;
  }

  // Delete empty resource folder.
  QDir dir(resourceFolder);
  if (dir.exists() && dir.isEmpty()) {
    dir.cdUp();
    dir.rmdir(resourceFolderName);
  }

  return true;
}

QSize WebViewExporter::pageLayoutSize(const QPageLayout &p_layout) const {
  Q_ASSERT(m_viewer);
  auto rect = p_layout.paintRect(QPageLayout::Inch);
  return QSize(rect.width() * m_viewer->logicalDpiX(), rect.height() * m_viewer->logicalDpiY());
}

void WebViewExporter::prepare(const ExportHtmlRequest &p_request) {
  Q_ASSERT(!m_viewer && !m_exportOngoing);
  const auto &p_option = p_request.options;

  auto *themeService = m_services.get<ThemeService>();
  auto *configMgr = m_services.get<ConfigMgr2>();
  Q_ASSERT(themeService);
  Q_ASSERT(configMgr);

  auto *adapter = new MarkdownViewerAdapter(m_services, this);
  m_viewer = new MarkdownViewer(adapter, m_services, themeService->getBaseBackground(), 1,
                                m_widgetParent.data());
  m_viewer->hide();
  connect(m_viewer->page(), &QWebEnginePage::loadFinished, this, [this](bool p_ok) {
    m_webViewStates |= p_ok ? WebViewState::LoadFinished : WebViewState::Failed;
  });
  connect(adapter, &MarkdownViewerAdapter::workFinished, this,
          [this]() { m_webViewStates |= WebViewState::WorkFinished; });

  const bool scrollable =
      p_request.purpose != ExportHtmlPurpose::Pdf && p_option.m_htmlOption.m_scrollable;

  const auto &config = configMgr->getEditorConfig().getMarkdownEditorConfig();

  // Configure the shared Graphviz/PlantUML helpers with the resolved executables. Export may
  // render diagrams without a MarkdownViewWindow having configured them, so seed them here.
  GraphvizHelper::getInst().update(config.getGraphvizExe());
  PlantUmlHelper::getInst().update(config.getPlantUmlJar(), config.getGraphvizExe(),
                                   config.getPlantUmlCommand());

  bool useWkhtmltopdf = false;
  QSize pageBodySize(1024, 768);
  if (p_request.purpose == ExportHtmlPurpose::Pdf) {
    useWkhtmltopdf = p_option.m_pdfOption.m_useWkhtmltopdf;
    if (p_option.m_pdfOption.m_layout) {
      pageBodySize = pageLayoutSize(*p_option.m_pdfOption.m_layout);
    }
  }

  qDebug() << "export page body size" << pageBodySize;

  HtmlTemplateUtils::MarkdownParas paras;
  auto webStyleFile = p_option.m_renderingStyleFile;
  if (webStyleFile.isEmpty()) {
    // Render with the CURRENT theme's web.css (matches the dialog default) rather than an
    // arbitrary first theme, so an empty ExportOption produces a consistent result.
    webStyleFile = themeService->getFile(Theme::File::WebStyleSheet);
  }

  QString webStyleContent;
  const QFileInfo webStyleInfo(webStyleFile);
  const auto themeFolder = webStyleInfo.absolutePath();
  if (webStyleInfo.fileName() == QStringLiteral("web.css") &&
      Theme::isValidThemeFolder(themeFolder)) {
    // Resolve the selected theme before Chromium can discard invalid palette declarations.
    try {
      QScopedPointer<Theme> theme(
          Theme::fromFolder(themeFolder, ThemeUtils::backfillSystemPalette));
      webStyleContent = theme->fetchWebStyleSheet();
    } catch (Exception &p_e) {
      qWarning() << "failed to load export theme" << themeFolder << p_e.what();
    }
  }
  if (webStyleContent.isEmpty()) {
    // Keep the selected stylesheet when resolution is unavailable, as for standalone CSS.
    paras.m_webStyleSheetFile = webStyleFile;
  }
  // Guarantee a real highlight.css: honor the selected syntax style only when it truly is a
  // highlight.css, else fall back to the current theme's highlight.css. Fixes the regression
  // where a persisted web.css left code blocks without Prism .token colors.
  const auto highlightStyleFile = resolveSyntaxHighlightFile(
      p_option.m_syntaxHighlightStyleFile, themeService->getFile(Theme::File::HighlightStyleSheet));

  paras.m_highlightStyleSheetFile = highlightStyleFile;
  // Remember the resolved highlight stylesheet so writeHtmlFile() can embed its content
  // directly (see m_syntaxHighlightStyleFile). This guarantees syntax colors and the
  // code-block copy-button toolbar styling survive into the static exported file.
  m_syntaxHighlightStyleFile = highlightStyleFile;
  paras.m_transparentBackgroundEnabled = p_option.m_useTransparentBg;
  paras.m_scrollable = scrollable;
  paras.m_headingFoldingEnabled =
      isHeadingFoldingEnabledForExport(p_option, config.getHeadingFoldingEnabled());
  paras.m_bodyWidth = pageBodySize.width();
  paras.m_bodyHeight = pageBodySize.height();
  paras.m_transformSvgToPngEnabled = p_option.m_transformSvgToPngEnabled;
  // wkhtmltopdf renders MathJax SVG at the surrounding Latin text size (scale x 1.0), which
  // looks too small beside the larger CJK glyphs in CJK-heavy notes. Enlarge math to 1.5x
  // body text for this export path only. `scale` is the ONLY effective lever here: MathJax
  // v3 SVG output has no matchFontHeight, so the `minScale` option is a no-op (verified by
  // measuring rendered equation height across scale/minScale combinations).
  paras.m_mathJaxScale = useWkhtmltopdf ? 1.5 : -1;
  // Intermediate consumers must not receive interactive export chrome.
  paras.m_removeCodeToolBarEnabled =
      p_request.purpose != ExportHtmlPurpose::Standalone || p_option.m_removeCodeToolBarEnabled;

  m_htmlTemplate = generateMarkdownViewerTemplate(*configMgr, config, paras, webStyleContent);

  {
    const bool addOutlinePanel = p_request.purpose == ExportHtmlPurpose::Standalone &&
                                 p_option.m_htmlOption.m_addOutlinePanel;
    m_exportHtmlTemplate = generateMarkdownExportTemplate(*configMgr, config, addOutlinePanel,
                                                          paras.m_headingFoldingEnabled);
  }
}

bool WebViewExporter::embedStyleResources(QString &p_html) const {
  bool altered = false;
  // Font sources may continue with format(...) or another source before the semicolon.
  QRegularExpression reg("\\burl\\(\"((file|qrc):[^\"\\)]+)\"\\)");

  int pos = 0;
  while (pos < p_html.size()) {
    QRegularExpressionMatch match;
    int idx = p_html.indexOf(reg, pos, &match);
    if (idx == -1) {
      break;
    }

    const QUrl url(match.captured(1));
    const auto suffix = QFileInfo(url.path()).suffix().toLower();
    QString dataURI;
    if (suffix == QStringLiteral("woff2") || suffix == QStringLiteral("woff") ||
        suffix == QStringLiteral("ttf")) {
      const auto path = url.isLocalFile() ? url.toLocalFile() : QLatin1Char(':') + url.path();
      QFile font(path);
      if (font.open(QIODevice::ReadOnly)) {
        const auto data = font.readAll();
        if (!data.isEmpty()) {
          dataURI = QStringLiteral("data:font/%1;base64,%2")
                        .arg(suffix, QString::fromLatin1(data.toBase64()));
        }
      }
    } else {
      dataURI = WebUtils::toDataUri(url, false);
    }
    if (dataURI.isEmpty()) {
      pos = idx + match.capturedLength();
    } else {
      // Replace the url string in html.
      QString newUrl = QStringLiteral("url('%1')").arg(dataURI);
      p_html.replace(idx, match.capturedLength(), newUrl);
      pos = idx + newUrl.size();
      altered = true;
    }
  }

  return altered;
}

bool WebViewExporter::embedBodyResources(const QUrl &p_baseUrl, QString &p_html) {
  if (p_baseUrl.isEmpty()) {
    return false;
  }

  // Shared with fixBodyResources() so the two passes cannot drift, and so the
  // attribute-preservation guarantee (a sized image keeps its width/height) is
  // testable without a web engine.
  return rewriteRenderedImgSrc(p_html, QLatin1Char('\''), [&p_baseUrl](const QString &p_src) {
    return WebUtils::toDataUri(p_baseUrl.resolved(p_src), true);
  });
}

static QString getResourceRelativePath(const QString &p_file) {
  int idx = p_file.lastIndexOf('/');
  int idx2 = p_file.lastIndexOf('/', idx - 1);
  Q_ASSERT(idx > 0 && idx2 < idx);
  return "." + p_file.mid(idx2);
}

bool WebViewExporter::fixBodyResources(const QUrl &p_baseUrl, const QString &p_folder,
                                       QString &p_html) {
  if (p_baseUrl.isEmpty()) {
    return false;
  }

  return rewriteRenderedImgSrc(
      p_html, QLatin1Char('"'), [&p_baseUrl, &p_folder](const QString &p_src) {
        const QString targetFile = WebUtils::copyResource(p_baseUrl.resolved(p_src), p_folder);
        return targetFile.isEmpty() ? QString() : getResourceRelativePath(targetFile);
      });
}

bool WebViewExporter::prepareLivePageForExport(RasterFlags p_flags, const QPageLayout *p_layout) {
  bool pdfRenderReady = false;
  QMetaObject::Connection conn =
      connect(m_viewer->adapter(), &MarkdownViewerAdapter::pdfRenderReady, this,
              [&pdfRenderReady]() { pdfRenderReady = true; });

  // Rasterize diagrams at the PRINTED box, not the on-screen one: the page lays a diagram out at
  // its natural width and the CSS clamp then shrinks it, so rastering on screen resamples twice
  // and visibly softens the result. The printed box is derived from the very layout wkhtmltopdf
  // is handed. 0 disables a clamp, and the page then falls back to its natural size.
  int maxWidthPx = 0;
  int maxHeightPx = 0;
  if (p_layout) {
    maxWidthPx = wkhtmltopdfContentWidthPx(*p_layout);
    maxHeightPx = wkhtmltopdfPageContentHeightPx(*p_layout);
  }
  const int rasterScale = qMax(2, qRound(c_diagramRasterDpi / 96.0));

  // One entry point, no worker names: the page side decides which renderers have export work and
  // signals onPdfRenderReady() exactly once when they have all settled (markdownviewercore.js).
  const QString js =
      QStringLiteral("if (typeof vxcore !== 'undefined' && vxcore.prepareForExport) {"
                     "  vxcore.prepareForExport({ rasterizeMath: %1, rasterizeDiagrams: %2,"
                     "    maxWidthPx: %3, maxHeightPx: %4, rasterScale: %5 });"
                     "} else { window.vxMarkdownAdapter.onPdfRenderReady(); }")
          .arg(p_flags.testFlag(RasterizeMath) ? QStringLiteral("true") : QStringLiteral("false"),
               p_flags.testFlag(RasterizeDiagrams) ? QStringLiteral("true")
                                                   : QStringLiteral("false"),
               QString::number(maxWidthPx), QString::number(maxHeightPx),
               QString::number(rasterScale));

  m_viewer->page()->runJavaScript(js);

  // Bounded wait: if the render-ready signal never arrives (JS bridge not
  // ready, an uncaught error in the page, ...), fall through instead of
  // freezing the export. Worst case the math stays as SVG.
  const qint64 c_pdfRenderReadyTimeoutMs = 30000;
  QElapsedTimer renderTimer;
  renderTimer.start();
  while (!pdfRenderReady) {
    Utils::sleepWait(100);
    if (m_askedToStop) {
      disconnect(conn);
      return false;
    }
    if (renderTimer.hasExpired(c_pdfRenderReadyTimeoutMs)) {
      disconnect(conn);
      if (p_flags.testFlag(RasterizeDiagrams)) {
        // Diagram rasterization rewrites the LIVE DOM (each diagram is replaced by its raster).
        // Serializing the page while that is still running would produce a random mix of diagrams
        // and rasters, so fail loudly instead. Math alone is left as a best-effort fallback: it
        // only degrades the equations back to vector SVG.
        qWarning() << "timed out waiting for diagram rasterization; aborting the export";
        emit logRequested(tr("Timed out while rasterizing the diagrams of this note."));
        return false;
      }
      qWarning() << "timed out waiting for math SVG rasterization; proceeding";
      return true;
    }
  }
  disconnect(conn);
  return true;
}
