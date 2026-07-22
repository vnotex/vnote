#include "webviewexporter.h"

#include "exportstyleresolver.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QWebEnginePage>
#include <QWidget>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/htmltemplateutils.h>
#include <core/markdownwebglobaloptions.h>
#include <core/markdowneditorconfig.h>
#include <gui/services/themeservice.h>
#include <utils/fileutils2.h>
#include <utils/htmlutils.h>
#include <utils/pathutils.h>
#include <utils/processutils.h>
#include <utils/utils.h>
#include <utils/webutils.h>
#include <widgets/editors/graphvizhelper.h>
#include <widgets/editors/markdownviewer.h>
#include <widgets/editors/markdownvieweradapter.h>
#include <widgets/editors/plantumlhelper.h>
using namespace vnotex;

static const QString c_imgRegExp = "<img ([^>]*)src=\"(?!data:)([^\"]+)\"([^>]*)>";

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

void fillThemeStyles(QString &p_template, const QString &p_webStyleSheetFile,
                     const QString &p_highlightStyleSheetFile) {
  QString styles;
  styles += fillStyleTag(p_webStyleSheetFile);
  styles += fillStyleTag(p_highlightStyleSheetFile);
  if (!styles.isEmpty()) {
    p_template.replace(QStringLiteral("<!-- VX_THEME_STYLES_PLACEHOLDER -->"), styles);
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
                                       const HtmlTemplateUtils::MarkdownParas &p_paras) {
  const auto &viewerResource = p_config.getViewerResource();
  const auto templateFile = resolveConfigFile(p_configMgr, viewerResource.m_template);
  auto htmlTemplate = readTemplateFile(templateFile, "failed to read HTML template");
  if (htmlTemplate.isEmpty()) {
    return errorPage();
  }

  fillGlobalStyles(htmlTemplate, viewerResource, p_configMgr, QString());
  fillThemeStyles(htmlTemplate, p_paras.m_webStyleSheetFile, p_paras.m_highlightStyleSheetFile);

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
                                       const MarkdownEditorConfig &p_config,
                                       bool p_addOutlinePanel) {
  auto exportResource = p_config.getExportResource();
  const auto templateFile = resolveConfigFile(p_configMgr, exportResource.m_template);
  auto htmlTemplate =
      readTemplateFile(templateFile, "failed to read Markdown export HTML template");
  if (htmlTemplate.isEmpty()) {
    return errorPage();
  }

  fillGlobalStyles(htmlTemplate, exportResource, p_configMgr, QString());
  HtmlTemplateUtils::fillOutlinePanel(htmlTemplate, exportResource, p_addOutlinePanel);

  // Always inject the self-contained code-block copy handler + styling, independent of the
  // user-mutable exportResource config (an existing config persisted before issue #2674 would
  // otherwise omit them, leaving the copy button non-functional/unstyled in exports). Injected
  // ahead of the placeholder so fillResourcesByContent() still appends any config resources.
  {
    const auto copyScript = readTemplateFile(
        resolveConfigFile(p_configMgr, QStringLiteral("web/js/exportcodecopy.js")),
        "failed to read code-block copy script");
    if (!copyScript.isEmpty()) {
      htmlTemplate.replace(QStringLiteral("/* VX_SCRIPTS_PLACEHOLDER */"),
                           copyScript + QStringLiteral("\n/* VX_SCRIPTS_PLACEHOLDER */"));
    }

    const auto copyStyle = readTemplateFile(
        resolveConfigFile(p_configMgr, QStringLiteral("web/css/codeblockactions.css")),
        "failed to read code-block copy style");
    if (!copyStyle.isEmpty()) {
      htmlTemplate.replace(QStringLiteral("/* VX_STYLES_PLACEHOLDER */"),
                           copyStyle + QStringLiteral("\n/* VX_STYLES_PLACEHOLDER */"));
    }
  }

  fillResourcesByContent(htmlTemplate, exportResource, p_configMgr);
  return htmlTemplate;
}

} // namespace

vnotex::WebViewExporter::WebViewExporter(ServiceLocator &p_services, QWidget *p_parent)
    : QObject(p_parent), m_services(p_services) {}

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

