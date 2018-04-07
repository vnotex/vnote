#include "vexportdialog.h"

#include <QtWidgets>
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>

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
    m_srcCB->setSizeAdjustPolicy(QComboBox::AdjustToContents);
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
    m_consoleEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
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
                    m_exporter->setAskedToStop(true);
                    appendLogLine(tr("Cancelling the export..."));
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

    // Progress bar.
    m_proBar = new QProgressBar();
    m_proBar->setRange(0, 0);
    m_proBar->hide();

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addWidget(m_proBar);
    btnLayout->addWidget(m_btnBox);

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
    m_customSettings = setupCustomAdvancedSettings();

    QVBoxLayout *advLayout = new QVBoxLayout();
    advLayout->addWidget(m_generalSettings);
    advLayout->addWidget(m_htmlSettings);
    advLayout->addWidget(m_pdfSettings);
    advLayout->addWidget(m_customSettings);

    m_settingBox->setLayout(advLayout);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(m_basicBox);
    mainLayout->addWidget(m_settingBox);
    mainLayout->addWidget(m_consoleEdit);
    mainLayout->addLayout(btnLayout);

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

    // Enable table of contents.
    m_tableOfContentsCB = new QCheckBox(tr("Enable Table Of Contents"));
    m_tableOfContentsCB->setToolTip(tr("Add a table of contents to the document"));

    // Use wkhtmltopdf.
    m_wkhtmltopdfCB = new QCheckBox(tr("Use wkhtmltopdf"));
    m_wkhtmltopdfCB->setToolTip(tr("Use wkhtmltopdf tool to generate PDF (wkhtmltopdf needed to be installed)"));
    connect(m_wkhtmltopdfCB, &QCheckBox::stateChanged,
            this, [this](int p_state) {
                bool checked = p_state == Qt::Checked;
                m_wkPathEdit->setEnabled(checked);
                m_wkPathBrowseBtn->setEnabled(checked);
                m_wkBackgroundCB->setEnabled(checked);
                m_wkPageNumberCB->setEnabled(checked);
                m_wkExtraArgsEdit->setEnabled(checked);
            });

    QPushButton *wkBtn = new QPushButton(tr("Download wkhtmltopdf"));
    connect(wkBtn, &QPushButton::clicked,
            this, [this]() {
                QString url("https://wkhtmltopdf.org/downloads.html");
                QDesktopServices::openUrl(QUrl(url));
            });

    // wkhtmltopdf Path.
    m_wkPathEdit = new VLineEdit();
    m_wkPathEdit->setToolTip(tr("Tell VNote where to find wkhtmltopdf tool"));
    m_wkPathEdit->setEnabled(false);

    m_wkPathBrowseBtn = new QPushButton(tr("&Browse"));
    connect(m_wkPathBrowseBtn, &QPushButton::clicked,
            this, &VExportDialog::handleWkPathBrowseBtnClicked);
    m_wkPathBrowseBtn->setEnabled(false);

    m_wkTitleEdit = new VLineEdit();
    m_wkTitleEdit->setPlaceholderText(tr("Empty to use the name of the first source file"));
    m_wkTitleEdit->setToolTip(tr("Title of the generated PDF file"));
    m_wkTitleEdit->setEnabled(false);

    m_wkTargetFileNameEdit = new VLineEdit();
    m_wkTargetFileNameEdit->setPlaceholderText(tr("Empty to use the name of the first source file"));
    m_wkTargetFileNameEdit->setToolTip(tr("Name of the generated PDF file"));
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp),
                                                 m_wkTargetFileNameEdit);
    m_wkTargetFileNameEdit->setValidator(validator);
    m_wkTargetFileNameEdit->setEnabled(false);

    // wkhtmltopdf enable background.
    m_wkBackgroundCB = new QCheckBox(tr("Enable background"));
    m_wkBackgroundCB->setToolTip(tr("Enable background when printing"));
    m_wkBackgroundCB->setEnabled(false);

    // wkhtmltopdf page number.
    m_wkPageNumberCB = VUtils::getComboBox();
    m_wkPageNumberCB->setToolTip(tr("Append page number as footer"));
    m_wkPageNumberCB->setEnabled(false);

    // wkhtmltopdf extra argumnets.
    m_wkExtraArgsEdit = new VLineEdit();
    m_wkExtraArgsEdit->setToolTip(tr("Additional global options passed to wkhtmltopdf"));
    m_wkExtraArgsEdit->setPlaceholderText(tr("Use \" to enclose options containing spaces"));
    m_wkExtraArgsEdit->setEnabled(false);

    QGridLayout *advLayout = new QGridLayout();
    advLayout->addWidget(new QLabel(tr("Page layout:")), 0, 0);
    advLayout->addWidget(m_layoutLabel, 0, 1);
    advLayout->addWidget(layoutBtn, 0, 2);
    advLayout->addWidget(m_tableOfContentsCB, 0, 4, 1, 2);

    advLayout->addWidget(m_wkhtmltopdfCB, 1, 1, 1, 2);
    advLayout->addWidget(wkBtn, 1, 4, 1, 2);

    advLayout->addWidget(new QLabel(tr("wkhtmltopdf path:")), 2, 0);
    advLayout->addWidget(m_wkPathEdit, 2, 1, 1, 4);
    advLayout->addWidget(m_wkPathBrowseBtn, 2, 5);

    advLayout->addWidget(new QLabel(tr("Title:")), 3, 0);
    advLayout->addWidget(m_wkTitleEdit, 3, 1, 1, 2);

    advLayout->addWidget(new QLabel(tr("Output file name:")), 3, 3);
    advLayout->addWidget(m_wkTargetFileNameEdit, 3, 4, 1, 2);

    advLayout->addWidget(new QLabel(tr("Page number:")), 4, 0);
    advLayout->addWidget(m_wkPageNumberCB, 4, 1, 1, 2);
    advLayout->addWidget(m_wkBackgroundCB, 4, 4, 1, 2);

    advLayout->addWidget(new QLabel(tr("Additional options:")), 5, 0);
    advLayout->addWidget(m_wkExtraArgsEdit, 5, 1, 1, 5);

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

    m_subfolderCB->setChecked(s_opt.m_processSubfolders);

    // Export format.
    m_formatCB->addItem(tr("Markdown"), (int)ExportFormat::Markdown);
    m_formatCB->addItem(tr("HTML"), (int)ExportFormat::HTML);
    m_formatCB->addItem(tr("PDF"), (int)ExportFormat::PDF);
    m_formatCB->addItem(tr("PDF (All In One)"), (int)ExportFormat::OnePDF);
    m_formatCB->addItem(tr("Custom"), (int)ExportFormat::Custom);
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

    m_tableOfContentsCB->setChecked(s_opt.m_pdfOpt.m_enableTableOfContents);

    m_wkhtmltopdfCB->setChecked(s_opt.m_pdfOpt.m_wkhtmltopdf);

    // wkhtmltopdf path.
    m_wkPathEdit->setText(g_config->getWkhtmltopdfPath());

    m_wkBackgroundCB->setChecked(s_opt.m_pdfOpt.m_wkEnableBackground);

    // wkhtmltopdf page number.
    m_wkPageNumberCB->addItem(tr("None"), (int)ExportPageNumber::None);
    m_wkPageNumberCB->addItem(tr("Left"), (int)ExportPageNumber::Left);
    m_wkPageNumberCB->addItem(tr("Center"), (int)ExportPageNumber::Center);
    m_wkPageNumberCB->addItem(tr("Right"), (int)ExportPageNumber::Right);
    m_wkPageNumberCB->setCurrentIndex(m_wkPageNumberCB->findData((int)s_opt.m_pdfOpt.m_wkPageNumber));

    m_wkExtraArgsEdit->setText(g_config->getWkhtmltopdfArgs());

    // Custom export.
    // Read from config every time.
    ExportCustomOption customOpt(g_config->getCustomExport());

    m_customSrcFormatCB->addItem(tr("Markdown"), (int)ExportCustomOption::SourceFormat::Markdown);
    m_customSrcFormatCB->addItem(tr("HTML"), (int)ExportCustomOption::SourceFormat::HTML);
    m_customSrcFormatCB->setCurrentIndex(m_customSrcFormatCB->findData((int)customOpt.m_srcFormat));

    m_customSuffixEdit->setText(customOpt.m_outputSuffix);

    m_customCmdEdit->setPlainText(customOpt.m_cmd);

    m_customAllInOneCB->setChecked(s_opt.m_customOpt.m_allInOne);

    m_customFolderSepEdit->setText(s_opt.m_customOpt.m_folderSep);
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
    m_proBar->show();
    m_askedToStop = false;
    m_exporter->setAskedToStop(false);
    m_inExport = true;

    QString outputFolder = QDir::cleanPath(QDir(getOutputDirectory()).absolutePath());

    QString renderStyle = m_renderStyleCB->currentData().toString();
    QString cssUrl = g_config->getCssStyleUrl(renderStyle);

    s_opt = ExportOption(currentSource(),
                         currentFormat(),
                         (MarkdownConverterType)m_rendererCB->currentData().toInt(),
                         m_renderBgCB->currentData().toString(),
                         renderStyle,
                         m_renderCodeBlockStyleCB->currentData().toString(),
                         m_subfolderCB->isChecked(),
                         ExportPDFOption(&m_pageLayout,
                                         m_wkhtmltopdfCB->isChecked(),
                                         QDir::toNativeSeparators(m_wkPathEdit->text()),
                                         m_wkBackgroundCB->isChecked(),
                                         m_tableOfContentsCB->isChecked(),
                                         m_wkTitleEdit->text(),
                                         m_wkTargetFileNameEdit->text(),
                                         (ExportPageNumber)
                                         m_wkPageNumberCB->currentData().toInt(),
                                         m_wkExtraArgsEdit->text()),
                         ExportHTMLOption(m_embedStyleCB->isChecked(),
                                          m_completeHTMLCB->isChecked(),
                                          m_mimeHTMLCB->isChecked()),
                         ExportCustomOption((ExportCustomOption::SourceFormat)
                                            m_customSrcFormatCB->currentData().toInt(),
                                            m_customSuffixEdit->text(),
                                            m_customCmdEdit->toPlainText(),
                                            cssUrl,
                                            m_customAllInOneCB->isChecked(),
                                            m_customFolderSepEdit->text(),
                                            m_customTargetFileNameEdit->text()));

    m_consoleEdit->clear();
    appendLogLine(tr("Export to %1.").arg(outputFolder));

    if (s_opt.m_format == ExportFormat::PDF
        || s_opt.m_format == ExportFormat::OnePDF
        || s_opt.m_format == ExportFormat::HTML) {
        if (s_opt.m_format != ExportFormat::OnePDF) {
            s_opt.m_pdfOpt.m_wkTitle.clear();
        }

        if ((s_opt.m_format == ExportFormat::PDF
             && s_opt.m_pdfOpt.m_wkhtmltopdf)
            || s_opt.m_format == ExportFormat::OnePDF) {
            g_config->setWkhtmltopdfPath(s_opt.m_pdfOpt.m_wkPath);
            g_config->setWkhtmltopdfArgs(s_opt.m_pdfOpt.m_wkExtraArgs);

            if (!checkWkhtmltopdfExecutable(s_opt.m_pdfOpt.m_wkPath)) {
                m_inExport = false;
                m_exportBtn->setEnabled(true);
                m_proBar->hide();
                return;
            }
        }

        m_exporter->prepareExport(s_opt);
    } else if (s_opt.m_format == ExportFormat::Custom) {
        const ExportCustomOption &opt = s_opt.m_customOpt;
        if (opt.m_srcFormat == ExportCustomOption::HTML) {
            m_exporter->prepareExport(s_opt);
        }

        // Save it to config.
        g_config->setCustomExport(opt.toConfig());

        if (opt.m_outputSuffix.isEmpty()
            || opt.m_cmd.isEmpty()
            || (opt.m_allInOne && opt.m_folderSep.isEmpty())) {
            appendLogLine(tr("Invalid configurations for custom export."));
            m_inExport = false;
            m_exportBtn->setEnabled(true);
            m_proBar->hide();
            return;
        }
    }

    int ret = 0;
    QString msg;

    if (s_opt.m_format == ExportFormat::OnePDF) {
        QList<QString> files;
        // Output HTMLs to a tmp folder.
        QTemporaryDir tmpDir;
        if (!tmpDir.isValid()) {
            goto exit;
        }

        ret = outputAsHTML(tmpDir.path(), &msg, &files);
        if (m_askedToStop) {
            ret = 0;
            goto exit;
        }

        Q_ASSERT(ret == files.size());
        if (!files.isEmpty()) {
            ret = doExportPDFAllInOne(files, s_opt, outputFolder, &msg);
        }
    } else if (s_opt.m_format == ExportFormat::Custom
               && s_opt.m_customOpt.m_allInOne) {
        QList<QString> files;
        QTemporaryDir tmpDir;
        if (!tmpDir.isValid()) {
            goto exit;
        }

        if (s_opt.m_customOpt.m_srcFormat == ExportCustomOption::HTML) {
            // Output HTMLs to a tmp folder.
            ret = outputAsHTML(tmpDir.path(), &msg, &files);
            if (m_askedToStop) {
                ret = 0;
                goto exit;
            }

            Q_ASSERT(ret == files.size());
        } else {
            // Collect all markdown files.
            files = collectFiles(&msg);
        }

        if (!files.isEmpty()) {
            ret = doExportCustomAllInOne(files, s_opt, outputFolder, &msg);
        }
    } else {
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
    }

