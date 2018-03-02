#include "vexportdialog.h"

#include <QtWidgets>
#include <QCoreApplication>
#include <QProcess>

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
#include "vlineedit.h"

extern VConfigManager *g_config;

extern VNote *g_vnote;

QString VExportDialog::s_lastOutputFolder;

ExportOption VExportDialog::s_opt;

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
                               QMarginsF(10, 16, 10, 10),
                               QPageLayout::Millimeter)),
      m_inExport(false),
      m_askedToStop(false)
{
    if (s_lastOutputFolder.isEmpty()) {
        s_lastOutputFolder = g_config->getExportFolderPath();
    }

    setupUI();

    m_exporter = new VExporter(this);
    connect(m_exporter, &VExporter::outputLog,
            this, [this](const QString &p_log) {
                appendLogLine(p_log);
            });

    initUIFields(p_renderer);

    handleInputChanged();
}

void VExportDialog::setupUI()
{
    // Notes to export.
    m_srcCB = VUtils::getComboBox();
    m_srcCB->setToolTip(tr("Choose notes to export"));
    m_srcCB->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(m_srcCB, SIGNAL(currentIndexChanged(int)),
            this, SLOT(handleCurrentSrcChanged(int)));

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
    QPushButton *browseBtn = new QPushButton(tr("&Browse"));
    connect(browseBtn, &QPushButton::clicked,
            this, &VExportDialog::handleOutputBrowseBtnClicked);
    QHBoxLayout *outputLayout = new QHBoxLayout();
    outputLayout->addWidget(m_outputEdit);
    outputLayout->addWidget(browseBtn);

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
    m_generalSettings = setupGeneralAdvancedSettings();
    m_htmlSettings = setupHTMLAdvancedSettings();
    m_pdfSettings = setupPDFAdvancedSettings();

    QVBoxLayout *advLayout = new QVBoxLayout();
    advLayout->addWidget(m_generalSettings);
    advLayout->addWidget(m_htmlSettings);
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
    QPushButton *layoutBtn = new QPushButton(tr("Settings"));

#ifndef QT_NO_PRINTER
    connect(layoutBtn, &QPushButton::clicked,
            this, &VExportDialog::handleLayoutBtnClicked);
#else
    layoutBtn->hide();
#endif

    updatePageLayoutLabel();

    QHBoxLayout *layoutLayout = new QHBoxLayout();
    layoutLayout->addWidget(m_layoutLabel);
    layoutLayout->addWidget(layoutBtn);
    layoutLayout->addStretch();

    // Use wkhtmltopdf.
    m_wkhtmltopdfCB = new QCheckBox(tr("Use wkhtmltopdf"));
    m_wkhtmltopdfCB->setToolTip(tr("Use wkhtmltopdf tool to generate PDF (wkhtmltopdf needed to be installed)"));
    connect(m_wkhtmltopdfCB, &QCheckBox::stateChanged,
            this, [this](int p_state) {
                bool checked = p_state == Qt::Checked;
                m_wkPathEdit->setEnabled(checked);
                m_wkPathBrowseBtn->setEnabled(checked);
                m_wkBackgroundCB->setEnabled(checked);
                m_wkTableOfContentsCB->setEnabled(checked);
                m_wkPageNumberCB->setEnabled(checked);
                m_wkExtraArgsEdit->setEnabled(checked);
            });

    QPushButton *wkBtn = new QPushButton(tr("Download wkhtmltopdf"));
    connect(wkBtn, &QPushButton::clicked,
            this, [this]() {
                QString url("https://wkhtmltopdf.org/downloads.html");
                QDesktopServices::openUrl(QUrl(url));
            });

    QHBoxLayout *wkLayout = new QHBoxLayout();
    wkLayout->addWidget(m_wkhtmltopdfCB);
    wkLayout->addStretch();
    wkLayout->addWidget(wkBtn);

    // wkhtmltopdf Path.
    m_wkPathEdit = new VLineEdit();
    m_wkPathEdit->setToolTip(tr("Tell VNote where to find wkhtmlpdf tool"));
    m_wkPathEdit->setEnabled(m_wkhtmltopdfCB->isChecked());

    m_wkPathBrowseBtn = new QPushButton(tr("&Browse"));
    m_wkPathBrowseBtn->setEnabled(m_wkhtmltopdfCB->isChecked());
    connect(m_wkPathBrowseBtn, &QPushButton::clicked,
            this, &VExportDialog::handleWkPathBrowseBtnClicked);

    QHBoxLayout *wkPathLayout = new QHBoxLayout();
    wkPathLayout->addWidget(m_wkPathEdit);
    wkPathLayout->addWidget(m_wkPathBrowseBtn);

    // wkhtmltopdf enable background.
    m_wkBackgroundCB = new QCheckBox(tr("Enable background"));
    m_wkBackgroundCB->setToolTip(tr("Enable background when printing"));
    m_wkBackgroundCB->setEnabled(m_wkhtmltopdfCB->isChecked());

    // wkhtmltopdf enable table of contents.
    m_wkTableOfContentsCB = new QCheckBox(tr("Enable Table Of Contents"));
    m_wkTableOfContentsCB->setToolTip(tr("Add a table of contents to the document"));
    m_wkTableOfContentsCB->setEnabled(m_wkhtmltopdfCB->isChecked());

    // wkhtmltopdf page number.
    m_wkPageNumberCB = VUtils::getComboBox();
    m_wkPageNumberCB->setToolTip(tr("Append page number as footer"));
    m_wkPageNumberCB->setEnabled(m_wkhtmltopdfCB->isChecked());

    // wkhtmltopdf extra argumnets.
    m_wkExtraArgsEdit = new VLineEdit();
    m_wkExtraArgsEdit->setToolTip(tr("Additional arguments passed to wkhtmltopdf"));
    m_wkExtraArgsEdit->setPlaceholderText(tr("Use \" to enclose arguments containing space"));
    m_wkExtraArgsEdit->setEnabled(m_wkhtmltopdfCB->isChecked());

    QFormLayout *advLayout = new QFormLayout();
    advLayout->addRow(tr("Page layout:"), layoutLayout);
    advLayout->addRow(wkLayout);
    advLayout->addRow(tr("wkhtmltopdf path:"), wkPathLayout);
    advLayout->addRow(m_wkBackgroundCB);
    advLayout->addRow(m_wkTableOfContentsCB);
    advLayout->addRow(tr("Page number:"), m_wkPageNumberCB);
    advLayout->addRow(tr("Additional arguments:"), m_wkExtraArgsEdit);

    advLayout->setContentsMargins(0, 0, 0, 0);

    QWidget *wid = new QWidget();
    wid->setLayout(advLayout);

    return wid;
}

