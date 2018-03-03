#include "vexporter.h"

#include <QDebug>
#include <QWidget>
#include <QWebChannel>
#include <QWebEngineProfile>
#include <QRegExp>
#include <QProcess>
#include <QTemporaryDir>
#include <QScopedPointer>
#include <QCoreApplication>

#include "vconfigmanager.h"
#include "vfile.h"
#include "vwebview.h"
#include "utils/vutils.h"
#include "vpreviewpage.h"
#include "vconstants.h"
#include "vmarkdownconverter.h"
#include "vdocument.h"
#include "utils/vwebutils.h"

extern VConfigManager *g_config;

extern VWebUtils *g_webUtils;

VExporter::VExporter(QWidget *p_parent)
    : QObject(p_parent),
      m_webViewer(NULL),
      m_state(ExportState::Idle),
      m_askedToStop(false)
{
}

static QString marginToStrMM(qreal p_margin)
{
    return QString("%1mm").arg(p_margin);
}

void VExporter::prepareExport(const ExportOption &p_opt)
{
    bool isPdf = p_opt.m_format == ExportFormat::PDF
                 || p_opt.m_format == ExportFormat::OnePDF;

    m_htmlTemplate = VUtils::generateHtmlTemplate(p_opt.m_renderer,
                                                  p_opt.m_renderBg,
                                                  p_opt.m_renderStyle,
                                                  p_opt.m_renderCodeBlockStyle,
                                                  isPdf,
                                                  isPdf && p_opt.m_pdfOpt.m_wkhtmltopdf);

    m_exportHtmlTemplate = VUtils::generateExportHtmlTemplate(p_opt.m_renderBg,
                                                              isPdf && p_opt.m_pdfOpt.m_wkhtmltopdf);

    m_pageLayout = *(p_opt.m_pdfOpt.m_layout);

    prepareWKArguments(p_opt.m_pdfOpt);
}

// From QProcess code.
static QStringList parseCombinedArgString(const QString &program)
{
    QStringList args;
    QString tmp;
    int quoteCount = 0;
    bool inQuote = false;

    // handle quoting. tokens can be surrounded by double quotes
    // "hello world". three consecutive double quotes represent
    // the quote character itself.
    for (int i = 0; i < program.size(); ++i) {
        if (program.at(i) == QLatin1Char('"')) {
            ++quoteCount;
            if (quoteCount == 3) {
                // third consecutive quote
                quoteCount = 0;
                tmp += program.at(i);
            }
            continue;
        }
        if (quoteCount) {
            if (quoteCount == 1)
                inQuote = !inQuote;
            quoteCount = 0;
        }
        if (!inQuote && program.at(i).isSpace()) {
            if (!tmp.isEmpty()) {
                args += tmp;
                tmp.clear();
            }
        } else {
            tmp += program.at(i);
        }
    }
    if (!tmp.isEmpty())
        args += tmp;

    return args;
}

void VExporter::prepareWKArguments(const ExportPDFOption &p_opt)
{
    m_wkArgs.clear();
    m_wkArgs << "--page-size" << m_pageLayout.pageSize().key();
    m_wkArgs << "--orientation"
             << (m_pageLayout.orientation() == QPageLayout::Portrait ? "Portrait" : "Landscape");

    QMarginsF marginsMM = m_pageLayout.margins(QPageLayout::Millimeter);
    m_wkArgs << "--margin-bottom" << marginToStrMM(marginsMM.bottom());
    m_wkArgs << "--margin-left" << marginToStrMM(marginsMM.left());
    m_wkArgs << "--margin-right" << marginToStrMM(marginsMM.right());
    m_wkArgs << "--margin-top" << marginToStrMM(marginsMM.top());

    QString footer;
    switch (p_opt.m_wkPageNumber) {
    case ExportPageNumber::Left:
        footer = "--footer-left";
        break;

    case ExportPageNumber::Center:
        footer = "--footer-center";
        break;

    case ExportPageNumber::Right:
        footer = "--footer-right";
        break;

    default:
        break;
    }

    if (!footer.isEmpty()) {
        m_wkArgs << footer << "[page]"
                 << "--footer-spacing" << QString::number(marginsMM.bottom() / 3, 'f', 2);
    }

    // Title.
    if (!p_opt.m_wkTitle.isEmpty()) {
        m_wkArgs << "--title" << p_opt.m_wkTitle;
    }

    m_wkArgs << "--encoding" << "utf-8";
    m_wkArgs << (p_opt.m_wkEnableBackground ? "--background" : "--no-background");

    // Delay for MathJax.
    if (p_opt.m_wkhtmltopdf) {
        m_wkArgs << "--javascript-delay" << "10000";
    }

    // Append additional global option.
    if (!p_opt.m_wkExtraArgs.isEmpty()) {
        m_wkArgs.append(parseCombinedArgString(p_opt.m_wkExtraArgs));
    }

    // TOC option.
    if (p_opt.m_wkEnableTableOfContents) {
        m_wkArgs << "toc" << "--toc-text-size-shrink" << "1.0";
    }
}