exit:

    if (m_askedToStop) {
        appendLogLine(tr("User cancelled the export. Aborted!"));
        m_askedToStop = false;
        m_exporter->setAskedToStop(false);
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
    m_proBar->hide();
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
    m_consoleEdit->appendPlainText(">>> " + p_text);
    m_consoleEdit->ensureCursorVisible();
    QCoreApplication::sendPostedEvents();
}

int VExportDialog::doExport(VFile *p_file,
                            const ExportOption &p_opt,
                            const QString &p_outputFolder,
                            QString *p_errMsg,
                            QList<QString> *p_outputFiles)
{
    Q_ASSERT(p_file);

    appendLogLine(tr("Exporting note %1.").arg(p_file->fetchPath()));

    int ret = 0;
    switch (p_opt.m_format) {
    case ExportFormat::Markdown:
        ret = doExportMarkdown(p_file, p_opt, p_outputFolder, p_errMsg, p_outputFiles);
        break;

    case ExportFormat::PDF:
        V_FALLTHROUGH;
    case ExportFormat::OnePDF:
        ret = doExportPDF(p_file, p_opt, p_outputFolder, p_errMsg, p_outputFiles);
        break;

    case ExportFormat::HTML:
        ret = doExportHTML(p_file, p_opt, p_outputFolder, p_errMsg, p_outputFiles);
        break;

    case ExportFormat::Custom:
        ret = doExportCustom(p_file, p_opt, p_outputFolder, p_errMsg, p_outputFiles);
        break;

    default:
        break;
    }

    return ret;
}

