#include "webviewexporter.h"

#include <QWidget>
#include <QWebEnginePage>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QProcess>

#include <widgets/editors/markdownviewer.h>
#include <widgets/editors/editormarkdownvieweradapter.h>
#include <core/editorconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/configmgr.h>
#include <core/htmltemplatehelper.h>
#include <utils/utils.h>
#include <utils/pathutils.h>
#include <utils/fileutils.h>
#include <utils/webutils.h>
#include <utils/processutils.h>
#include <core/file.h>

using namespace vnotex;

static const QString c_imgRegExp = "<img ([^>]*)src=\"(?!data:)([^\"]+)\"([^>]*)>";

WebViewExporter::WebViewExporter(QWidget *p_parent)
    : QObject(p_parent)
{
}

WebViewExporter::~WebViewExporter()
{
    clear();
}

void WebViewExporter::clear()
{
    m_askedToStop = false;

    delete m_viewer;
    m_viewer = nullptr;

    m_htmlTemplate.clear();
    m_exportHtmlTemplate.clear();

    m_exportOngoing = false;
}

bool WebViewExporter::doExport(const ExportOption &p_option,
                               const File *p_file,
                               const QString &p_outputFile)
{
    bool ret = false;
    m_askedToStop = false;

    Q_ASSERT(p_file->getContentType().isMarkdown());

    Q_ASSERT(!m_exportOngoing);
    m_exportOngoing = true;

    m_webViewStates = WebViewState::Started;

    auto baseUrl = PathUtils::pathToUrl(p_file->getContentPath());
    m_viewer->setHtml(m_htmlTemplate, baseUrl);

    auto textContent = p_file->read();
    if (p_option.m_targetFormat == ExportFormat::PDF
        && p_option.m_pdfOption.m_addTableOfContents
        && !p_option.m_pdfOption.m_useWkhtmltopdf) {
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
            qWarning() << "WebView failed when exporting" << p_file->getFilePath();
            goto exit_export;
        }
    }

    qDebug() << "WebView is ready";

    // Add extra wait to make sure Web side is really ready.
    Utils::sleepWait(200);

    switch (p_option.m_targetFormat) {
    case ExportFormat::HTML:
        // TODO: not supported yet.
        Q_ASSERT(!p_option.m_htmlOption.m_useMimeHtmlFormat);
        ret = doExportHtml(p_option.m_htmlOption, p_outputFile, baseUrl);
        break;

    case ExportFormat::PDF:
        if (p_option.m_pdfOption.m_useWkhtmltopdf) {
            ret = doExportWkhtmltopdf(p_option.m_pdfOption, p_outputFile, baseUrl);
        } else {
            ret = doExportPdf(p_option.m_pdfOption, p_outputFile);
        }
        break;

    default:
        break;
    }

exit_export:
    m_exportOngoing = false;
    return ret;
}

void WebViewExporter::stop()
{
    m_askedToStop = true;
}

bool WebViewExporter::isWebViewReady() const
{
    return m_webViewStates == (WebViewState::LoadFinished | WebViewState::WorkFinished);
}

bool WebViewExporter::isWebViewFailed() const
{
    return m_webViewStates & WebViewState::Failed;
}