bool VExporter::exportPDF(VFile *p_file,
                          const ExportOption &p_opt,
                          const QString &p_outputFile,
                          QString *p_errMsg)
{
    return exportViaWebView(p_file, p_opt, p_outputFile, p_errMsg);
}

bool VExporter::exportHTML(VFile *p_file,
                           const ExportOption &p_opt,
                           const QString &p_outputFile,
                           QString *p_errMsg)
{
    return exportViaWebView(p_file, p_opt, p_outputFile, p_errMsg);
}

void VExporter::initWebViewer(VFile *p_file, const ExportOption &p_opt)
{
    Q_ASSERT(!m_webViewer);

    m_webViewer = new VWebView(p_file, static_cast<QWidget *>(parent()));
    m_webViewer->hide();

    VPreviewPage *page = new VPreviewPage(m_webViewer);
    m_webViewer->setPage(page);
    connect(page, &VPreviewPage::loadFinished,
            this, &VExporter::handleLoadFinished);
    connect(page->profile(), &QWebEngineProfile::downloadRequested,
            this, &VExporter::handleDownloadRequested);

    m_webDocument = new VDocument(p_file, m_webViewer);
    connect(m_webDocument, &VDocument::logicsFinished,
            this, &VExporter::handleLogicsFinished);

    QWebChannel *channel = new QWebChannel(m_webViewer);
    channel->registerObject(QStringLiteral("content"), m_webDocument);
    page->setWebChannel(channel);

    // Need to generate HTML using Hoedown.
    if (p_opt.m_renderer == MarkdownConverterType::Hoedown) {
        VMarkdownConverter mdConverter;
        QString toc;
        QString html = mdConverter.generateHtml(p_file->getContent(),
                                                g_config->getMarkdownExtensions(),
                                                toc);
        m_webDocument->setHtml(html);
    }

    m_baseUrl = p_file->getBaseUrl();
    m_webViewer->setHtml(m_htmlTemplate, m_baseUrl);
}

void VExporter::handleLogicsFinished()
{
    Q_ASSERT(!(m_noteState & NoteState::WebLogicsReady));
    m_noteState = NoteState(m_noteState | NoteState::WebLogicsReady);
}

void VExporter::handleLoadFinished(bool p_ok)
{
    Q_ASSERT(!(m_noteState & NoteState::WebLoadFinished));
    m_noteState = NoteState(m_noteState | NoteState::WebLoadFinished);

    if (!p_ok) {
        m_noteState = NoteState(m_noteState | NoteState::Failed);
    }
}

void VExporter::clearWebViewer()
{
    // m_webDocument will be freeed by QObject.
    delete m_webViewer;
    m_webViewer = NULL;
    m_webDocument = NULL;
    m_baseUrl.clear();
}

bool VExporter::exportToPDF(VWebView *p_webViewer,
                            const QString &p_filePath,
                            const QPageLayout &p_layout)
{
    int pdfPrinted = 0;
    p_webViewer->page()->printToPdf([&, this](const QByteArray &p_result) {
        if (p_result.isEmpty() || this->m_state == ExportState::Cancelled) {
            pdfPrinted = -1;
            return;
        }

        V_ASSERT(!p_filePath.isEmpty());

        if (!VUtils::writeFileToDisk(p_filePath, p_result)) {
            pdfPrinted = -1;
            return;
        }

        pdfPrinted = 1;
    }, p_layout);

    while (pdfPrinted == 0) {
        VUtils::sleepWait(100);

        if (m_state == ExportState::Cancelled) {
            break;
        }
    }

    return pdfPrinted == 1;
}