bool vnotex::WebViewExporter::doExport(const ExportOption &p_option, const QString &p_content,
                                       const QString &p_filePath, const QString &p_fileName,
                                       const QString &p_resourcePath, const QString &p_destPath) {
  bool ret = false;
  m_askedToStop = false;

  Q_ASSERT(!m_exportOngoing);
  m_exportOngoing = true;

  m_webViewStates = WebViewState::Started;

  const auto contentPath = p_filePath.isEmpty() ? p_resourcePath : p_filePath;
  auto baseUrl = PathUtils::pathToUrl(contentPath);
  m_viewer->adapter()->reset();
  m_viewer->setHtml(m_htmlTemplate, baseUrl);

  auto textContent = p_content;
  if (p_option.m_targetFormat == ExportFormat::PDF && p_option.m_pdfOption.m_addTableOfContents &&
      !p_option.m_pdfOption.m_useWkhtmltopdf) {
    // Add `[TOC]` at the beginning.
    m_viewer->adapter()->setText("[TOC]\n\n" + textContent);
  } else {
    m_viewer->adapter()->setText(textContent);
  }

  while (!isWebViewReady()) {
    Utils::sleepWait(100);

    if (m_askedToStop) {
      goto exit_export;
    }

    if (isWebViewFailed()) {
      qWarning() << "WebView failed when exporting"
                 << (p_filePath.isEmpty() ? p_fileName : p_filePath);
      goto exit_export;
    }
  }

  qDebug() << "WebView is ready";

  // Add extra wait to make sure Web side is really ready.
  Utils::sleepWait(200);

  switch (p_option.m_targetFormat) {
  case ExportFormat::HTML:
    // TODO: MIME HTML format is not supported yet.
    Q_ASSERT(!p_option.m_htmlOption.m_useMimeHtmlFormat);
    ret = doExportHtml(p_option.m_htmlOption, p_destPath, baseUrl,
                       p_option.m_transformSvgToPngEnabled);
    break;

  case ExportFormat::PDF:
    if (p_option.m_pdfOption.m_useWkhtmltopdf) {
      ret = doExportWkhtmltopdf(p_option.m_pdfOption, p_destPath, baseUrl);
    } else {
      ret = doExportPdf(p_option.m_pdfOption, p_destPath);
    }
    break;

  default:
    break;
  }

exit_export:
  m_exportOngoing = false;
  return ret;
}

void WebViewExporter::stop() { m_askedToStop = true; }

bool WebViewExporter::isWebViewReady() const {
  return m_webViewStates == (WebViewState::LoadFinished | WebViewState::WorkFinished);
}

bool WebViewExporter::isWebViewFailed() const { return m_webViewStates & WebViewState::Failed; }