bool WebViewExporter::doExportHtml(const ExportHtmlOption &p_htmlOption,
                                   const QString &p_outputFile,
                                   const QUrl &p_baseUrl)
{
    ExportState state = ExportState::Busy;

    connect(m_viewer->adapter(), &MarkdownViewerAdapter::contentReady,
            this, [&, this](const QString &p_headContent,
                            const QString &p_styleContent,
                            const QString &p_content,
                            const QString &p_bodyClassList) {
                qDebug() << "doExportHtml contentReady";
                // Maybe unnecessary. Just to avoid duplicated signal connections.
                disconnect(m_viewer->adapter(), &MarkdownViewerAdapter::contentReady, this, 0);

                if (p_content.isEmpty() || m_askedToStop) {
                    state = ExportState::Failed;
                    return;
                }

                if (!writeHtmlFile(p_outputFile,
                                   p_baseUrl,
                                   p_headContent,
                                   p_styleContent,
                                   p_content,
                                   p_bodyClassList,
                                   p_htmlOption.m_embedStyles,
                                   p_htmlOption.m_completePage,
                                   p_htmlOption.m_embedImages)) {
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

bool WebViewExporter::writeHtmlFile(const QString &p_file,
                                    const QUrl &p_baseUrl,
                                    const QString &p_headContent,
                                    QString p_styleContent,
                                    const QString &p_content,
                                    const QString &p_bodyClassList,
                                    bool p_embedStyles,
                                    bool p_completePage,
                                    bool p_embedImages)
{
    const auto baseName = QFileInfo(p_file).completeBaseName();
    auto title = QString("%1 - %2").arg(baseName, ConfigMgr::c_appName);
    const QString resourceFolderName = baseName + "_files";
    auto resourceFolder = PathUtils::concatenateFilePath(PathUtils::parentDirPath(p_file), resourceFolderName);

    qDebug() << "HTML files folder" << resourceFolder;

    auto htmlContent = m_exportHtmlTemplate;
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

QSize WebViewExporter::pageLayoutSize(const QPageLayout &p_layout) const
{
    Q_ASSERT(m_viewer);
    auto rect = p_layout.paintRect(QPageLayout::Inch);
    return QSize(rect.width() * m_viewer->logicalDpiX(), rect.height() * m_viewer->logicalDpiY());
}

void WebViewExporter::prepare(const ExportOption &p_option)
{
    Q_ASSERT(!m_viewer && !m_exportOngoing);
    {
        // Adapter will be managed by MarkdownViewer.
        auto adapter = new MarkdownViewerAdapter(this);
        m_viewer = new MarkdownViewer(adapter, QColor(), 1, static_cast<QWidget *>(parent()));
        m_viewer->hide();
        connect(m_viewer->page(), &QWebEnginePage::loadFinished,
                this, [this]() {
                    m_webViewStates |= WebViewState::LoadFinished;
                });
        connect(adapter, &MarkdownViewerAdapter::workFinished,
                this, [this]() {
                    m_webViewStates |= WebViewState::WorkFinished;
                });
    }

    const bool scrollable = p_option.m_targetFormat != ExportFormat::PDF;
    const auto &config = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();
    bool useWkhtmltopdf = false;
    QSize pageBodySize(1024, 768);
    if (p_option.m_targetFormat == ExportFormat::PDF) {
        useWkhtmltopdf = p_option.m_pdfOption.m_useWkhtmltopdf;
        pageBodySize = pageLayoutSize(*(p_option.m_pdfOption.m_layout));
    }
    qDebug() << "export page body size" << pageBodySize;
    m_htmlTemplate = HtmlTemplateHelper::generateMarkdownViewerTemplate(config,
                                                                        p_option.m_renderingStyleFile,
                                                                        p_option.m_syntaxHighlightStyleFile,
                                                                        p_option.m_useTransparentBg,
                                                                        scrollable,
                                                                        pageBodySize.width(),
                                                                        pageBodySize.height(),
                                                                        useWkhtmltopdf,
                                                                        useWkhtmltopdf ? 2.5 : -1);

    {
        const bool addOutlinePanel = p_option.m_targetFormat == ExportFormat::HTML && p_option.m_htmlOption.m_addOutlinePanel;
        m_exportHtmlTemplate = HtmlTemplateHelper::generateExportTemplate(config, addOutlinePanel);
    }

    if (useWkhtmltopdf) {
        prepareWkhtmltopdfArguments(p_option.m_pdfOption);
    }
}

static QString marginToStrMM(qreal p_margin)
{
    return QString("%1mm").arg(p_margin);
}

void WebViewExporter::prepareWkhtmltopdfArguments(const ExportPdfOption &p_pdfOption)
{
    m_wkhtmltopdfArgs.clear();

    // Page layout.
    {
        const auto &layout = p_pdfOption.m_layout;
        m_wkhtmltopdfArgs << "--page-size" << layout->pageSize().key();
        m_wkhtmltopdfArgs << "--orientation"
                 << (layout->orientation() == QPageLayout::Portrait ? "Portrait" : "Landscape");

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
        m_wkhtmltopdfArgs << "toc" << "--toc-text-size-shrink" << "1.0";
    }
}

bool WebViewExporter::embedStyleResources(QString &p_html) const
{
    bool altered = false;
    QRegExp reg("\\burl\\(\"((file|qrc):[^\"\\)]+)\"\\);");

    int pos = 0;
    while (pos < p_html.size()) {
        int idx = p_html.indexOf(reg, pos);
        if (idx == -1) {
            break;
        }

        QString dataURI = WebUtils::toDataUri(QUrl(reg.cap(1)), false);
        if (dataURI.isEmpty()) {
            pos = idx + reg.matchedLength();
        } else {
            // Replace the url string in html.
            QString newUrl = QString("url('%1');").arg(dataURI);
            p_html.replace(idx, reg.matchedLength(), newUrl);
            pos = idx + newUrl.size();
            altered = true;
        }
    }

    return altered;
}

bool WebViewExporter::embedBodyResources(const QUrl &p_baseUrl, QString &p_html)
{
    bool altered = false;
    if (p_baseUrl.isEmpty()) {
        return altered;
    }

    QRegExp reg(c_imgRegExp);

    int pos = 0;
    while (pos < p_html.size()) {
        int idx = p_html.indexOf(reg, pos);
        if (idx == -1) {
            break;
        }

        if (reg.cap(2).isEmpty()) {
            pos = idx + reg.matchedLength();
            continue;
        }

        QUrl srcUrl(p_baseUrl.resolved(reg.cap(2)));
        const auto dataURI = WebUtils::toDataUri(srcUrl, true);
        if (dataURI.isEmpty()) {
            pos = idx + reg.matchedLength();
        } else {
            // Replace the url string in html.
            QString newUrl = QString("<img %1src='%2'%3>").arg(reg.cap(1), dataURI, reg.cap(3));
            p_html.replace(idx, reg.matchedLength(), newUrl);
            pos = idx + newUrl.size();
            altered = true;
        }
    }

    return altered;
}

static QString getResourceRelativePath(const QString &p_file)
{
    int idx = p_file.lastIndexOf('/');
    int idx2 = p_file.lastIndexOf('/', idx - 1);
    Q_ASSERT(idx > 0 && idx2 < idx);
    return "." + p_file.mid(idx2);
}

bool WebViewExporter::fixBodyResources(const QUrl &p_baseUrl,
                                       const QString &p_folder,
                                       QString &p_html)
{
    bool altered = false;
    if (p_baseUrl.isEmpty()) {
        return altered;
    }

    QRegExp reg(c_imgRegExp);

    int pos = 0;
    while (pos < p_html.size()) {
        int idx = p_html.indexOf(reg, pos);
        if (idx == -1) {
            break;
        }

        if (reg.cap(2).isEmpty()) {
            pos = idx + reg.matchedLength();
            continue;
        }

        QUrl srcUrl(p_baseUrl.resolved(reg.cap(2)));
        QString targetFile = WebUtils::copyResource(srcUrl, p_folder);
        if (targetFile.isEmpty()) {
            pos = idx + reg.matchedLength();
        } else {
            // Replace the url string in html.
            QString newUrl = QString("<img %1src=\"%2\"%3>").arg(reg.cap(1), getResourceRelativePath(targetFile), reg.cap(3));
            p_html.replace(idx, reg.matchedLength(), newUrl);
            pos = idx + newUrl.size();
            altered = true;
        }
    }

    return altered;
}

bool WebViewExporter::doExportPdf(const ExportPdfOption &p_pdfOption, const QString &p_outputFile)
{
    ExportState state = ExportState::Busy;

    m_viewer->page()->printToPdf([&, this](const QByteArray &p_result) {
        qDebug() << "doExportPdf printToPdf ready";
        if (p_result.isEmpty() || m_askedToStop) {
            state = ExportState::Failed;
            return;
        }

        Q_ASSERT(!p_outputFile.isEmpty());

        FileUtils::writeFile(p_outputFile, p_result);

        state = ExportState::Finished;
    }, *p_pdfOption.m_layout);

    while (state == ExportState::Busy) {
        Utils::sleepWait(100);

        if (m_askedToStop) {
            break;
        }
    }

    return state == ExportState::Finished;
}

bool WebViewExporter::doExportWkhtmltopdf(const ExportPdfOption &p_pdfOption, const QString &p_outputFile, const QUrl &p_baseUrl)
{
    if (p_pdfOption.m_wkhtmltopdfExePath.isEmpty()) {
        qWarning() << "invalid wkhtmltopdf executable path";
        return false;
    }

    ExportState state = ExportState::Busy;

    connect(m_viewer->adapter(), &MarkdownViewerAdapter::contentReady,
            this, [&, this](const QString &p_headContent,
                            const QString &p_styleContent,
                            const QString &p_content,
                            const QString &p_bodyClassList) {
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
                if (!writeHtmlFile(tmpHtmlFile,
                                   p_baseUrl,
                                   p_headContent,
                                   p_styleContent,
                                   p_content,
                                   p_bodyClassList,
                                   true,
                                   true,
                                   false)) {
                    state = ExportState::Failed;
                    return;
                }

                // Convert HTML to PDF via wkhtmltopdf.
                if (doWkhtmltopdf(p_pdfOption, QStringList() << tmpHtmlFile, p_outputFile)) {
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

bool WebViewExporter::doWkhtmltopdf(const ExportPdfOption &p_pdfOption, const QStringList &p_htmlFiles, const QString &p_outputFile)
{
    // Note: system's locale settings (Language for non-Unicode programs) is important to wkhtmltopdf.
    // Input file could be encoded via QUrl::fromLocalFile(p_htmlFile).toString(QUrl::EncodeUnicode) to
    // handle non-ASCII path.

    QStringList args(m_wkhtmltopdfArgs);

    // Prepare the args.
    for (auto const &file : p_htmlFiles) {
        args << QDir::toNativeSeparators(file);
    }

    args << QDir::toNativeSeparators(p_outputFile);

    return startProcess(p_pdfOption.m_wkhtmltopdfExePath, args);
}

bool WebViewExporter::startProcess(const QString &p_program, const QStringList &p_args)
{
    emit logRequested(p_program + " " + ProcessUtils::combineArgString(p_args));

    auto ret = ProcessUtils::start(p_program,
                                   p_args,
                                   [this](const QString &p_log) {
                                       emit logRequested(p_log);
                                   },
                                   m_askedToStop);
    return ret == ProcessUtils::State::Succeeded;
}
