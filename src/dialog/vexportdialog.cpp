#include "vexportdialog.h"

#include <QtWidgets>
#include <QCoreApplication>

#ifndef QT_NO_PRINTER
#include <QPrinter>
#include <QPageSetupDialog>
#endif

#include "utils/vutils.h"
#include "vlineedit.h"
#include "vnotebook.h"
#include "vfile.h"
#include "vdirectory.h"
#include "vcart.h"
#include "vconfigmanager.h"
#include "vnotefile.h"
#include "vnote.h"
#include "vexporter.h"

extern VConfigManager *g_config;

extern VNote *g_vnote;

QString VExportDialog::s_lastOutputFolder;

ExportFormat VExportDialog::s_lastExportFormat = ExportFormat::Markdown;

#define LOGERR(x) do { QString msg = (x); \
                       VUtils::addErrMsg(p_errMsg, msg); \
                       appendLogLine(msg); \
                  } while (0)

VExportDialog::VExportDialog(VNotebook *p_notebook,
                             VDirectory *p_directory,
                             VFile *p_file,
                             VCart *p_cart,
                             MarkdownConverterType p_renderer,
                             QWidget *p_parent)
    : QDialog(p_parent),
      m_notebook(p_notebook),
      m_directory(p_directory),
      m_file(p_file),
      m_cart(p_cart),
      m_pageLayout(QPageLayout(QPageSize(QPageSize::A4),
                               QPageLayout::Portrait,
                               QMarginsF(0.3, 0.3, 0.3, 0.3))),
      m_inExport(false),
      m_askedToStop(false)
{
    if (s_lastOutputFolder.isEmpty()) {
        s_lastOutputFolder = g_config->getExportFolderPath();
    }

    setupUI();

    m_exporter = new VExporter(this);

    initUIFields(p_renderer);

    handleInputChanged();
}