QWidget *VExportDialog::setupHTMLAdvancedSettings()
{
    // Embed CSS styles.
    m_embedStyleCB = new QCheckBox(tr("Embed CSS styles"), this);
    m_embedStyleCB->setToolTip(tr("Embed CSS styles in HTML file"));

    // Complete HTML.
    m_completeHTMLCB = new QCheckBox(tr("Complete page"), this);
    m_completeHTMLCB->setToolTip(tr("Export the whole web page along with pictures "
                                    "which may not keep the HTML link structure of "
                                    "the original page"));

    // Mime HTML.
    m_mimeHTMLCB = new QCheckBox(tr("MIME HTML"), this);
    m_mimeHTMLCB->setToolTip(tr("Export as a complete web page in MIME HTML format"));
    connect(m_mimeHTMLCB, &QCheckBox::stateChanged,
            this, [this](int p_state) {
                bool checked = p_state == Qt::Checked;
                m_embedStyleCB->setEnabled(!checked);
                m_completeHTMLCB->setEnabled(!checked);
            });

    QFormLayout *advLayout = new QFormLayout();
    advLayout->addRow(m_embedStyleCB);
    advLayout->addRow(m_completeHTMLCB);
    advLayout->addRow(m_mimeHTMLCB);

    advLayout->setContentsMargins(0, 0, 0, 0);

    QWidget *wid = new QWidget();
    wid->setLayout(advLayout);

    return wid;
}