int VExportDialog::doExport(VDirectory *p_directory,
                            const ExportOption &p_opt,
                            const QString &p_outputFolder,
                            QString *p_errMsg,
                            QList<QString> *p_outputFiles)
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

        ret += doExport(file, p_opt, outputPath, p_errMsg, p_outputFiles);
    }

    // Export subfolders.
    if (p_opt.m_processSubfolders) {
        for (auto const & dir : p_directory->getSubDirs()) {
            if (!checkUserAction()) {
                goto exit;
            }

            ret += doExport(dir, p_opt, outputPath, p_errMsg, p_outputFiles);
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
                            QString *p_errMsg,
                            QList<QString> *p_outputFiles)
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

        ret += doExport(dir, p_opt, outputPath, p_errMsg, p_outputFiles);
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
                            QString *p_errMsg,
                            QList<QString> *p_outputFiles)
{
    Q_UNUSED(p_cart);
    Q_ASSERT(p_cart);
    int ret = 0;

    QVector<QString> files = m_cart->getFiles();
    for (auto const & it : files) {
        VFile *file = g_vnote->getFile(it);
        if (!file) {
            LOGERR(tr("Fail to open file %1.").arg(it));
            continue;
        }

        ret += doExport(file, p_opt, p_outputFolder, p_errMsg, p_outputFiles);
    }

    return ret;
}

