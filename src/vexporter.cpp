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

extern VConfigManager vconfig;

QString VExporter::s_defaultPathDir = QDir::homePath();

VExporter::VExporter(MarkdownConverterType p_mdType, QWidget *p_parent)
    : QDialog(p_parent), m_document(NULL, this), m_mdType(p_mdType),
      m_file(NULL), m_type(ExportType::PDF), m_source(ExportSource::Invalid),
      m_webReady(false), m_state(ExportState::Idle),
      m_pageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(0.0, 0.0, 0.0, 0.0)))
{
    setupUI();
}

void VExporter::setupUI()
{
    setupMarkdownViewer();

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
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &VExporter::startExport);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &VExporter::cancelExport);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    m_pathEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

    QGridLayout *mainLayout = new QGridLayout();
    mainLayout->addWidget(m_webViewer, 0, 0, 1, 3);
    mainLayout->addWidget(m_infoLabel, 1, 0, 1, 3);
    mainLayout->addWidget(pathLabel, 2, 0);
    mainLayout->addWidget(m_pathEdit, 2, 1);
    mainLayout->addWidget(m_browseBtn, 2, 2);
    mainLayout->addWidget(layoutLabel, 3, 0);
    mainLayout->addWidget(m_layoutLabel, 3, 1);
    mainLayout->addWidget(m_layoutBtn, 3, 2);
    mainLayout->addWidget(m_proLabel, 4, 1, 1, 2);
    mainLayout->addWidget(m_proBar, 5, 1, 1, 2);
    mainLayout->addWidget(m_btnBox, 6, 1, 1, 2);

    // Only use VWebView to do the conversion.
    m_webViewer->hide();

    m_proLabel->hide();
    m_proBar->hide();

    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(tr("Export Note"));

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

void VExporter::setupMarkdownViewer()
{
    m_webViewer = new VWebView(NULL, this);
    VPreviewPage *page = new VPreviewPage(this);
    m_webViewer->setPage(page);

    QWebChannel *channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("content"), &m_document);
    page->setWebChannel(channel);

    connect(&m_document, &VDocument::loadFinished,
            this, &VExporter::readyToExport);

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

void VExporter::updateWebViewer(VFile *p_file)
{
    m_document.setFile(p_file);

    // Need to generate HTML using Hoedown.
    if (m_mdType == MarkdownConverterType::Hoedown) {
        VMarkdownConverter mdConverter;
        QString toc;
        QString html = mdConverter.generateHtml(p_file->getContent(),
                                                vconfig.getMarkdownExtensions(),
                                                toc);
        m_document.setHtml(html);
    }

    m_webViewer->setHtml(m_htmlTemplate, p_file->getBaseUrl());
}

void VExporter::readyToExport()
{
    Q_ASSERT(!m_webReady);
    m_webReady = true;
}

void VExporter::startExport()
{
    enableUserInput(false);
    V_ASSERT(m_state == ExportState::Idle);
    m_state = ExportState::Busy;

    if (m_source == ExportSource::Note) {
        V_ASSERT(m_file);
        bool isOpened = m_file->isOpened();
        if (!isOpened && !m_file->open()) {
            goto exit;
        }

        m_webReady = false;
        updateWebViewer(m_file);

        // Update progress info.
        m_proLabel->setText(tr("Exporting %1").arg(m_file->getName()));
        m_proBar->setMinimum(0);
        m_proBar->setMaximum(100);
        m_proBar->reset();
        m_proLabel->show();
        m_proBar->show();

        while (!m_webReady) {
            VUtils::sleepWait(100);
            if (m_proBar->value() < 70) {
                m_proBar->setValue(m_proBar->value() + 1);
            }

            if (m_state == ExportState::Cancelled) {
                goto exit;
            }
        }

        // Wait to ensure Web side is really ready.
        VUtils::sleepWait(200);

        if (m_state == ExportState::Cancelled) {
            goto exit;
        }

        m_proBar->setValue(80);

        exportToPDF(m_webViewer, getFilePath(), m_pageLayout);

        m_proBar->setValue(100);

        m_webReady = false;

        if (!isOpened) {
            m_file->close();
        }
    }

exit:
    m_proLabel->setText("");
    m_proBar->reset();
    m_proLabel->hide();
    m_proBar->hide();
    enableUserInput(true);

    if (m_state == ExportState::Cancelled) {
        reject();
    } else {
        accept();
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

void VExporter::exportToPDF(VWebView *p_webViewer, const QString &p_filePath,
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

    qDebug() << "export to PDF" << p_filePath << "state" << pdfPrinted;
}

void VExporter::enableUserInput(bool p_enabled)
{
    m_btnBox->button(QDialogButtonBox::Ok)->setEnabled(p_enabled);
    m_pathEdit->setEnabled(p_enabled);
    m_browseBtn->setEnabled(p_enabled);
    m_layoutBtn->setEnabled(p_enabled);
}