void VExportDialog::setupUI()
{
    // Notes to export.
    m_srcCB = VUtils::getComboBox();
    m_srcCB->setToolTip(tr("Choose notes to export"));
    m_srcCB->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

    // Target format.
    m_formatCB = VUtils::getComboBox();
    m_formatCB->setToolTip(tr("Choose target format to export as"));
    m_formatCB->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(m_formatCB, SIGNAL(currentIndexChanged(int)),
            this, SLOT(handleCurrentFormatChanged(int)));

    // Markdown renderer.
    m_rendererCB = VUtils::getComboBox();
    m_rendererCB->setToolTip(tr("Choose converter to render Markdown"));
    m_rendererCB->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    // Markdown rendering background.
    m_renderBgCB = VUtils::getComboBox();
    m_renderBgCB->setToolTip(tr("Choose rendering background color for Markdown"));

    // Markdown rendering style.
    m_renderStyleCB = VUtils::getComboBox();
    m_renderStyleCB->setToolTip(tr("Choose rendering style for Markdown"));

    // Markdown rendering code block style.
    m_renderCodeBlockStyleCB = VUtils::getComboBox();
    m_renderCodeBlockStyleCB->setToolTip(tr("Choose rendering code block style for Markdown"));

    // Output directory.
    m_outputEdit = new VLineEdit(s_lastOutputFolder);
    connect(m_outputEdit, &QLineEdit::textChanged,
            this, &VExportDialog::handleInputChanged);
    m_browseBtn = new QPushButton(tr("&Browse"));
    connect(m_browseBtn, &QPushButton::clicked,
            this, &VExportDialog::handleBrowseBtnClicked);
    QHBoxLayout *outputLayout = new QHBoxLayout();
    outputLayout->addWidget(m_outputEdit);
    outputLayout->addWidget(m_browseBtn);

    m_basicBox = new QGroupBox(tr("Information"));

    m_settingBox = new QGroupBox(tr("Advanced Settings"));

    m_consoleEdit = new QPlainTextEdit();
    m_consoleEdit->setReadOnly(true);
    m_consoleEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_consoleEdit->setProperty("LineEdit", true);
    m_consoleEdit->setPlaceholderText(tr("Output logs will be shown here"));

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Close);
    m_exportBtn = m_btnBox->addButton(tr("Export"), QDialogButtonBox::ActionRole);
    m_openBtn = m_btnBox->addButton(tr("Open Output Directory"), QDialogButtonBox::ActionRole);
    connect(m_btnBox, &QDialogButtonBox::rejected,
            this, [this]() {
                if (m_inExport) {
                    // Just cancel the export. Do not exit.
                    m_askedToStop = true;
                } else {
                    QDialog::reject();
                }
            });

    m_exportBtn->setProperty("SpecialBtn", true);
    connect(m_exportBtn, &QPushButton::clicked,
            this, &VExportDialog::startExport);

    connect(m_openBtn, &QPushButton::clicked,
            this, [this]() {
                QUrl url = QUrl::fromLocalFile(getOutputDirectory());
                QDesktopServices::openUrl(url);
            });

    QFormLayout *basicLayout = new QFormLayout();
    basicLayout->addRow(tr("Notes to export:"), m_srcCB);
    basicLayout->addRow(tr("Target format:"), m_formatCB);
    basicLayout->addRow(tr("Markdown renderer:"), m_rendererCB);
    basicLayout->addRow(tr("Markdown rendering background:"), m_renderBgCB);
    basicLayout->addRow(tr("Markdown rendering style:"), m_renderStyleCB);
    basicLayout->addRow(tr("Markdown rendering code block style:"), m_renderCodeBlockStyleCB);
    basicLayout->addRow(tr("Output directory:"), outputLayout);

    m_basicBox->setLayout(basicLayout);

    // Settings box.
    m_pdfSettings = setupPDFAdvancedSettings();

    QVBoxLayout *advLayout = new QVBoxLayout();
    advLayout->addWidget(m_pdfSettings);

    m_settingBox->setLayout(advLayout);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(m_basicBox);
    mainLayout->addWidget(m_settingBox);
    mainLayout->addWidget(m_consoleEdit);
    mainLayout->addWidget(m_btnBox);

    setLayout(mainLayout);

    setWindowTitle(tr("Export"));
}

QWidget *VExportDialog::setupPDFAdvancedSettings()
{
    // Page layout settings.
    m_layoutLabel = new QLabel();
    m_layoutBtn = new QPushButton(tr("Settings"));

#ifndef QT_NO_PRINTER
    connect(m_layoutBtn, &QPushButton::clicked,
            this, &VExportDialog::handleLayoutBtnClicked);
#else
    m_layoutBtn->hide();
#endif

    updatePageLayoutLabel();

    QHBoxLayout *layoutLayout = new QHBoxLayout();
    layoutLayout->addWidget(m_layoutLabel);
    layoutLayout->addWidget(m_layoutBtn);
    layoutLayout->addStretch();

    QFormLayout *advLayout = new QFormLayout();
    advLayout->addRow(tr("Page layout:"), layoutLayout);

    advLayout->setContentsMargins(0, 0, 0, 0);

    QWidget *wid = new QWidget();
    wid->setLayout(advLayout);

    return wid;
}