int VExportDialog::doExportMarkdown(VFile *p_file,
                                    const ExportOption &p_opt,
                                    const QString &p_outputFolder,
                                    QString *p_errMsg,
                                    QList<QString> *p_outputFiles)
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
        if (p_outputFiles) {
            p_outputFiles->append(destPath);
        }

        appendLogLine(tr("Note %1 exported to %2.").arg(srcFilePath).arg(outputPath));
    }

    return ret;
}

int VExportDialog::doExportPDF(VFile *p_file,
                               const ExportOption &p_opt,
                               const QString &p_outputFolder,
                               QString *p_errMsg,
                               QList<QString> *p_outputFiles)
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
        if (p_outputFiles) {
            p_outputFiles->append(outputPath);
        }

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
                                QString *p_errMsg,
                                QList<QString> *p_outputFiles)
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
        if (p_outputFiles) {
            p_outputFiles->append(outputPath);
        }

        appendLogLine(tr("Note %1 exported to %2.").arg(srcFilePath).arg(outputPath));
        return 1;
    } else {
        appendLogLine(tr("Fail to export note %1.").arg(srcFilePath));
        return 0;
    }
}

int VExportDialog::doExportCustom(VFile *p_file,
                                  const ExportOption &p_opt,
                                  const QString &p_outputFolder,
                                  QString *p_errMsg,
                                  QList<QString> *p_outputFiles)
{
    Q_UNUSED(p_opt);

    QString srcFilePath(p_file->fetchPath());

    if (p_file->getDocType() != DocType::Markdown) {
        LOGERR(tr("Skip exporting non-Markdown file %1.").arg(srcFilePath));
        return 0;
    }

    if (!VUtils::makePath(p_outputFolder)) {
        LOGERR(tr("Fail to create directory %1.").arg(p_outputFolder));
        return 0;
    }

    // Get output file.
    QString suffix = "." + p_opt.m_customOpt.m_outputSuffix;
    QString name = VUtils::getFileNameWithSequence(p_outputFolder,
                                                   QFileInfo(p_file->getName()).completeBaseName() + suffix);
    QString outputPath = QDir(p_outputFolder).filePath(name);

    if (m_exporter->exportCustom(p_file, p_opt, outputPath, p_errMsg)) {
        if (p_outputFiles) {
            p_outputFiles->append(outputPath);
        }

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
    bool pdfTitleNameEnabled = false;
    bool customEnabled = false;

    if (p_index >= 0) {
        switch (currentFormat()) {
        case ExportFormat::PDF:
            pdfEnabled = true;
            m_wkhtmltopdfCB->setEnabled(true);
            break;

        case ExportFormat::HTML:
            htmlEnabled = true;
            break;

        case ExportFormat::OnePDF:
            pdfEnabled = true;
            pdfTitleNameEnabled = true;
            m_wkhtmltopdfCB->setChecked(true);
            m_wkhtmltopdfCB->setEnabled(false);
            break;

        case ExportFormat::Custom:
            customEnabled = true;
            break;

        default:
            break;
        }
    }

    m_pdfSettings->setVisible(pdfEnabled);
    m_htmlSettings->setVisible(htmlEnabled);
    m_customSettings->setVisible(customEnabled);

    m_wkTitleEdit->setEnabled(pdfTitleNameEnabled);
    m_wkTargetFileNameEdit->setEnabled(pdfTitleNameEnabled);
}

void VExportDialog::handleCurrentSrcChanged(int p_index)
{
    bool subfolderEnabled = false;

    if (p_index >= 0) {
        switch (currentSource()) {
        case ExportSource::CurrentFolder:
            subfolderEnabled = true;
            break;

        default:
            break;
        }
    }

    m_subfolderCB->setVisible(subfolderEnabled);
}

int VExportDialog::doExportPDFAllInOne(const QList<QString> &p_files,
                                       const ExportOption &p_opt,
                                       const QString &p_outputFolder,
                                       QString *p_errMsg)
{
    if (p_files.isEmpty()) {
        return 0;
    }

    if (!VUtils::makePath(p_outputFolder)) {
        LOGERR(tr("Fail to create directory %1.").arg(p_outputFolder));
        return 0;
    }

    // Get output file.
    const QString suffix = ".pdf";
    QString name = p_opt.m_pdfOpt.m_wkTargetFileName;
    if (name.isEmpty()) {
        name = VUtils::getFileNameWithSequence(p_outputFolder,
                                               QFileInfo(p_files.first()).completeBaseName() + suffix);
    } else if (!name.endsWith(suffix)) {
        name += suffix;
    }

    QString outputPath = QDir(p_outputFolder).filePath(name);

    int ret = m_exporter->exportPDFInOne(p_files, p_opt, outputPath, p_errMsg);
    if (ret > 0) {
        appendLogLine(tr("%1 notes exported to %2.").arg(ret).arg(outputPath));
    } else {
        appendLogLine(tr("Fail to export %1 notes in one PDF.").arg(p_files.size()));
    }

    return ret;
}

int VExportDialog::doExportCustomAllInOne(const QList<QString> &p_files,
                                          const ExportOption &p_opt,
                                          const QString &p_outputFolder,
                                          QString *p_errMsg)
{
    if (p_files.isEmpty()) {
        return 0;
    }

    if (!VUtils::makePath(p_outputFolder)) {
        LOGERR(tr("Fail to create directory %1.").arg(p_outputFolder));
        return 0;
    }

    // Get output file.
    QString suffix = "." + p_opt.m_customOpt.m_outputSuffix;
    QString name = p_opt.m_customOpt.m_targetFileName;
    if (name.isEmpty()) {
        name = VUtils::getFileNameWithSequence(p_outputFolder,
                                               QFileInfo(p_files.first()).completeBaseName() + suffix);
    } else if (!name.endsWith(suffix)) {
        name += suffix;
    }

    QString outputPath = QDir(p_outputFolder).filePath(name);

    int ret = m_exporter->exportCustomInOne(p_files, p_opt, outputPath, p_errMsg);
    if (ret > 0) {
        appendLogLine(tr("%1 notes exported to %2.").arg(ret).arg(outputPath));
    } else {
        appendLogLine(tr("Fail to export %1 notes in one.").arg(p_files.size()));
    }

    return ret;
}

QWidget *VExportDialog::setupCustomAdvancedSettings()
{
    // Source format.
    m_customSrcFormatCB = VUtils::getComboBox(this);
    m_customSrcFormatCB->setToolTip(tr("Choose format of the input"));

    // Output suffix.
    m_customSuffixEdit = new VLineEdit(this);
    m_customSuffixEdit->setPlaceholderText(tr("Without the preceding dot"));
    m_customSuffixEdit->setToolTip(tr("Suffix of the output file without the preceding dot"));
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp),
                                                 m_customSuffixEdit);
    m_customSuffixEdit->setValidator(validator);

    QLabel *tipsLabel = new QLabel(tr("<span><span style=\"font-weight:bold;\">%0</span> for the input file; "
                                      "<span style=\"font-weight:bold;\">%1</span> for the output file; "
                                      "<span style=\"font-weight:bold;\">%2</span> for the rendering CSS style file; "
                                      "<span style=\"font-weight:bold;\">%3</span> for the input file directory.</span>"),
                                   this);
    tipsLabel->setWordWrap(true);

    // Enable All In One.
    m_customAllInOneCB = new QCheckBox(tr("Enable All In One"), this);
    m_customAllInOneCB->setToolTip(tr("Pass a list of input files to the custom command"));
    connect(m_customAllInOneCB, &QCheckBox::stateChanged,
            this, [this](int p_state) {
                bool checked = p_state == Qt::Checked;
                m_customFolderSepEdit->setEnabled(checked);
                m_customTargetFileNameEdit->setEnabled(checked);
            });

    // Input directory separator.
    m_customFolderSepEdit = new VLineEdit(this);
    m_customFolderSepEdit->setPlaceholderText(tr("Separator to concatenate input files directories"));
    m_customFolderSepEdit->setToolTip(tr("Separator to concatenate input files directories"));
    m_customFolderSepEdit->setEnabled(false);

    // Target file name for all in one.
    m_customTargetFileNameEdit = new VLineEdit(this);
    m_customTargetFileNameEdit->setPlaceholderText(tr("Empty to use the name of the first source file"));
    m_customTargetFileNameEdit->setToolTip(tr("Name of the generated All-In-One file"));
    validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp),
                                                 m_customTargetFileNameEdit);
    m_customTargetFileNameEdit->setValidator(validator);
    m_customTargetFileNameEdit->setEnabled(false);

    // Cmd edit.
    m_customCmdEdit = new QPlainTextEdit(this);
    m_customCmdEdit->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    QString cmdExamp("pandoc --resource-path=.:\"%3\" --css=\"%2\" -s -o \"%1\" \"%0\"");
    m_customCmdEdit->setPlaceholderText(cmdExamp);
    m_customCmdEdit->setToolTip(tr("Custom command to be executed"));
    m_customCmdEdit->setProperty("LineEdit", true);

    QGridLayout *advLayout = new QGridLayout();
    advLayout->addWidget(new QLabel(tr("Source format:")), 0, 0);
    advLayout->addWidget(m_customSrcFormatCB, 0, 1, 1, 2);

    advLayout->addWidget(new QLabel(tr("Output suffix:")), 0, 3);
    advLayout->addWidget(m_customSuffixEdit, 0, 4, 1, 2);

    advLayout->addWidget(m_customAllInOneCB, 1, 1, 1, 2);

    advLayout->addWidget(new QLabel(tr("Output file name:")), 2, 0);
    advLayout->addWidget(m_customTargetFileNameEdit, 2, 1, 1, 2);

    advLayout->addWidget(new QLabel(tr("Input directories separator:")), 2, 3);
    advLayout->addWidget(m_customFolderSepEdit, 2, 4, 1, 2);

    advLayout->addWidget(tipsLabel, 3, 0, 1, 6);

    advLayout->addWidget(m_customCmdEdit, 4, 0, 1, 6);

    advLayout->setContentsMargins(0, 0, 0, 0);

    QWidget *wid = new QWidget();
    wid->setLayout(advLayout);

    m_customCmdEdit->setMaximumHeight(m_customSrcFormatCB->height() * 3);

    return wid;
}

