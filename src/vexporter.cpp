#include "vexporter.h"

#include <QDebug>
#include <QWidget>
#include <QWebChannel>

#include "vconfigmanager.h"
#include "vfile.h"
#include "vwebview.h"
#include "utils/vutils.h"
#include "vpreviewpage.h"
#include "vconstants.h"
#include "vmarkdownconverter.h"
#include "vdocument.h"

extern VConfigManager *g_config;

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

    m_webViewer->setHtml(m_htmlTemplate, p_file->getBaseUrl());
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

        QFile file(p_filePath);

        if (!file.open(QFile::WriteOnly)) {
            pdfPrinted = -1;
            return;
        }

        file.write(p_result.data(), p_result.size());
        file.close();

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
                             const QString &p_filePath)
{
    Q_UNUSED(p_webViewer);
    int htmlExported = 0;

    connect(p_webDocument, &VDocument::htmlContentFinished,
            this, [&, this](const QString &p_headContent, const QString &p_bodyContent) {
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

                QString html(m_exportHtmlTemplate);
                html.replace(HtmlHolder::c_headHolder, p_headContent);
                html.replace(HtmlHolder::c_bodyHolder, p_bodyContent);

                file.write(html.toUtf8());
                file.close();

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