bool WebViewExporter::doExportHtml(const ExportHtmlOption &p_htmlOption,
                                   const QString &p_outputFile, const QUrl &p_baseUrl,
                                   bool p_rasterizeMathSvg) {
  ExportState state = ExportState::Busy;

  connect(m_viewer->adapter(), &MarkdownViewerAdapter::contentReady, this,
          [&, this](const QString &p_headContent, const QString &p_styleContent,
                    const QString &p_content, const QString &p_bodyClassList) {
            qDebug() << "doExportHtml contentReady";
            // Maybe unnecessary. Just to avoid duplicated signal connections.
            disconnect(m_viewer->adapter(), &MarkdownViewerAdapter::contentReady, this, 0);

            if (p_content.isEmpty() || m_askedToStop) {
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

  // For custom/docx (intermediate HTML) export, flatten MathJax's inline SVG to PNG first so
  // downstream tools (e.g. Pandoc without rsvg-convert) can embed the equations. saveContent()
  // serializes the live DOM, so the rasterized <img> nodes are captured. Direct HTML export
  // keeps math as vector (p_rasterizeMathSvg == false).
  if (p_rasterizeMathSvg) {
    if (!waitForMathSvgRasterization()) {
      disconnect(m_viewer->adapter(), &MarkdownViewerAdapter::contentReady, this, 0);
      return false;
    }
  }

  m_viewer->adapter()->saveContent();

  while (state == ExportState::Busy) {
    Utils::sleepWait(100);

    if (m_askedToStop) {
      break;
    }
  }

  return state == ExportState::Finished;
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

void WebViewExporter::prepare(const ExportOption &p_option) {
  Q_ASSERT(!m_viewer && !m_exportOngoing);
  Q_ASSERT(p_option.m_targetFormat == ExportFormat::PDF ||
           p_option.m_targetFormat == ExportFormat::HTML);

  auto *themeService = m_services.get<ThemeService>();
  auto *configMgr = m_services.get<ConfigMgr2>();
  Q_ASSERT(themeService);
  Q_ASSERT(configMgr);

  auto *adapter = new MarkdownViewerAdapter(m_services, this);
  m_viewer = new MarkdownViewer(adapter, m_services, themeService->getBaseBackground(), 1,
                                static_cast<QWidget *>(parent()));
  m_viewer->hide();
  connect(m_viewer->page(), &QWebEnginePage::loadFinished, this,
          [this]() { m_webViewStates |= WebViewState::LoadFinished; });
  connect(adapter, &MarkdownViewerAdapter::workFinished, this,
          [this]() { m_webViewStates |= WebViewState::WorkFinished; });

  bool scrollable = true;
  if (p_option.m_targetFormat == ExportFormat::PDF ||
      (p_option.m_targetFormat == ExportFormat::HTML && !p_option.m_htmlOption.m_scrollable) ||
      (p_option.m_targetFormat == ExportFormat::Custom &&
       !p_option.m_customOption->m_targetPageScrollable)) {
    scrollable = false;
  }

  const auto &config = configMgr->getEditorConfig().getMarkdownEditorConfig();

  // Configure the shared Graphviz/PlantUML helpers with the resolved executables. Export may
  // render diagrams without a MarkdownViewWindow having configured them, so seed them here.
  GraphvizHelper::getInst().update(config.getGraphvizExe());
  PlantUmlHelper::getInst().update(config.getPlantUmlJar(), config.getGraphvizExe(),
                                   config.getPlantUmlCommand());

  bool useWkhtmltopdf = false;
  QSize pageBodySize(1024, 768);
  if (p_option.m_targetFormat == ExportFormat::PDF) {
    useWkhtmltopdf = p_option.m_pdfOption.m_useWkhtmltopdf;
    pageBodySize = pageLayoutSize(*(p_option.m_pdfOption.m_layout));
  }

  qDebug() << "export page body size" << pageBodySize;

  HtmlTemplateUtils::MarkdownParas paras;
  auto webStyleFile = p_option.m_renderingStyleFile;
  if (webStyleFile.isEmpty()) {
    // Render with the CURRENT theme's web.css (matches the dialog default) rather than an
    // arbitrary first theme, so an empty ExportOption produces a consistent result.
    webStyleFile = themeService->getFile(Theme::File::WebStyleSheet);
  }

  paras.m_webStyleSheetFile = webStyleFile;
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
  paras.m_bodyWidth = pageBodySize.width();
  paras.m_bodyHeight = pageBodySize.height();
  paras.m_transformSvgToPngEnabled = p_option.m_transformSvgToPngEnabled;
  // wkhtmltopdf renders MathJax SVG at the surrounding Latin text size (scale x 1.0), which
  // looks too small beside the larger CJK glyphs in CJK-heavy notes. Enlarge math to 1.5x
  // body text for this export path only. `scale` is the ONLY effective lever here: MathJax
  // v3 SVG output has no matchFontHeight, so the `minScale` option is a no-op (verified by
  // measuring rendered equation height across scale/minScale combinations).
  paras.m_mathJaxScale = useWkhtmltopdf ? 1.5 : -1;
  // Keep the code-block toolbar (copy button) only for genuine HTML export. For PDF/Custom,
  // force it off. The intermediate HTML feeding PDF/custom has target format HTML but sets
  // m_removeCodeToolBarEnabled = true explicitly, so it is honored here and still drops the
  // toolbar; genuine HTML export leaves the option at its false default.
  paras.m_removeCodeToolBarEnabled = p_option.m_targetFormat == ExportFormat::HTML
                                         ? p_option.m_removeCodeToolBarEnabled
                                         : true;

  m_htmlTemplate = generateMarkdownViewerTemplate(*configMgr, config, paras);

  {
    const bool addOutlinePanel =
        p_option.m_targetFormat == ExportFormat::HTML && p_option.m_htmlOption.m_addOutlinePanel;
    m_exportHtmlTemplate = generateMarkdownExportTemplate(*configMgr, config, addOutlinePanel);
  }

  if (useWkhtmltopdf) {
    prepareWkhtmltopdfArguments(p_option.m_pdfOption);
  }
}

static QString marginToStrMM(qreal p_margin) { return QStringLiteral("%1mm").arg(p_margin); }

void WebViewExporter::prepareWkhtmltopdfArguments(const ExportPdfOption &p_pdfOption) {
  m_wkhtmltopdfArgs.clear();

  // Page layout.
  {
    const auto &layout = p_pdfOption.m_layout;
    m_wkhtmltopdfArgs << "--page-size" << layout->pageSize().key();
    m_wkhtmltopdfArgs << "--orientation"
                      << (layout->orientation() == QPageLayout::Portrait ? "Portrait"
                                                                         : "Landscape");

    const auto marginsMM = layout->margins(QPageLayout::Millimeter);
    m_wkhtmltopdfArgs << "--margin-bottom" << marginToStrMM(marginsMM.bottom());
    m_wkhtmltopdfArgs << "--margin-left" << marginToStrMM(marginsMM.left());
    m_wkhtmltopdfArgs << "--margin-right" << marginToStrMM(marginsMM.right());
    m_wkhtmltopdfArgs << "--margin-top" << marginToStrMM(marginsMM.top());

    // Header/footer. Emit each non-empty field; values are unicode-encoded so
    // non-ASCII (e.g. CJK) content survives, matching the TOC header handling.
    const bool hasHeader = !p_pdfOption.m_headerLeft.isEmpty() ||
                           !p_pdfOption.m_headerCenter.isEmpty() ||
                           !p_pdfOption.m_headerRight.isEmpty();
    const bool hasFooter = !p_pdfOption.m_footerLeft.isEmpty() ||
                           !p_pdfOption.m_footerCenter.isEmpty() ||
                           !p_pdfOption.m_footerRight.isEmpty();

    if (!p_pdfOption.m_headerLeft.isEmpty()) {
      m_wkhtmltopdfArgs << "--header-left" << HtmlUtils::unicodeEncode(p_pdfOption.m_headerLeft);
    }
    if (!p_pdfOption.m_headerCenter.isEmpty()) {
      m_wkhtmltopdfArgs << "--header-center" << HtmlUtils::unicodeEncode(p_pdfOption.m_headerCenter);
    }
    if (!p_pdfOption.m_headerRight.isEmpty()) {
      m_wkhtmltopdfArgs << "--header-right" << HtmlUtils::unicodeEncode(p_pdfOption.m_headerRight);
    }
    if (hasHeader) {
      m_wkhtmltopdfArgs << "--header-spacing" << QString::number(marginsMM.top() / 3, 'f', 2);
    }

    if (!p_pdfOption.m_footerLeft.isEmpty()) {
      m_wkhtmltopdfArgs << "--footer-left" << HtmlUtils::unicodeEncode(p_pdfOption.m_footerLeft);
    }
    if (!p_pdfOption.m_footerCenter.isEmpty()) {
      m_wkhtmltopdfArgs << "--footer-center" << HtmlUtils::unicodeEncode(p_pdfOption.m_footerCenter);
    }
    if (!p_pdfOption.m_footerRight.isEmpty()) {
      m_wkhtmltopdfArgs << "--footer-right" << HtmlUtils::unicodeEncode(p_pdfOption.m_footerRight);
    }
    if (hasFooter) {
      m_wkhtmltopdfArgs << "--footer-spacing" << QString::number(marginsMM.bottom() / 3, 'f', 2);
    }
  }

  m_wkhtmltopdfArgs << "--encoding" << "utf-8";

  // Delay 10 seconds for MathJax.
  m_wkhtmltopdfArgs << "--javascript-delay" << "5000";

  m_wkhtmltopdfArgs << "--enable-local-file-access";

  // Append additional global option.
  if (!p_pdfOption.m_wkhtmltopdfArgs.isEmpty()) {
    m_wkhtmltopdfArgs.append(ProcessUtils::parseCombinedArgString(p_pdfOption.m_wkhtmltopdfArgs));
  }

  // Must be put after the global object options.
  if (p_pdfOption.m_addTableOfContents) {
    m_wkhtmltopdfArgs << "toc";
    m_wkhtmltopdfArgs << "--toc-text-size-shrink" << "1.0";
    m_wkhtmltopdfArgs << "--toc-header-text" << HtmlUtils::unicodeEncode(tr("Table of Contents"));
  }
}

bool WebViewExporter::embedStyleResources(QString &p_html) const {
  bool altered = false;
  QRegularExpression reg("\\burl\\(\"((file|qrc):[^\"\\)]+)\"\\);");

  int pos = 0;
  while (pos < p_html.size()) {
    QRegularExpressionMatch match;
    int idx = p_html.indexOf(reg, pos, &match);
    if (idx == -1) {
      break;
    }

    QString dataURI = WebUtils::toDataUri(QUrl(match.captured(1)), false);
    if (dataURI.isEmpty()) {
      pos = idx + match.capturedLength();
    } else {
      // Replace the url string in html.
      QString newUrl = QStringLiteral("url('%1');").arg(dataURI);
      p_html.replace(idx, match.capturedLength(), newUrl);
      pos = idx + newUrl.size();
      altered = true;
    }
  }

  return altered;
}

bool WebViewExporter::embedBodyResources(const QUrl &p_baseUrl, QString &p_html) {
  bool altered = false;
  if (p_baseUrl.isEmpty()) {
    return altered;
  }

  QRegularExpression reg(c_imgRegExp);

  int pos = 0;
  while (pos < p_html.size()) {
    QRegularExpressionMatch match;
    int idx = p_html.indexOf(reg, pos, &match);
    if (idx == -1) {
      break;
    }

    if (match.captured(2).isEmpty()) {
      pos = idx + match.capturedLength();
      continue;
    }

    QUrl srcUrl(p_baseUrl.resolved(match.captured(2)));
    const auto dataURI = WebUtils::toDataUri(srcUrl, true);
    if (dataURI.isEmpty()) {
      pos = idx + match.capturedLength();
    } else {
      // Replace the url string in html.
      QString newUrl =
          QStringLiteral("<img %1src='%2'%3>").arg(match.captured(1), dataURI, match.captured(3));
      p_html.replace(idx, match.capturedLength(), newUrl);
      pos = idx + newUrl.size();
      altered = true;
    }
  }

  return altered;
}

static QString getResourceRelativePath(const QString &p_file) {
  int idx = p_file.lastIndexOf('/');
  int idx2 = p_file.lastIndexOf('/', idx - 1);
  Q_ASSERT(idx > 0 && idx2 < idx);
  return "." + p_file.mid(idx2);
}

bool WebViewExporter::fixBodyResources(const QUrl &p_baseUrl, const QString &p_folder,
                                       QString &p_html) {
  bool altered = false;
  if (p_baseUrl.isEmpty()) {
    return altered;
  }

  QRegularExpression reg(c_imgRegExp);

  int pos = 0;
  while (pos < p_html.size()) {
    QRegularExpressionMatch match;
    int idx = p_html.indexOf(reg, pos, &match);
    if (idx == -1) {
      break;
    }

    if (match.captured(2).isEmpty()) {
      pos = idx + match.capturedLength();
      continue;
    }

    QUrl srcUrl(p_baseUrl.resolved(match.captured(2)));
    QString targetFile = WebUtils::copyResource(srcUrl, p_folder);
    if (targetFile.isEmpty()) {
      pos = idx + match.capturedLength();
    } else {
      // Replace the url string in html.
      QString newUrl =
          QStringLiteral("<img %1src=\"%2\"%3>")
              .arg(match.captured(1), getResourceRelativePath(targetFile), match.captured(3));
      p_html.replace(idx, match.capturedLength(), newUrl);
      pos = idx + newUrl.size();
      altered = true;
    }
  }

  return altered;
}

bool WebViewExporter::waitForMathSvgRasterization() {
  bool pdfRenderReady = false;
  QMetaObject::Connection conn =
      connect(m_viewer->adapter(), &MarkdownViewerAdapter::pdfRenderReady, this,
              [&pdfRenderReady]() { pdfRenderReady = true; });

  m_viewer->page()->runJavaScript(
      "if (typeof vxcore !== 'undefined' && vxcore.getWorker('mathjax')) {"
      "  vxcore.getWorker('mathjax').convertAllSvgToPng();"
      "} else { window.vxMarkdownAdapter.onPdfRenderReady(); }");

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
      qWarning() << "timed out waiting for math SVG rasterization; proceeding";
      break;
    }
  }
  disconnect(conn);
  return true;
}

bool WebViewExporter::doExportPdf(const ExportPdfOption &p_pdfOption, const QString &p_outputFile) {
  ExportState state = ExportState::Busy;

  if (!waitForMathSvgRasterization()) {
    return false;
  }

  m_viewer->page()->printToPdf(
      [&, this](const QByteArray &p_result) {
        qDebug() << "doExportPdf printToPdf ready";
        if (p_result.isEmpty() || m_askedToStop) {
          state = ExportState::Failed;
          return;
        }

        Q_ASSERT(!p_outputFile.isEmpty());

        Error err = FileUtils2::writeFile(p_outputFile, p_result);
        if (err) {
          qWarning() << "failed to write exported PDF file" << p_outputFile << err.what();
          state = ExportState::Failed;
          return;
        }

        state = ExportState::Finished;
      },
      *p_pdfOption.m_layout);

  while (state == ExportState::Busy) {
    Utils::sleepWait(100);

    if (m_askedToStop) {
      break;
    }
  }

  return state == ExportState::Finished;
}

bool WebViewExporter::doExportWkhtmltopdf(const ExportPdfOption &p_pdfOption,
                                          const QString &p_outputFile, const QUrl &p_baseUrl) {
  if (p_pdfOption.m_wkhtmltopdfExePath.isEmpty()) {
    qWarning() << "invalid wkhtmltopdf executable path";
    return false;
  }

  ExportState state = ExportState::Busy;

  connect(m_viewer->adapter(), &MarkdownViewerAdapter::contentReady, this,
          [&, this](const QString &p_headContent, const QString &p_styleContent,
                    const QString &p_content, const QString &p_bodyClassList) {
            qDebug() << "doExportWkhtmltopdf contentReady";
            // Maybe unnecessary. Just to avoid duplicated signal connections.
            disconnect(m_viewer->adapter(), &MarkdownViewerAdapter::contentReady, this, 0);

            if (p_content.isEmpty() || m_askedToStop) {
              state = ExportState::Failed;
              return;
            }

            // Save HTML to a temp dir.
            QTemporaryDir tmpDir;
            if (!tmpDir.isValid()) {
              state = ExportState::Failed;
              return;
            }

            auto tmpHtmlFile = tmpDir.filePath("vnote_export_tmp.html");
            if (!writeHtmlFile(tmpHtmlFile, p_baseUrl, p_headContent, p_styleContent, p_content,
                               p_bodyClassList, true, true, false)) {
              state = ExportState::Failed;
              return;
            }

            // Convert HTML to PDF via wkhtmltopdf.
            if (htmlToPdfViaWkhtmltopdf(p_pdfOption, QStringList() << tmpHtmlFile, p_outputFile)) {
              state = ExportState::Finished;
            } else {
              state = ExportState::Failed;
            }
          });

  m_viewer->adapter()->saveContent();

  while (state == ExportState::Busy) {
    Utils::sleepWait(100);

    if (m_askedToStop) {
      break;
    }
  }

  return state == ExportState::Finished;
}

bool WebViewExporter::htmlToPdfViaWkhtmltopdf(const ExportPdfOption &p_pdfOption,
                                              const QStringList &p_htmlFiles,
                                              const QString &p_outputFile) {
  QStringList args(m_wkhtmltopdfArgs);

  // Prepare the args.
  for (auto const &file : p_htmlFiles) {
    // Note: system's locale settings (Language for non-Unicode programs) is important to
    // wkhtmltopdf. Input file could be encoded via
    // QUrl::fromLocalFile(p_htmlFile).toString(QUrl::EncodeUnicode) to handle non-ASCII path. But
    // for the output file, it is useless.
    args << QUrl::fromLocalFile(QDir::toNativeSeparators(file)).toString(QUrl::EncodeUnicode);
  }

  // To handle non-ASCII path, export it to a temp file and then copy it.
  QTemporaryDir tmpDir;
  if (!tmpDir.isValid()) {
    return false;
  }

  const auto tmpFile = tmpDir.filePath("vx_tmp_output.pdf");
  args << QDir::toNativeSeparators(tmpFile);

  bool ret = startProcess(QDir::toNativeSeparators(p_pdfOption.m_wkhtmltopdfExePath), args);
  if (ret && QFileInfo::exists(tmpFile)) {
    emit logRequested(tr("Copy output file (%1) to (%2).").arg(tmpFile, p_outputFile));
    Error err = FileUtils2::copyFile(tmpFile, p_outputFile);
    if (err) {
      qWarning() << "failed to copy exported PDF file" << tmpFile << err.what();
      return false;
    }
  }

  return ret;
}

bool WebViewExporter::startProcess(const QString &p_program, const QStringList &p_args) {
  emit logRequested(p_program + " " + ProcessUtils::combineArgString(p_args));

  auto ret = ProcessUtils::start(
      p_program, p_args, [this](const QString &p_log) { emit logRequested(p_log); }, m_askedToStop);
  return ret == ProcessUtils::State::Succeeded;
}
