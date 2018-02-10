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
    m_htmlTemplate = VUtils::generateHtmlTemplate(p_opt.m_renderer, true);
    m_pageLayout = *(p_opt.m_layout);
}

bool VExporter::exportPDF(VFile *p_file,
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
    bool exportRet = exportToPDF(m_webViewer,
                                 p_outputFile,
                                 m_pageLayout);

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

void VExporter::initWebViewer(VFile *p_file, const ExportOption &p_opt)
{
    Q_ASSERT(!m_webViewer);

    m_webViewer = new VWebView(p_file, static_cast<QWidget *>(parent()));
    m_webViewer->hide();

    VPreviewPage *page = new VPreviewPage(m_webViewer);
    m_webViewer->setPage(page);

    connect(page, &VPreviewPage::loadFinished,
            this, &VExporter::handleLoadFinished);

    VDocument *document = new VDocument(p_file, m_webViewer);
    connect(document, &VDocument::logicsFinished,
            this, &VExporter::handleLogicsFinished);

    QWebChannel *channel = new QWebChannel(m_webViewer);
    channel->registerObject(QStringLiteral("content"), document);
    page->setWebChannel(channel);

    // Need to generate HTML using Hoedown.
    if (p_opt.m_renderer == MarkdownConverterType::Hoedown) {
        VMarkdownConverter mdConverter;
        QString toc;
        QString html = mdConverter.generateHtml(p_file->getContent(),
                                                g_config->getMarkdownExtensions(),
                                                toc);
        document->setHtml(html);
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
    if (m_webViewer) {
        delete m_webViewer;
        m_webViewer = NULL;
    }
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