void VExportDialog::initUIFields(MarkdownConverterType p_renderer)
{
    // Notes to export.
    if (m_file) {
        m_srcCB->addItem(tr("Current Note (%1)").arg(m_file->getName()),
                         (int)ExportSource::CurrentNote);
    }

    if (m_directory) {
        m_srcCB->addItem(tr("Current Directory (%1)").arg(m_directory->getName()),
                         (int)ExportSource::CurrentDirectory);
    }

    if (m_notebook) {
        m_srcCB->addItem(tr("Current Notebook (%1)").arg(m_notebook->getName()),
                         (int)ExportSource::CurrentNotebook);
    }

    if (m_cart && m_cart->count() > 0) {
        m_srcCB->addItem(tr("Cart (%1)").arg(m_cart->count()),
                         (int)ExportSource::Cart);
    }

    // Export format.
    m_formatCB->addItem(tr("Markdown"), (int)ExportFormat::Markdown);
    m_formatCB->addItem(tr("HTML"), (int)ExportFormat::HTML);
    m_formatCB->addItem(tr("PDF"), (int)ExportFormat::PDF);
    m_formatCB->setCurrentIndex(m_formatCB->findData((int)s_lastExportFormat));

    // Markdown renderer.
    m_rendererCB->addItem(tr("Hoedown"), MarkdownConverterType::Hoedown);
    m_rendererCB->addItem(tr("Marked"), MarkdownConverterType::Marked);
    m_rendererCB->addItem(tr("Markdown-it"), MarkdownConverterType::MarkdownIt);
    m_rendererCB->addItem(tr("Showdown"), MarkdownConverterType::Showdown);
    m_rendererCB->setCurrentIndex(m_rendererCB->findData(p_renderer));

    // Markdown rendering background.
    m_renderBgCB->addItem(tr("System"), "System");
    const QVector<VColor> &bgColors = g_config->getCustomColors();
    for (int i = 0; i < bgColors.size(); ++i) {
        m_renderBgCB->addItem(bgColors[i].m_name, bgColors[i].m_name);
    }

    m_renderBgCB->setCurrentIndex(
        m_renderBgCB->findData(g_config->getCurRenderBackgroundColor()));

    // Markdown rendering style.
    QList<QString> styles = g_config->getCssStyles();
    for (auto const &style : styles) {
        m_renderStyleCB->addItem(style, style);
    }

    m_renderStyleCB->setCurrentIndex(
        m_renderStyleCB->findData(g_config->getCssStyle()));

    // Markdown rendering code block style.
    QList<QString> cbStyles = g_config->getCodeBlockCssStyles();
    for (auto const &style : cbStyles) {
        m_renderCodeBlockStyleCB->addItem(style, style);
    }

    m_renderCodeBlockStyleCB->setCurrentIndex(
        m_renderCodeBlockStyleCB->findData(g_config->getCodeBlockCssStyle()));
}

void VExportDialog::startExport()
{
    if (m_inExport) {
        return;
    }

    m_exportBtn->setEnabled(false);
    m_askedToStop = false;
    m_inExport = true;

    QString outputFolder = QDir::cleanPath(QDir(getOutputDirectory()).absolutePath());

    ExportOption opt((ExportSource)m_srcCB->currentData().toInt(),
                     (ExportFormat)m_formatCB->currentData().toInt(),
                     (MarkdownConverterType)m_rendererCB->currentData().toInt(),
                     m_renderBgCB->currentData().toString(),
                     m_renderStyleCB->currentData().toString(),
                     m_renderCodeBlockStyleCB->currentData().toString(),
                     &m_pageLayout);

    s_lastExportFormat = opt.m_format;

    m_consoleEdit->clear();
    appendLogLine(tr("Export to %1.").arg(outputFolder));

    if (opt.m_format == ExportFormat::PDF
        || opt.m_format == ExportFormat::HTML) {
        m_exporter->prepareExport(opt);
    }

    int ret = 0;
    QString msg;

    switch ((int)opt.m_source) {
    case (int)ExportSource::CurrentNote:
        ret = doExport(m_file, opt, outputFolder, &msg);
        break;

    case (int)ExportSource::CurrentDirectory:
        ret = doExport(m_directory, opt, outputFolder, &msg);
        break;

    case (int)ExportSource::CurrentNotebook:
        ret = doExport(m_notebook, opt, outputFolder, &msg);
        break;

    case (int)ExportSource::Cart:
        ret = doExport(m_cart, opt, outputFolder, &msg);
        break;

    default:
        break;
    }

    if (m_askedToStop) {
        appendLogLine(tr("User cancelled the export. Aborted!"));
        m_askedToStop = false;
    }

    if (!msg.isEmpty()) {
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Warning"),
                            tr("Errors found during export."),
                            msg,
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
    }

    appendLogLine(tr("%1 notes exported.").arg(ret));

    m_inExport = false;
    m_exportBtn->setEnabled(true);
}