QWidget *VExportDialog::setupGeneralAdvancedSettings()
{
    // Include subfolders.
    m_subfolderCB = new QCheckBox(tr("Process subfolders"));
    m_subfolderCB->setToolTip(tr("Process subfolders recursively"));
    m_subfolderCB->setChecked(true);

    QFormLayout *advLayout = new QFormLayout();
    advLayout->addRow(m_subfolderCB);

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
        m_srcCB->addItem(tr("Current Folder (%1)").arg(m_directory->getName()),
                         (int)ExportSource::CurrentFolder);
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
    m_formatCB->setCurrentIndex(m_formatCB->findData((int)s_opt.m_format));

    // Markdown renderer.
    m_rendererCB->addItem(tr("Hoedown"), MarkdownConverterType::Hoedown);
    m_rendererCB->addItem(tr("Marked"), MarkdownConverterType::Marked);
    m_rendererCB->addItem(tr("Markdown-it"), MarkdownConverterType::MarkdownIt);
    m_rendererCB->addItem(tr("Showdown"), MarkdownConverterType::Showdown);
    m_rendererCB->setCurrentIndex(m_rendererCB->findData(p_renderer));

    // Markdown rendering background.
    m_renderBgCB->addItem(tr("System"), "System");
    m_renderBgCB->addItem(tr("Transparent"), "Transparent");
    const QVector<VColor> &bgColors = g_config->getCustomColors();
    for (int i = 0; i < bgColors.size(); ++i) {
        m_renderBgCB->addItem(bgColors[i].m_name, bgColors[i].m_name);
    }

    if (s_opt.m_renderBg.isEmpty()) {
        s_opt.m_renderBg = g_config->getCurRenderBackgroundColor();
    }

    m_renderBgCB->setCurrentIndex(m_renderBgCB->findData(s_opt.m_renderBg));

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

    m_embedStyleCB->setChecked(s_opt.m_htmlOpt.m_embedCssStyle);

    m_completeHTMLCB->setChecked(s_opt.m_htmlOpt.m_completeHTML);

    m_mimeHTMLCB->setChecked(s_opt.m_htmlOpt.m_mimeHTML);

    m_wkhtmltopdfCB->setChecked(s_opt.m_pdfOpt.m_wkhtmltopdf);

    // wkhtmltopdf path.
    m_wkPathEdit->setText(g_config->getWkhtmltopdfPath());

    m_wkBackgroundCB->setChecked(s_opt.m_pdfOpt.m_wkEnableBackground);

    m_wkTableOfContentsCB->setChecked(s_opt.m_pdfOpt.m_wkEnableTableOfContents);

    // wkhtmltopdf page number.
    m_wkPageNumberCB->addItem(tr("None"), (int)ExportPageNumber::None);
    m_wkPageNumberCB->addItem(tr("Left"), (int)ExportPageNumber::Left);
    m_wkPageNumberCB->addItem(tr("Center"), (int)ExportPageNumber::Center);
    m_wkPageNumberCB->addItem(tr("Right"), (int)ExportPageNumber::Right);
    m_wkPageNumberCB->setCurrentIndex(m_wkPageNumberCB->findData((int)s_opt.m_pdfOpt.m_wkPageNumber));

    m_wkExtraArgsEdit->setText(g_config->getWkhtmltopdfArgs());
}