bool VExporter::exportToPDFViaWK(VDocument *p_webDocument,
                                 const ExportPDFOption &p_opt,
                                 const QString &p_filePath,
                                 QString *p_errMsg)
{
    int pdfExported = 0;

    connect(p_webDocument, &VDocument::htmlContentFinished,
            this, [&, this](const QString &p_headContent,
                            const QString &p_styleContent,
                            const QString &p_bodyContent) {
                if (p_bodyContent.isEmpty() || this->m_state == ExportState::Cancelled) {
                    pdfExported = -1;
                    return;
                }

                Q_ASSERT(!p_filePath.isEmpty());

                // Save HTML to a temp dir.
                QTemporaryDir tmpDir;
                if (!tmpDir.isValid()) {
                    pdfExported = -1;
                    return;
                }

                QString htmlPath = tmpDir.filePath("vnote_tmp.html");

                QFile file(htmlPath);
                if (!file.open(QFile::WriteOnly)) {
                    pdfExported = -1;
                    return;
                }

                QString resFolder = QFileInfo(htmlPath).completeBaseName() + "_files";
                QString resFolderPath = QDir(VUtils::basePathFromPath(htmlPath)).filePath(resFolder);

                qDebug() << "temp HTML files folder" << resFolderPath;

                QString html(m_exportHtmlTemplate);
                if (!p_styleContent.isEmpty()) {
                    QString content(p_styleContent);
                    fixStyleResources(resFolderPath, content);
                    html.replace(HtmlHolder::c_styleHolder, content);
                }

                if (!p_headContent.isEmpty()) {
                    html.replace(HtmlHolder::c_headHolder, p_headContent);
                }

                QString content(p_bodyContent);
                fixBodyResources(m_baseUrl, resFolderPath, content);
                html.replace(HtmlHolder::c_bodyHolder, content);

                file.write(html.toUtf8());
                file.close();

                // Convert via wkhtmltopdf.
                QList<QString> files;
                files.append(htmlPath);
                if (!htmlsToPDFViaWK(files, p_filePath, p_opt, p_errMsg)) {
                    pdfExported = -1;
                } else {
                    pdfExported = 1;
                }
            });

    p_webDocument->getHtmlContentAsync();

    while (pdfExported == 0) {
        VUtils::sleepWait(100);

        if (m_state == ExportState::Cancelled) {
            break;
        }
    }

    return pdfExported == 1;
}

bool VExporter::exportViaWebView(VFile *p_file,
                                 const ExportOption &p_opt,
                                 const QString &p_outputFile,
                                 QString *p_errMsg)
{
    Q_UNUSED(p_errMsg);

    bool ret = false;

    bool isOpened = p_file->isOpened();
    if (!isOpened && !p_file->open()) {
        goto exit;
    }

    Q_ASSERT(m_state == ExportState::Idle);
    m_state = ExportState::Busy;

    clearNoteState();

    initWebViewer(p_file, p_opt);

    while (!isNoteStateReady()) {
        VUtils::sleepWait(100);

        if (m_state == ExportState::Cancelled) {
            goto exit;
        }

        if (isNoteStateFailed()) {
            m_state = ExportState::Failed;
            goto exit;
        }
    }

    // Wait to ensure Web side is really ready.
    VUtils::sleepWait(200);

    if (m_state == ExportState::Cancelled) {
        goto exit;
    }

    {

    bool exportRet = false;
    switch (p_opt.m_format) {
    case ExportFormat::PDF:
        V_FALLTHROUGH;
    case ExportFormat::OnePDF:
        if (p_opt.m_pdfOpt.m_wkhtmltopdf) {
            exportRet = exportToPDFViaWK(m_webDocument,
                                         p_opt.m_pdfOpt,
                                         p_outputFile,
                                         p_errMsg);
        } else {
            exportRet = exportToPDF(m_webViewer,
                                    p_outputFile,
                                    m_pageLayout);
        }

        break;

    case ExportFormat::HTML:
        if (p_opt.m_htmlOpt.m_mimeHTML) {
            exportRet = exportToMHTML(m_webViewer,
                                      p_opt.m_htmlOpt,
                                      p_outputFile);
        } else {
            exportRet = exportToHTML(m_webDocument,
                                     p_opt.m_htmlOpt,
                                     p_outputFile);
        }

        break;

    default:
        break;
    }

    clearNoteState();

    if (!isOpened) {
        p_file->close();
    }

    if (exportRet) {
        m_state = ExportState::Successful;
    } else {
        m_state = ExportState::Failed;
    }

    }

exit:
    clearWebViewer();

    if (m_state == ExportState::Successful) {
        ret = true;
    }

    m_state = ExportState::Idle;

    return ret;
}