int VExportDialog::outputAsHTML(const QString &p_outputFolder,
                                QString *p_errMsg,
                                QList<QString> *p_outputFiles)
{
    int ret = 0;
    ExportFormat fmt = s_opt.m_format;
    s_opt.m_format = ExportFormat::HTML;
    switch (s_opt.m_source) {
    case ExportSource::CurrentNote:
        ret = doExport(m_file, s_opt, p_outputFolder, p_errMsg, p_outputFiles);
        break;

    case ExportSource::CurrentFolder:
        ret = doExport(m_directory, s_opt, p_outputFolder, p_errMsg, p_outputFiles);
        break;

    case ExportSource::CurrentNotebook:
        ret = doExport(m_notebook, s_opt, p_outputFolder, p_errMsg, p_outputFiles);
        break;

    case ExportSource::Cart:
        ret = doExport(m_cart, s_opt, p_outputFolder, p_errMsg, p_outputFiles);
        break;

    default:
        break;
    }

    s_opt.m_format = fmt;

    return ret;
}

QList<QString> VExportDialog::collectFiles(QString *p_errMsg)
{
    Q_UNUSED(p_errMsg);

    QList<QString> files;
    switch (s_opt.m_source) {
    case ExportSource::CurrentNote:
        files.append(m_file->fetchPath());
        break;

    case ExportSource::CurrentFolder:
        files = m_directory->collectFiles();
        break;

    case ExportSource::CurrentNotebook:
        files = m_notebook->collectFiles();
        break;

    case ExportSource::Cart:
        files = m_cart->getFiles().toList();
        break;

    default:
        break;
    }

    return files;
}