QString VExportDialog::getOutputDirectory() const
{
    return m_outputEdit->text();
}

void VExportDialog::handleBrowseBtnClicked()
{
    QString initPath = getOutputDirectory();
    if (!QFileInfo::exists(initPath)) {
        initPath = g_config->getDocumentPathOrHomePath();
    }

    QString dirPath = QFileDialog::getExistingDirectory(this,
                                                        tr("Select Output Directory To Export To"),
                                                        initPath,
                                                        QFileDialog::ShowDirsOnly
                                                        | QFileDialog::DontResolveSymlinks);

    if (!dirPath.isEmpty()) {
        m_outputEdit->setText(dirPath);
        s_lastOutputFolder = dirPath;
    }
}

void VExportDialog::handleInputChanged()
{
    // Source.
    bool sourceOk = true;
    if (m_srcCB->count() == 0) {
        sourceOk = false;
    }

    // Output folder.
    bool pathOk = true;
    QString path = getOutputDirectory();
    if (path.isEmpty() || !VUtils::checkPathLegal(path)) {
        pathOk = false;
    }

    m_exportBtn->setEnabled(sourceOk && pathOk);
    m_openBtn->setEnabled(pathOk);
}

void VExportDialog::appendLogLine(const QString &p_text)
{
    m_consoleEdit->appendPlainText(p_text);
    QCoreApplication::sendPostedEvents();
}

int VExportDialog::doExport(VFile *p_file,
                            const ExportOption &p_opt,
                            const QString &p_outputFolder,
                            QString *p_errMsg)
{
    Q_ASSERT(p_file);

    appendLogLine(tr("Exporting note %1.").arg(p_file->fetchPath()));

    int ret = 0;
    switch ((int)p_opt.m_format) {
    case (int)ExportFormat::Markdown:
        ret = doExportMarkdown(p_file, p_opt, p_outputFolder, p_errMsg);
        break;

    case (int)ExportFormat::PDF:
        ret = doExportPDF(p_file, p_opt, p_outputFolder, p_errMsg);
        break;

    case (int)ExportFormat::HTML:
        ret = doExportHTML(p_file, p_opt, p_outputFolder, p_errMsg);
        break;

    default:
        break;
    }

    return ret;
}

int VExportDialog::doExport(VDirectory *p_directory,
                            const ExportOption &p_opt,
                            const QString &p_outputFolder,
                            QString *p_errMsg)
{
    Q_ASSERT(p_directory);

    bool opened = p_directory->isOpened();
    if (!opened && !p_directory->open()) {
        LOGERR(tr("Fail to open folder %1.").arg(p_directory->fetchRelativePath()));
        return 0;
    }

    int ret = 0;

    QString folderName = VUtils::getDirNameWithSequence(p_outputFolder,
                                                        p_directory->getName());
    QString outputPath = QDir(p_outputFolder).filePath(folderName);
    if (!VUtils::makePath(outputPath)) {
        LOGERR(tr("Fail to create directory %1.").arg(outputPath));
        goto exit;
    }

    // Export child notes.
    for (auto const & file : p_directory->getFiles()) {
        if (!checkUserAction()) {
            goto exit;
        }

        ret += doExport(file, p_opt, outputPath, p_errMsg);
    }

    // Export subfolder.
    for (auto const & dir : p_directory->getSubDirs()) {
        if (!checkUserAction()) {
            goto exit;
        }

        ret += doExport(dir, p_opt, outputPath, p_errMsg);
    }

exit:
    if (!opened) {
        p_directory->close();
    }

    return ret;
}

