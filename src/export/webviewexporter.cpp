#include "webviewexporter.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QWebEnginePage>
#include <QWidget>

#include <core/editorconfig.h>
#include <core/exception.h>
#include <core/htmltemplatehelper.h>
#include <core/iconfigmgr.h>
#include <core/mainconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/services/configcoreservice.h>
#include <gui/services/themeservice.h>
#include <utils/fileutils.h>
#include <utils/htmlutils.h>
#include <utils/pathutils.h>
#include <utils/processutils.h>
#include <utils/utils.h>
#include <utils/webutils.h>
#include <widgets/editors/markdownviewer.h>
#include <widgets/editors/markdownvieweradapter.h>

using namespace vnotex;

static const QString c_imgRegExp = "<img ([^>]*)src=\"(?!data:)([^\"]+)\"([^>]*)>";

namespace {

class ExportConfigMgr final : public IConfigMgr {
public:
  void updateMainConfig(const QJsonObject &p_jobj) override { m_mainConfig = p_jobj; }

  void updateSessionConfig(const QJsonObject &p_jobj) override { m_sessionConfig = p_jobj; }

private:
  QJsonObject m_mainConfig;
  QJsonObject m_sessionConfig;
};

QString resolveConfigFile(ConfigCoreService &p_configService, const QString &p_filePath) {
  QFileInfo info(p_filePath);
  if (info.isAbsolute()) {
    return p_filePath;
  }

  return QDir(p_configService.getDataPath(DataLocation::App)).filePath(p_filePath);
}

QString readTemplateFile(const QString &p_filePath, const QString &p_logPrefix) {
  try {
    return FileUtils::readTextFile(p_filePath);
  } catch (Exception &p_e) {
    qWarning() << p_logPrefix << p_filePath << p_e.what();
    return QString();
  }
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

void fillGlobalStyles(QString &p_template, const WebResource &p_resource,
                      ConfigCoreService &p_configService, const QString &p_additionalStyles) {
  QString styles;
  for (const auto &ele : p_resource.m_resources) {
    if (ele.isGlobal()) {
      if (ele.m_enabled) {
        for (const auto &style : ele.m_styles) {
          const auto styleFile = resolveConfigFile(p_configService, style);
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

void fillResources(QString &p_template, const WebResource &p_resource,
                   ConfigCoreService &p_configService) {
  QString styles;
  QString scripts;

  for (const auto &ele : p_resource.m_resources) {
    if (ele.m_enabled && !ele.isGlobal()) {
      for (const auto &style : ele.m_styles) {
        styles += fillStyleTag(resolveConfigFile(p_configService, style));
      }

      for (const auto &script : ele.m_scripts) {
        scripts += fillScriptTag(resolveConfigFile(p_configService, script));
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
                            ConfigCoreService &p_configService) {
  QString styles;
  QString scripts;

  for (const auto &ele : p_resource.m_resources) {
    if (ele.m_enabled && !ele.isGlobal()) {
      for (const auto &style : ele.m_styles) {
        const auto styleFile = resolveConfigFile(p_configService, style);
        const auto content = readTemplateFile(styleFile, "failed to read resource");
        if (!content.isEmpty()) {
          styles += content;
        }
      }

      for (const auto &script : ele.m_scripts) {
        const auto scriptFile = resolveConfigFile(p_configService, script);
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

QString generateMarkdownViewerTemplate(ConfigCoreService &p_configService,
                                       const MarkdownEditorConfig &p_config,
                                       const HtmlTemplateHelper::MarkdownParas &p_paras) {
  const auto &viewerResource = p_config.getViewerResource();
  const auto templateFile = resolveConfigFile(p_configService, viewerResource.m_template);
  auto htmlTemplate = readTemplateFile(templateFile, "failed to read HTML template");
  if (htmlTemplate.isEmpty()) {
    return errorPage();
  }

  fillGlobalStyles(htmlTemplate, viewerResource, p_configService, QString());
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

  fillResources(htmlTemplate, viewerResource, p_configService);
  return htmlTemplate;
}

QString generateMarkdownExportTemplate(ConfigCoreService &p_configService,
                                       const MarkdownEditorConfig &p_config,
                                       bool p_addOutlinePanel) {
  auto exportResource = p_config.getExportResource();
  const auto templateFile = resolveConfigFile(p_configService, exportResource.m_template);
  auto htmlTemplate =
      readTemplateFile(templateFile, "failed to read Markdown export HTML template");
  if (htmlTemplate.isEmpty()) {
    return errorPage();
  }

  fillGlobalStyles(htmlTemplate, exportResource, p_configService, QString());
  HtmlTemplateHelper::fillOutlinePanel(htmlTemplate, exportResource, p_addOutlinePanel);
  fillResourcesByContent(htmlTemplate, exportResource, p_configService);
  return htmlTemplate;
}

MarkdownEditorConfig buildMarkdownEditorConfig(ConfigCoreService &p_configService) {
  ExportConfigMgr configMgr;
  MainConfig mainConfig(&configMgr);

  const auto mainConfigJson = p_configService.getConfig();
  if (!mainConfigJson.isEmpty()) {
    mainConfig.fromJson(mainConfigJson);
  }

  return mainConfig.getEditorConfig().getMarkdownEditorConfig();
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
    ret = doExportHtml(p_option.m_htmlOption, p_destPath, baseUrl);
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
                                   const QString &p_outputFile, const QUrl &p_baseUrl) {
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
  HtmlTemplateHelper::fillTitle(htmlContent, title);

  if (!p_styleContent.isEmpty() && p_embedStyles) {
    embedStyleResources(p_styleContent);
    HtmlTemplateHelper::fillStyleContent(htmlContent, p_styleContent);
  }

  if (!p_headContent.isEmpty()) {
    HtmlTemplateHelper::fillHeadContent(htmlContent, p_headContent);
  }

  if (p_completePage) {
    QString content(p_content);
    if (p_embedImages) {
      embedBodyResources(p_baseUrl, content);
    } else {
      fixBodyResources(p_baseUrl, resourceFolder, content);
    }

    HtmlTemplateHelper::fillContent(htmlContent, content);
  } else {
    HtmlTemplateHelper::fillContent(htmlContent, p_content);
  }

  if (!p_bodyClassList.isEmpty()) {
    HtmlTemplateHelper::fillBodyClassList(htmlContent, p_bodyClassList);
  }

  FileUtils::writeFile(p_file, htmlContent);

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
  auto *configService = m_services.get<ConfigCoreService>();
  Q_ASSERT(themeService);
  Q_ASSERT(configService);

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

  const auto config = buildMarkdownEditorConfig(*configService);
  bool useWkhtmltopdf = false;
  QSize pageBodySize(1024, 768);
  if (p_option.m_targetFormat == ExportFormat::PDF) {
    useWkhtmltopdf = p_option.m_pdfOption.m_useWkhtmltopdf;
    pageBodySize = pageLayoutSize(*(p_option.m_pdfOption.m_layout));
  }

  qDebug() << "export page body size" << pageBodySize;

  HtmlTemplateHelper::MarkdownParas paras;
  auto webStyleFile = p_option.m_renderingStyleFile;
  if (webStyleFile.isEmpty()) {
    const auto webStyles = themeService->getWebStyles();
    if (!webStyles.isEmpty()) {
      webStyleFile = webStyles.constFirst().second;
    }
  }

  paras.m_webStyleSheetFile = webStyleFile;
  auto highlightStyleFile = p_option.m_syntaxHighlightStyleFile;
  if (highlightStyleFile.isEmpty()) {
    highlightStyleFile = themeService->getFile(Theme::File::HighlightStyleSheet);
  }

  paras.m_highlightStyleSheetFile = highlightStyleFile;
  paras.m_transparentBackgroundEnabled = p_option.m_useTransparentBg;
  paras.m_scrollable = scrollable;
  paras.m_bodyWidth = pageBodySize.width();
  paras.m_bodyHeight = pageBodySize.height();
  paras.m_transformSvgToPngEnabled = p_option.m_transformSvgToPngEnabled;
  paras.m_mathJaxScale = useWkhtmltopdf ? 2.5 : -1;
  paras.m_removeCodeToolBarEnabled = p_option.m_removeCodeToolBarEnabled;

  m_htmlTemplate = generateMarkdownViewerTemplate(*configService, config, paras);

  {
    const bool addOutlinePanel =
        p_option.m_targetFormat == ExportFormat::HTML && p_option.m_htmlOption.m_addOutlinePanel;
    m_exportHtmlTemplate = generateMarkdownExportTemplate(*configService, config, addOutlinePanel);
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

    // Footer.
    m_wkhtmltopdfArgs << "--footer-right" << "[page]"
                      << "--footer-spacing" << QString::number(marginsMM.bottom() / 3, 'f', 2);
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

bool WebViewExporter::doExportPdf(const ExportPdfOption &p_pdfOption, const QString &p_outputFile) {
  ExportState state = ExportState::Busy;

  m_viewer->page()->printToPdf(
      [&, this](const QByteArray &p_result) {
        qDebug() << "doExportPdf printToPdf ready";
        if (p_result.isEmpty() || m_askedToStop) {
          state = ExportState::Failed;
          return;
        }

        Q_ASSERT(!p_outputFile.isEmpty());

        FileUtils::writeFile(p_outputFile, p_result);

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
    FileUtils::copyFile(tmpFile, p_outputFile);
  }

  return ret;
}

bool WebViewExporter::startProcess(const QString &p_program, const QStringList &p_args) {
  emit logRequested(p_program + " " + ProcessUtils::combineArgString(p_args));

  auto ret = ProcessUtils::start(
      p_program, p_args, [this](const QString &p_log) { emit logRequested(p_log); }, m_askedToStop);
  return ret == ProcessUtils::State::Succeeded;
}
