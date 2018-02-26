#include "vexporter.h"

#include <QDebug>
#include <QWidget>
#include <QWebChannel>
#include <QRegExp>

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
      m_state(ExportState::Idle)
{
}

void VExporter::prepareExport(const ExportOption &p_opt)
{
    m_htmlTemplate = VUtils::generateHtmlTemplate(p_opt.m_renderer,
                                                  p_opt.m_renderBg,
                                                  p_opt.m_renderStyle,
                                                  p_opt.m_renderCodeBlockStyle,
                                                  p_opt.m_format == ExportFormat::PDF);

    m_exportHtmlTemplate = VUtils::generateExportHtmlTemplate(p_opt.m_renderBg);

    m_pageLayout = *(p_opt.m_layout);
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
        exportRet = exportToPDF(m_webViewer,
                                p_outputFile,
                                m_pageLayout);
        break;

    case ExportFormat::HTML:
        exportRet = exportToHTML(m_webViewer,
                                 m_webDocument,
                                 p_opt.m_embedCssStyle,
                                 p_opt.m_completeHTML,
                                 p_outputFile);
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

bool VExporter::exportToHTML(VWebView *p_webViewer,
                             VDocument *p_webDocument,
                             bool p_embedCssStyle,
                             bool p_completeHTML,
                             const QString &p_filePath)
{
    Q_UNUSED(p_webViewer);
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
                if (!p_styleContent.isEmpty() && p_embedCssStyle) {
                    QString content(p_styleContent);
                    fixStyleResources(resFolderPath, content);
                    html.replace(HtmlHolder::c_styleHolder, content);
                }

                if (!p_headContent.isEmpty()) {
                    html.replace(HtmlHolder::c_headHolder, p_headContent);
                }

                if (p_completeHTML) {
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