int VExportDialog::doExport(VNotebook *p_notebook,
                            const ExportOption &p_opt,
                            const QString &p_outputFolder,
                            QString *p_errMsg)
{
    Q_ASSERT(p_notebook);

    bool opened = p_notebook->isOpened();
    if (!opened && !p_notebook->open()) {
        LOGERR(tr("Fail to open notebook %1.").arg(p_notebook->getName()));
        return 0;
    }

    int ret = 0;

    QString folderName = VUtils::getDirNameWithSequence(p_outputFolder,
                                                        p_notebook->getName());
    QString outputPath = QDir(p_outputFolder).filePath(folderName);
    if (!VUtils::makePath(outputPath)) {
        LOGERR(tr("Fail to create directory %1.").arg(outputPath));
        goto exit;
    }

    // Export subfolder.
    for (auto const & dir : p_notebook->getRootDir()->getSubDirs()) {
        if (!checkUserAction()) {
            goto exit;
        }

        ret += doExport(dir, p_opt, outputPath, p_errMsg);
    }

exit:
    if (!opened) {
        p_notebook->close();
    }

    return ret;
}

int VExportDialog::doExport(VCart *p_cart,
                            const ExportOption &p_opt,
                            const QString &p_outputFolder,
                            QString *p_errMsg)
{
    Q_ASSERT(p_cart);

    int ret = 0;

    QVector<QString> files = m_cart->getFiles();
    for (auto const & it : files) {
        VFile *file = g_vnote->getFile(it);
        if (!file) {
            LOGERR(tr("Fail to open file %1.").arg(it));
            continue;
        }

        ret += doExport(file, p_opt, p_outputFolder, p_errMsg);
    }

    return ret;
}

int VExportDialog::doExportMarkdown(VFile *p_file,
                                    const ExportOption &p_opt,
                                    const QString &p_outputFolder,
                                    QString *p_errMsg)
{
    Q_UNUSED(p_opt);

    QString srcFilePath(p_file->fetchPath());

    if (p_file->getDocType() != DocType::Markdown) {
        LOGERR(tr("Skip exporting non-Markdown file %1 as Markdown.").arg(srcFilePath));
        return 0;
    }

    // Export it to a folder with the same name.
    QString name = VUtils::getDirNameWithSequence(p_outputFolder, p_file->getName());
    QString outputPath = QDir(p_outputFolder).filePath(name);
    if (!VUtils::makePath(outputPath)) {
        LOGERR(tr("Fail to create directory %1.").arg(outputPath));
        return 0;
    }

    // Copy the note file.
    QString destPath = QDir(outputPath).filePath(p_file->getName());
    if (!VUtils::copyFile(srcFilePath, destPath, false)) {
        LOGERR(tr("Fail to copy the note file %1.").arg(srcFilePath));
        return 0;
    }

    // Copy images.
    int ret = 1;
    int nrImageCopied = 0;
    QVector<ImageLink> images = VUtils::fetchImagesFromMarkdownFile(p_file,
                                                                    ImageLink::LocalRelativeInternal);
    if (!VNoteFile::copyInternalImages(images,
                                       outputPath,
                                       false,
                                       &nrImageCopied,
                                       p_errMsg)) {
        ret = 0;
        appendLogLine(tr("Fail to copy images of note %1.").arg(srcFilePath));
    }

    // Copy attachments.
    if (p_file->getType() == FileType::Note) {
        VNoteFile *noteFile = static_cast<VNoteFile *>(p_file);
        QString attaFolder = noteFile->getAttachmentFolder();
        if (!attaFolder.isEmpty()) {
            QString attaFolderPath;
            attaFolderPath = noteFile->fetchAttachmentFolderPath();
            attaFolder = VUtils::getDirNameWithSequence(outputPath, attaFolder);
            QString folderPath = QDir(outputPath).filePath(attaFolder);

            // Copy attaFolder to folderPath.
            if (!VUtils::copyDirectory(attaFolderPath, folderPath, false)) {
                LOGERR(tr("Fail to copy attachments folder %1 to %2.")
                         .arg(attaFolderPath).arg(folderPath));
                ret = 0;
            }
        }
    }

    if (ret) {
        appendLogLine(tr("Note %1 exported to %2.").arg(srcFilePath).arg(outputPath));
    }

    return ret;
}