bool VExporter::exportToHTML(VDocument *p_webDocument,
                             const ExportHTMLOption &p_opt,
                             const QString &p_filePath)
{
    int htmlExported = 0;

    connect(p_webDocument, &VDocument::htmlContentFinished,
            this, [&, this](const QString &p_headContent,
                            const QString &p_styleContent,
                            const QString &p_bodyContent) {
                if (p_bodyContent.isEmpty() || this->m_state == ExportState::Cancelled) {
                    htmlExported = -1;
                    return;
                }

                Q_ASSERT(!p_filePath.isEmpty());

                QFile file(p_filePath);
                if (!file.open(QFile::WriteOnly)) {
                    htmlExported = -1;
                    return;
                }

                QString resFolder = QFileInfo(p_filePath).completeBaseName() + "_files";
                QString resFolderPath = QDir(VUtils::basePathFromPath(p_filePath)).filePath(resFolder);

                qDebug() << "HTML files folder" << resFolderPath;

                QString html(m_exportHtmlTemplate);
                if (!p_styleContent.isEmpty() && p_opt.m_embedCssStyle) {
                    QString content(p_styleContent);
                    fixStyleResources(resFolderPath, content);
                    html.replace(HtmlHolder::c_styleHolder, content);
                }

                if (!p_headContent.isEmpty()) {
                    html.replace(HtmlHolder::c_headHolder, p_headContent);
                }

                if (p_opt.m_completeHTML) {
                    QString content(p_bodyContent);
                    fixBodyResources(m_baseUrl, resFolderPath, content);
                    html.replace(HtmlHolder::c_bodyHolder, content);
                } else {
                    html.replace(HtmlHolder::c_bodyHolder, p_bodyContent);
                }

                file.write(html.toUtf8());
                file.close();

                // Delete empty resource folder.
                QDir dir(resFolderPath);
                if (dir.isEmpty()) {
                    dir.cdUp();
                    dir.rmdir(resFolder);
                }

                htmlExported = 1;
            });

    p_webDocument->getHtmlContentAsync();

    while (htmlExported == 0) {
        VUtils::sleepWait(100);

        if (m_state == ExportState::Cancelled) {
            break;
        }
    }

    return htmlExported == 1;
}

bool VExporter::fixStyleResources(const QString &p_folder,
                                  QString &p_html)
{
    bool altered = false;
    QRegExp reg("\\burl\\(\"((file|qrc):[^\"\\)]+)\"\\);");

    int pos = 0;
    while (pos < p_html.size()) {
        int idx = p_html.indexOf(reg, pos);
        if (idx == -1) {
            break;
        }

        QString targetFile = g_webUtils->copyResource(QUrl(reg.cap(1)), p_folder);
        if (targetFile.isEmpty()) {
            pos = idx + reg.matchedLength();
        } else {
            // Replace the url string in html.
            QString newUrl = QString("url(\"%1\");").arg(getResourceRelativePath(targetFile));
            p_html.replace(idx, reg.matchedLength(), newUrl);
            pos = idx + newUrl.size();
            altered = true;
        }
    }

    return altered;
}

bool VExporter::fixBodyResources(const QUrl &p_baseUrl,
                                 const QString &p_folder,
                                 QString &p_html)
{
    bool altered = false;
    if (p_baseUrl.isEmpty()) {
        return altered;
    }

    QRegExp reg("<img ([^>]*)src=\"([^\"]+)\"([^>]*)>");

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
        QString targetFile = g_webUtils->copyResource(srcUrl, p_folder);
        if (targetFile.isEmpty()) {
            pos = idx + reg.matchedLength();
        } else {
            // Replace the url string in html.
            QString newUrl = QString("<img %1src=\"%2\"%3>").arg(reg.cap(1))
                                                            .arg(getResourceRelativePath(targetFile))
                                                            .arg(reg.cap(3));
            p_html.replace(idx, reg.matchedLength(), newUrl);
            pos = idx + newUrl.size();
            altered = true;
        }
    }

    return altered;
}

QString VExporter::getResourceRelativePath(const QString &p_file)
{
    int idx = p_file.lastIndexOf('/');
    int idx2 = p_file.lastIndexOf('/', idx - 1);
    Q_ASSERT(idx > 0 && idx2 < idx);
    return "." + p_file.mid(idx2);
}

bool VExporter::exportToMHTML(VWebView *p_webViewer,
                              const ExportHTMLOption &p_opt,
                              const QString &p_filePath)
{
    Q_UNUSED(p_opt);

    m_downloadState = QWebEngineDownloadItem::DownloadRequested;

    p_webViewer->page()->save(p_filePath, QWebEngineDownloadItem::MimeHtmlSaveFormat);

    while (m_downloadState == QWebEngineDownloadItem::DownloadRequested
           || m_downloadState == QWebEngineDownloadItem::DownloadInProgress) {
        VUtils::sleepWait(100);
    }

    return m_downloadState == QWebEngineDownloadItem::DownloadCompleted;
}