bool VExportDialog::checkWkhtmltopdfExecutable(const QString &p_file)
{
    QStringList args;
    args << "--version";
    int ret = QProcess::execute(p_file, args);
    switch (ret) {
    case -2:
        appendLogLine(tr("Fail to start wkhtmltopdf."));
        break;

    case -1:
        appendLogLine(tr("wkhtmltopdf crashed."));
        break;

    case 0:
        appendLogLine(tr("Use %1.").arg(p_file));
        break;

    default:
        appendLogLine(tr("wkhtmltopdf returned %1.").arg(ret));
        break;
    }

    return ret == 0;
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

    s_opt = ExportOption((ExportSource)m_srcCB->currentData().toInt(),
                         (ExportFormat)m_formatCB->currentData().toInt(),
                         (MarkdownConverterType)m_rendererCB->currentData().toInt(),
                         m_renderBgCB->currentData().toString(),
                         m_renderStyleCB->currentData().toString(),
                         m_renderCodeBlockStyleCB->currentData().toString(),
                         m_subfolderCB->isChecked(),
                         ExportPDFOption(&m_pageLayout,
                                         m_wkhtmltopdfCB->isChecked(),
                                         QDir::toNativeSeparators(m_wkPathEdit->text()),
                                         m_wkBackgroundCB->isChecked(),
                                         m_wkTableOfContentsCB->isChecked(),
                                         (ExportPageNumber)m_wkPageNumberCB->currentData().toInt(),
                                         m_wkExtraArgsEdit->text()),
                         ExportHTMLOption(m_embedStyleCB->isChecked(),
                                          m_completeHTMLCB->isChecked(),
                                          m_mimeHTMLCB->isChecked()));

    m_consoleEdit->clear();
    appendLogLine(tr("Export to %1.").arg(outputFolder));

    if (s_opt.m_format == ExportFormat::PDF
        || s_opt.m_format == ExportFormat::HTML) {
        m_exporter->prepareExport(s_opt);

        if (s_opt.m_format == ExportFormat::PDF
            && s_opt.m_pdfOpt.m_wkhtmltopdf) {
            g_config->setWkhtmltopdfPath(s_opt.m_pdfOpt.m_wkPath);
            g_config->setWkhtmltopdfArgs(s_opt.m_pdfOpt.m_wkExtraArgs);

            if (!checkWkhtmltopdfExecutable(s_opt.m_pdfOpt.m_wkPath)) {
                m_inExport = false;
                m_exportBtn->setEnabled(true);
                return;
            }
        }
    }

    int ret = 0;
    QString msg;

    switch (s_opt.m_source) {
    case ExportSource::CurrentNote:
        ret = doExport(m_file, s_opt, outputFolder, &msg);
        break;

    case ExportSource::CurrentFolder:
        ret = doExport(m_directory, s_opt, outputFolder, &msg);
        break;

    case ExportSource::CurrentNotebook:
        ret = doExport(m_notebook, s_opt, outputFolder, &msg);
        break;

    case ExportSource::Cart:
        ret = doExport(m_cart, s_opt, outputFolder, &msg);
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

void VExportDialog::handleOutputBrowseBtnClicked()
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

void VExportDialog::handleWkPathBrowseBtnClicked()
{
    QString initPath = m_wkPathEdit->text();
    if (!QFileInfo::exists(initPath)) {
        initPath = QCoreApplication::applicationDirPath();
    }

#if defined(Q_OS_WIN)
    QString filter = tr("Executable (*.exe)");
#else
    QString filter;
#endif

    QString filePath = QFileDialog::getOpenFileName(this,
                                                    tr("Select wkhtmltopdf Executable"),
                                                    initPath,
                                                    filter);

    if (!filePath.isEmpty()) {
        m_wkPathEdit->setText(filePath);
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

    // Export subfolders.
    if (p_opt.m_processSubfolders) {
        for (auto const & dir : p_directory->getSubDirs()) {
            if (!checkUserAction()) {
                goto exit;
            }

            ret += doExport(dir, p_opt, outputPath, p_errMsg);
        }
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
    QString suffix = p_opt.m_htmlOpt.m_mimeHTML ? ".mht" : ".html";
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

    m_pageLayout.setUnits(QPageLayout::Millimeter);
    m_pageLayout.setPageSize(printer.pageLayout().pageSize());
    m_pageLayout.setMargins(printer.pageLayout().margins(QPageLayout::Millimeter));
    m_pageLayout.setOrientation(printer.pageLayout().orientation());

    updatePageLayoutLabel();
#endif
}

void VExportDialog::updatePageLayoutLabel()
{
    m_layoutLabel->setText(QString("%1, %2").arg(m_pageLayout.pageSize().name())
                                            .arg(m_pageLayout.orientation() == QPageLayout::Portrait ?
                                                 tr("Portrait") : tr("Landscape")));
}

void VExportDialog::handleCurrentFormatChanged(int p_index)
{
    bool pdfEnabled = false;
    bool htmlEnabled = false;

    if (p_index >= 0) {
        switch (m_formatCB->currentData().toInt()) {
        case (int)ExportFormat::PDF:
            pdfEnabled = true;
            break;

        case (int)ExportFormat::HTML:
            htmlEnabled = true;
            break;

        default:
            break;
        }
    }

    m_pdfSettings->setVisible(pdfEnabled);
    m_htmlSettings->setVisible(htmlEnabled);
}

void VExportDialog::handleCurrentSrcChanged(int p_index)
{
    bool subfolderEnabled = false;

    if (p_index >= 0) {
        switch (m_srcCB->currentData().toInt()) {
        case (int)ExportSource::CurrentFolder:
            subfolderEnabled = true;
            break;

        default:
            break;
        }
    }

    m_subfolderCB->setVisible(subfolderEnabled);
}
