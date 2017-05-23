#include "vexporter.h"

#include <QtWidgets>
#include <QFileInfo>
#include <QDir>
#include <QWebChannel>
#include <QDebug>
#include <QVBoxLayout>
#include <QShowEvent>

#ifndef QT_NO_PRINTER
#include <QPrinter>
#include <QPageSetupDialog>
#endif

#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "vfile.h"
#include "vwebview.h"
#include "vpreviewpage.h"
#include "vconstants.h"
#include "vnote.h"
#include "vmarkdownconverter.h"
#include "vdocument.h"

extern VConfigManager vconfig;

QString VExporter::s_defaultPathDir = QDir::homePath();

VExporter::VExporter(MarkdownConverterType p_mdType, QWidget *p_parent)
    : QDialog(p_parent), m_webViewer(NULL), m_mdType(p_mdType),
      m_file(NULL), m_type(ExportType::PDF), m_source(ExportSource::Invalid),
      m_noteState(NoteState::NotReady), m_state(ExportState::Idle),
      m_pageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(0.0, 0.0, 0.0, 0.0)))
{
    initMarkdownTemplate();

    setupUI();
}

void VExporter::initMarkdownTemplate()
{
    QString jsFile, extraFile;
    switch (m_mdType) {
    case MarkdownConverterType::Marked:
        jsFile = "qrc" + VNote::c_markedJsFile;
        extraFile = "<script src=\"qrc" + VNote::c_markedExtraFile + "\"></script>\n";
        break;

    case MarkdownConverterType::Hoedown:
        jsFile = "qrc" + VNote::c_hoedownJsFile;
        // Use Marked to highlight code blocks.
        extraFile = "<script src=\"qrc" + VNote::c_markedExtraFile + "\"></script>\n";
        break;

    case MarkdownConverterType::MarkdownIt:
        jsFile = "qrc" + VNote::c_markdownitJsFile;
        extraFile = "<script src=\"qrc" + VNote::c_markdownitExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitAnchorExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitTaskListExtraFile + "\"></script>\n";
        break;

    case MarkdownConverterType::Showdown:
        jsFile = "qrc" + VNote::c_showdownJsFile;
        extraFile = "<script src=\"qrc" + VNote::c_showdownExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_showdownAnchorExtraFile + "\"></script>\n";

        break;

    default:
        Q_ASSERT(false);
    }

    if (vconfig.getEnableMermaid()) {
        extraFile += "<link rel=\"stylesheet\" type=\"text/css\" href=\"qrc" + VNote::c_mermaidCssFile +
                     "\"/>\n" + "<script src=\"qrc" + VNote::c_mermaidApiJsFile + "\"></script>\n" +
                     "<script>var VEnableMermaid = true;</script>\n";
    }

    if (vconfig.getEnableMathjax()) {
        extraFile += "<script type=\"text/x-mathjax-config\">"
                     "MathJax.Hub.Config({\n"
                     "                    tex2jax: {inlineMath: [['$','$'], ['\\\\(','\\\\)']]},\n"
                     "                    showProcessingMessages: false,\n"
                     "                    messageStyle: \"none\"});\n"
                     "</script>\n"
                     "<script type=\"text/javascript\" async src=\"" + VNote::c_mathjaxJsFile + "\"></script>\n" +
                     "<script>var VEnableMathjax = true;</script>\n";
    }

    if (vconfig.getEnableImageCaption()) {
        extraFile += "<script>var VEnableImageCaption = true;</script>\n";
    }

    m_htmlTemplate = VNote::s_markdownTemplatePDF;
    m_htmlTemplate.replace(c_htmlJSHolder, jsFile);
    if (!extraFile.isEmpty()) {
        m_htmlTemplate.replace(c_htmlExtraHolder, extraFile);
    }
}