int VExportDialog::doExportPDF(VFile *p_file,
                               const ExportOption &p_opt,
                               const QString &p_outputFolder,
                               QString *p_errMsg)
{
    Q_UNUSED(p_opt);

    QString srcFilePath(p_file->fetchPath());

    if (p_file->getDocType() != DocType::Markdown) {
        LOGERR(tr("Skip exporting non-Markdown file %1 as PDF.").arg(srcFilePath));
        return 0;
    }

    if (!VUtils::makePath(p_outputFolder)) {
        LOGERR(tr("Fail to create directory %1.").arg(p_outputFolder));
        return 0;
    }

    // Get output file.
    QString suffix = ".pdf";
    QString name = VUtils::getFileNameWithSequence(p_outputFolder,
                                                   QFileInfo(p_file->getName()).completeBaseName() + suffix);
    QString outputPath = QDir(p_outputFolder).filePath(name);

    if (m_exporter->exportPDF(p_file, p_opt, outputPath, p_errMsg)) {
        appendLogLine(tr("Note %1 exported to %2.").arg(srcFilePath).arg(outputPath));
        return 1;
    } else {
        appendLogLine(tr("Fail to export note %1.").arg(srcFilePath));
        return 0;
    }
}

int VExportDialog::doExportHTML(VFile *p_file,
                                const ExportOption &p_opt,
                                const QString &p_outputFolder,
                                QString *p_errMsg)
{
    Q_UNUSED(p_opt);

    QString srcFilePath(p_file->fetchPath());

    if (p_file->getDocType() != DocType::Markdown) {
        LOGERR(tr("Skip exporting non-Markdown file %1 as HTML.").arg(srcFilePath));
        return 0;
    }

    if (!VUtils::makePath(p_outputFolder)) {
        LOGERR(tr("Fail to create directory %1.").arg(p_outputFolder));
        return 0;
    }

    // Get output file.
    QString suffix = ".html";
    QString name = VUtils::getFileNameWithSequence(p_outputFolder,
                                                   QFileInfo(p_file->getName()).completeBaseName() + suffix);
    QString outputPath = QDir(p_outputFolder).filePath(name);

    if (m_exporter->exportHTML(p_file, p_opt, outputPath, p_errMsg)) {
        appendLogLine(tr("Note %1 exported to %2.").arg(srcFilePath).arg(outputPath));
        return 1;
    } else {
        appendLogLine(tr("Fail to export note %1.").arg(srcFilePath));
        return 0;
    }
}

bool VExportDialog::checkUserAction()
{
    if (m_askedToStop) {
        return false;
    }

    QCoreApplication::processEvents();

    return true;
}

void VExportDialog::handleLayoutBtnClicked()
{
#ifndef QT_NO_PRINTER
    QPrinter printer;
    printer.setPageLayout(m_pageLayout);

    QPageSetupDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    m_pageLayout.setPageSize(printer.pageLayout().pageSize());
    m_pageLayout.setMargins(printer.pageLayout().margins());
    m_pageLayout.setOrientation(printer.pageLayout().orientation());

    updatePageLayoutLabel();
#endif
}

void VExportDialog::updatePageLayoutLabel()
{
    qDebug() << "page layout margins:" << m_pageLayout.margins();

    m_layoutLabel->setText(QString("%1, %2").arg(m_pageLayout.pageSize().name())
                                            .arg(m_pageLayout.orientation() == QPageLayout::Portrait ?
                                                 tr("Portrait") : tr("Landscape")));
}

void VExportDialog::handleCurrentFormatChanged(int p_index)
{
    bool pdfEnabled = false;

    if (p_index >= 0) {
        switch (m_formatCB->currentData().toInt()) {
        case (int)ExportFormat::PDF:
            pdfEnabled = true;
            break;

        default:
            break;
        }
    }

    m_pdfSettings->setVisible(pdfEnabled);
}