void VExporter::handleDownloadRequested(QWebEngineDownloadItem *p_item)
{
    if (p_item->savePageFormat() == QWebEngineDownloadItem::MimeHtmlSaveFormat) {
        connect(p_item, &QWebEngineDownloadItem::stateChanged,
                this, [this](QWebEngineDownloadItem::DownloadState p_state) {
                    m_downloadState = p_state;
                });
    }
}

static QString combineArgs(QStringList &p_args)
{
    QString str;
    for (const QString &arg : p_args) {
        QString tmp;
        if (arg.contains(' ')) {
            tmp = '"' + arg + '"';
        } else {
            tmp = arg;
        }

        if (str.isEmpty()) {
            str = tmp;
        } else {
            str = str + ' ' + tmp;
        }
    }

    return str;
}

bool VExporter::htmlsToPDFViaWK(const QList<QString> &p_htmlFiles,
                                const QString &p_filePath,
                                const ExportPDFOption &p_opt,
                                QString *p_errMsg)
{
    // Note: system's locale settings (Language for non-Unicode programs) is important to wkhtmltopdf.
    // Input file could be encoded via QUrl::fromLocalFile(p_htmlFile).toString(QUrl::EncodeUnicode) to
    // handle non-ASCII path.

    QStringList args(m_wkArgs);

    for (auto const & it : p_htmlFiles) {
        args << QDir::toNativeSeparators(it);
    }

    args << QDir::toNativeSeparators(p_filePath);

    QString cmd = p_opt.m_wkPath + " " + combineArgs(args);
    emit outputLog(cmd);
    qDebug() << "wkhtmltopdf cmd:" << cmd;
    int ret = startProcess(p_opt.m_wkPath, args);
    qDebug() << "wkhtmltopdf returned" << ret;
    if (m_askedToStop) {
        return ret == 0;
    }

    switch (ret) {
    case -2:
        VUtils::addErrMsg(p_errMsg, tr("Fail to start wkhtmltopdf (%1).").arg(cmd));
        break;

    case -1:
        VUtils::addErrMsg(p_errMsg, tr("wkhtmltopdf crashed (%1).").arg(cmd));
        break;

    default:
        break;
    }

    return ret == 0;
}

int VExporter::exportPDFInOne(const QList<QString> &p_htmlFiles,
                              const ExportOption &p_opt,
                              const QString &p_outputFile,
                              QString *p_errMsg)
{
    if (!htmlsToPDFViaWK(p_htmlFiles, p_outputFile, p_opt.m_pdfOpt, p_errMsg)) {
        return 0;
    }

    return p_htmlFiles.size();
}

int VExporter::startProcess(const QString &p_program, const QStringList &p_args)
{
    int ret = 0;
    QScopedPointer<QProcess> process(new QProcess(this));
    process->start(p_program, p_args);
    bool finished = false;
    bool started = false;
    while (true) {
        QProcess::ProcessError err = process->error();
        if (err == QProcess::FailedToStart
            || err == QProcess::Crashed) {
            emit outputLog(tr("QProcess error %1.").arg(err));
            if (err == QProcess::FailedToStart) {
                ret = -2;
            } else {
                ret = -1;
            }

            break;
        }

        if (started) {
            if (process->state() == QProcess::NotRunning) {
                finished = true;
            }
        } else {
            if (process->state() != QProcess::NotRunning) {
                started = true;
            }
        }

        if (process->waitForFinished(500)) {
            // Finished.
            finished = true;
        }

        QByteArray outBa = process->readAllStandardOutput();
        QByteArray errBa = process->readAllStandardError();
        QString msg;
        if (!outBa.isEmpty()) {
            msg += QString::fromLocal8Bit(outBa);
        }

        if (!errBa.isEmpty()) {
            msg += QString::fromLocal8Bit(errBa);
        }

        if (!msg.isEmpty()) {
            emit outputLog(msg);
        }

        if (finished) {
            QProcess::ExitStatus sta = process->exitStatus();
            if (sta == QProcess::CrashExit) {
                ret = -1;
                break;
            }

            ret = process->exitCode();
            break;
        }

        QCoreApplication::processEvents();

        if (m_askedToStop) {
            process->kill();
            ret = -1;
            break;
        }
    }

    return ret;
}