void VExporter::setupUI()
{
    m_infoLabel = new QLabel();
    m_infoLabel->setWordWrap(true);

    // Target file path.
    QLabel *pathLabel = new QLabel(tr("Target &path:"));
    m_pathEdit = new QLineEdit();
    pathLabel->setBuddy(m_pathEdit);
    m_browseBtn = new QPushButton(tr("&Browse"));
    connect(m_browseBtn, &QPushButton::clicked,
            this, &VExporter::handleBrowseBtnClicked);

    // Page layout.
    QLabel *layoutLabel = new QLabel(tr("Page layout:"));
    m_layoutLabel = new QLabel();
    m_layoutBtn = new QPushButton(tr("&Settings"));

#ifndef QT_NO_PRINTER
    connect(m_layoutBtn, &QPushButton::clicked,
            this, &VExporter::handleLayoutBtnClicked);
#else
    m_layoutBtn->hide();
#endif

    // Progress.
    m_proLabel = new QLabel(this);
    m_proBar = new QProgressBar(this);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_openBtn = m_btnBox->addButton(tr("Open File Location"), QDialogButtonBox::ActionRole);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &VExporter::startExport);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &VExporter::cancelExport);
    connect(m_openBtn, &QPushButton::clicked, this, &VExporter::openTargetPath);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    m_pathEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

    QGridLayout *mainLayout = new QGridLayout();
    mainLayout->addWidget(m_infoLabel, 0, 0, 1, 3);
    mainLayout->addWidget(pathLabel, 1, 0);
    mainLayout->addWidget(m_pathEdit, 1, 1);
    mainLayout->addWidget(m_browseBtn, 1, 2);
    mainLayout->addWidget(layoutLabel, 2, 0);
    mainLayout->addWidget(m_layoutLabel, 2, 1);
    mainLayout->addWidget(m_layoutBtn, 2, 2);
    mainLayout->addWidget(m_proLabel, 3, 1, 1, 2);
    mainLayout->addWidget(m_proBar, 4, 1, 1, 2);
    mainLayout->addWidget(m_btnBox, 5, 1, 1, 2);

    m_proLabel->hide();
    m_proBar->hide();

    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(tr("Export Note"));

    m_openBtn->hide();

    updatePageLayoutLabel();
}

static QString exportTypeStr(ExportType p_type)
{
    if (p_type == ExportType::PDF) {
        return "PDF";
    } else {
        return "HTML";
    }
}

void VExporter::handleBrowseBtnClicked()
{
    QFileInfo fi(getFilePath());
    QString fileType = m_type == ExportType::PDF ?
                       tr("Portable Document Format (*.pdf)") :
                       tr("WebPage, Complete (*.html)");
    QString path = QFileDialog::getSaveFileName(this, tr("Export As"),
                                                fi.absolutePath(),
                                                fileType);
    if (path.isEmpty()) {
        return;
    }

    setFilePath(path);
    s_defaultPathDir = VUtils::basePathFromPath(path);

    m_openBtn->hide();
}

void VExporter::handleLayoutBtnClicked()
{
#ifndef QT_NO_PRINTER
    QPrinter printer;
    printer.setPageLayout(m_pageLayout);

    QPageSetupDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    m_pageLayout.setPageSize(printer.pageLayout().pageSize());
    m_pageLayout.setOrientation(printer.pageLayout().orientation());

    updatePageLayoutLabel();
#endif
}

void VExporter::updatePageLayoutLabel()
{
    m_layoutLabel->setText(QString("%1, %2").arg(m_pageLayout.pageSize().name())
                                            .arg(m_pageLayout.orientation() == QPageLayout::Portrait ?
                                                 tr("Portrait") : tr("Landscape")));
}

QString VExporter::getFilePath() const
{
    return QDir::cleanPath(m_pathEdit->text());
}

void VExporter::setFilePath(const QString &p_path)
{
    m_pathEdit->setText(QDir::toNativeSeparators(p_path));
}

void VExporter::exportNote(VFile *p_file, ExportType p_type)
{
    m_file = p_file;
    m_type = p_type;
    m_source = ExportSource::Note;

    if (!m_file || m_file->getDocType() != DocType::Markdown) {
        // Do not support non-Markdown note now.
        m_btnBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }

    m_infoLabel->setText(tr("Export note <span style=\"%1\">%2</span> as %3.")
                            .arg(vconfig.c_dataTextStyle)
                            .arg(m_file->getName())
                            .arg(exportTypeStr(p_type)));

    setWindowTitle(tr("Export As %1").arg(exportTypeStr(p_type)));

    setFilePath(QDir(s_defaultPathDir).filePath(QFileInfo(p_file->retrivePath()).baseName() +
                                                "." + exportTypeStr(p_type).toLower()));
}

void VExporter::initWebViewer(VFile *p_file)
{
    V_ASSERT(!m_webViewer);

    m_webViewer = new VWebView(p_file, this);
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

    qDebug() << "VPreviewPage" << page->parent() << "QWebChannel" << channel->parent();

    // Need to generate HTML using Hoedown.
    if (m_mdType == MarkdownConverterType::Hoedown) {
        VMarkdownConverter mdConverter;
        QString toc;
        QString html = mdConverter.generateHtml(p_file->getContent(),
                                                vconfig.getMarkdownExtensions(),
                                                toc);
        document->setHtml(html);
    }

    m_webViewer->setHtml(m_htmlTemplate, p_file->getBaseUrl());
}

void VExporter::clearWebViewer()
{
    if (m_webViewer) {
        delete m_webViewer;
        m_webViewer = NULL;
    }
}

void VExporter::handleLogicsFinished()
{
    Q_ASSERT(!(m_noteState & NoteState::WebLogicsReady));
    m_noteState = NoteState(m_noteState | NoteState::WebLogicsReady);
}

void VExporter::handleLoadFinished(bool p_ok)
{
    qDebug() << "Web load finished" << p_ok;

    Q_ASSERT(!(m_noteState & NoteState::WebLoadFinished));
    m_noteState = NoteState(m_noteState | NoteState::WebLoadFinished);

    if (!p_ok) {
        m_noteState = NoteState(m_noteState | NoteState::Failed);
    }
}

void VExporter::clearNoteState()
{
    m_noteState = NoteState::NotReady;
}

bool VExporter::isNoteStateReady() const
{
    return m_noteState == NoteState::Ready;
}

bool VExporter::isNoteStateFailed() const
{
    return m_noteState & NoteState::Failed;
}

void VExporter::startExport()
{
    int exportedNum = 0;
    enableUserInput(false);
    V_ASSERT(m_state == ExportState::Idle);
    m_state = ExportState::Busy;

    m_openBtn->hide();

    if (m_source == ExportSource::Note) {
        V_ASSERT(m_file);
        bool isOpened = m_file->isOpened();
        if (!isOpened && !m_file->open()) {
            goto exit;
        }

        clearNoteState();
        initWebViewer(m_file);

        // Update progress info.
        m_proLabel->setText(tr("Exporting %1").arg(m_file->getName()));
        m_proBar->setEnabled(true);
        m_proBar->setMinimum(0);
        m_proBar->setMaximum(100);
        m_proBar->reset();
        m_proLabel->show();
        m_proBar->show();

        while (!isNoteStateReady()) {
            VUtils::sleepWait(100);
            if (m_proBar->value() < 70) {
                m_proBar->setValue(m_proBar->value() + 1);
            }

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

        m_proBar->setValue(80);

        bool exportRet = exportToPDF(m_webViewer, getFilePath(), m_pageLayout);

        clearNoteState();

        if (!isOpened) {
            m_file->close();
        }

        if (exportRet) {
            m_proBar->setValue(100);
            m_state = ExportState::Successful;
            exportedNum++;
        } else {
            m_proBar->setEnabled(false);
            m_state = ExportState::Failed;
        }
    }

exit:
    clearWebViewer();

    m_proLabel->setText("");
    m_proLabel->hide();
    enableUserInput(true);

    if (m_state == ExportState::Cancelled) {
        reject();
    }

    if (exportedNum) {
        m_openBtn->show();
    }

    m_state = ExportState::Idle;
}

void VExporter::cancelExport()
{
    if (m_state == ExportState::Idle) {
        reject();
    } else {
        m_state = ExportState::Cancelled;
    }
}

bool VExporter::exportToPDF(VWebView *p_webViewer, const QString &p_filePath,
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

void VExporter::enableUserInput(bool p_enabled)
{
    m_btnBox->button(QDialogButtonBox::Ok)->setEnabled(p_enabled);
    m_pathEdit->setEnabled(p_enabled);
    m_browseBtn->setEnabled(p_enabled);
    m_layoutBtn->setEnabled(p_enabled);
}

void VExporter::openTargetPath() const
{
    QUrl url = QUrl::fromLocalFile(VUtils::basePathFromPath(getFilePath()));
    QDesktopServices::openUrl(url);
}
