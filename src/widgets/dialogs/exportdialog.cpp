#include "exportdialog.h"

#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QProgressBar>
#include <QFileInfo>
#include <QFileDialog>
#include <QUrl>
#include <QPlainTextEdit>
#include <QCoreApplication>
#include <QPrinter>
#include <QPageSetupDialog>
#include <QPageLayout>

#include <notebook/notebook.h>
#include <notebook/node.h>
#include <buffer/buffer.h>
#include <widgets/widgetsfactory.h>
#include <core/thememgr.h>
#include <core/configmgr.h>
#include <core/sessionconfig.h>
#include <core/vnotex.h>
#include <utils/widgetutils.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <utils/clipboardutils.h>
#include <export/exporter.h>

using namespace vnotex;

ExportDialog::ExportDialog(Notebook *p_notebook,
                           Node *p_folder,
                           Buffer *p_buffer,
                           QWidget *p_parent)
    : ScrollDialog(p_parent),
      m_notebook(p_notebook),
      m_folder(p_folder),
      m_buffer(p_buffer)
{
    setupUI();

    initOptions();

    restoreFields(m_option);

    connect(this, &QDialog::finished,
            this, [this]() {
                saveFields(m_option);
                ConfigMgr::getInst().getSessionConfig().setExportOption(m_option);
            });
}

void ExportDialog::setupUI()
{
    auto widget = new QWidget(this);
    setCentralWidget(widget);

    auto mainLayout = new QVBoxLayout(widget);

    auto sourceBox = setupSourceGroup(widget);
    mainLayout->addWidget(sourceBox);

    auto targetBox = setupTargetGroup(widget);
    mainLayout->addWidget(targetBox);

    m_advancedGroupBox = setupAdvancedGroup(widget);
    mainLayout->addWidget(m_advancedGroupBox);

    m_progressBar = new QProgressBar(widget);
    m_progressBar->setRange(0, 0);
    m_progressBar->hide();
    addBottomWidget(m_progressBar);

    setupButtonBox();

    setWindowTitle(tr("Export"));
}

QGroupBox *ExportDialog::setupSourceGroup(QWidget *p_parent)
{
    auto box = new QGroupBox(tr("Source"), p_parent);
    auto layout = WidgetsFactory::createFormLayout(box);

    {
        m_sourceComboBox = WidgetsFactory::createComboBox(box);
        if (m_buffer) {
            m_sourceComboBox->addItem(tr("Current Buffer (%1)").arg(m_buffer->getName()),
                                      static_cast<int>(ExportSource::CurrentBuffer));
        }
        if (m_folder && m_folder->isContainer()) {
            m_sourceComboBox->addItem(tr("Current Folder (%1)").arg(m_folder->getName()),
                                      static_cast<int>(ExportSource::CurrentFolder));
        }
        if (m_notebook) {
            m_sourceComboBox->addItem(tr("Current Notebook (%1)").arg(m_notebook->getName()),
                                      static_cast<int>(ExportSource::CurrentNotebook));
        }
        layout->addRow(tr("Source:"), m_sourceComboBox);
    }

    {
        // TODO: Source format filtering.
    }

    return box;
}

QString ExportDialog::getDefaultOutputDir() const
{
    return PathUtils::concatenateFilePath(ConfigMgr::getDocumentOrHomePath(), tr("vnote_exports"));
}

QGroupBox *ExportDialog::setupTargetGroup(QWidget *p_parent)
{
    auto box = new QGroupBox(tr("Target"), p_parent);
    auto layout = WidgetsFactory::createFormLayout(box);

    {
        m_targetFormatComboBox = WidgetsFactory::createComboBox(box);
        m_targetFormatComboBox->addItem(tr("Markdown"),
                                        static_cast<int>(ExportFormat::Markdown));
        m_targetFormatComboBox->addItem(tr("HTML"),
                                        static_cast<int>(ExportFormat::HTML));
        m_targetFormatComboBox->addItem(tr("PDF"),
                                        static_cast<int>(ExportFormat::PDF));
        m_targetFormatComboBox->addItem(tr("Custom"),
                                        static_cast<int>(ExportFormat::Custom));
        connect(m_targetFormatComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this]() {
                    AdvancedSettings settings = AdvancedSettings::Max;
                    int format = m_targetFormatComboBox->currentData().toInt();
                    switch (format) {
                    case static_cast<int>(ExportFormat::HTML):
                        settings = AdvancedSettings::HTML;
                        break;

                    case static_cast<int>(ExportFormat::PDF):
                        settings = AdvancedSettings::PDF;
                        break;

                    default:
                        break;
                    }
                    showAdvancedSettings(settings);
                });
        layout->addRow(tr("Format:"), m_targetFormatComboBox);
    }

    {
        m_transparentBgCheckBox = WidgetsFactory::createCheckBox(tr("Use transparent background"), box);
        layout->addRow(m_transparentBgCheckBox);
    }

    {
        const auto webStyles = VNoteX::getInst().getThemeMgr().getWebStyles();

        m_renderingStyleComboBox = WidgetsFactory::createComboBox(box);
        layout->addRow(tr("Rendering style:"), m_renderingStyleComboBox);
        for (const auto &pa : webStyles) {
            m_renderingStyleComboBox->addItem(pa.first, pa.second);
        }

        m_syntaxHighlightStyleComboBox = WidgetsFactory::createComboBox(box);
        layout->addRow(tr("Syntax highlighting style:"), m_syntaxHighlightStyleComboBox);
        for (const auto &pa : webStyles) {
            m_syntaxHighlightStyleComboBox->addItem(pa.first, pa.second);
        }
    }

    {
        auto outputLayout = new QHBoxLayout();

        m_outputDirLineEdit = WidgetsFactory::createLineEdit(box);
        outputLayout->addWidget(m_outputDirLineEdit);

        auto browseBtn = new QPushButton(tr("Browse"), box);
        outputLayout->addWidget(browseBtn);
        connect(browseBtn, &QPushButton::clicked,
                this, [this]() {
                    QString initPath = getOutputDir();
                    if (!QFileInfo::exists(initPath)) {
                        initPath = getDefaultOutputDir();
                    }

                    QString dirPath = QFileDialog::getExistingDirectory(this,
                                                                        tr("Select Export Output Directory"),
                                                                        initPath,
                                                                        QFileDialog::ShowDirsOnly
                                                                        | QFileDialog::DontResolveSymlinks);

                    if (!dirPath.isEmpty()) {
                        m_outputDirLineEdit->setText(dirPath);
                    }
                });

        layout->addRow(tr("Output directory:"), outputLayout);
    }

    return box;
}

QGroupBox *ExportDialog::setupAdvancedGroup(QWidget *p_parent)
{
    auto box = new QGroupBox(tr("Advanced"), p_parent);
    auto layout = new QVBoxLayout(box);

    m_advancedSettings.resize(AdvancedSettings::Max);

    m_advancedSettings[AdvancedSettings::General] = setupGeneralAdvancedSettings(box);
    layout->addWidget(m_advancedSettings[AdvancedSettings::General]);

    return box;
}

QWidget *ExportDialog::setupGeneralAdvancedSettings(QWidget *p_parent)
{
    QWidget *widget = new QWidget(p_parent);
    auto layout = WidgetsFactory::createFormLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);

    {
        m_recursiveCheckBox = WidgetsFactory::createCheckBox(tr("Process sub-folders"), widget);
        layout->addRow(m_recursiveCheckBox);
    }

    {
        m_exportAttachmentsCheckBox = WidgetsFactory::createCheckBox(tr("Export attachments"), widget);
        layout->addRow(m_exportAttachmentsCheckBox);
    }

    return widget;
}

void ExportDialog::setupButtonBox()
{
    setDialogButtonBox(QDialogButtonBox::Close);

    auto box = getDialogButtonBox();

    m_exportBtn = box->addButton(tr("Export"), QDialogButtonBox::ActionRole);
    connect(m_exportBtn, &QPushButton::clicked,
            this, &ExportDialog::startExport);

    m_openDirBtn = box->addButton(tr("Open Directory"), QDialogButtonBox::ActionRole);
    connect(m_openDirBtn, &QPushButton::clicked,
            this, [this]() {
                auto dir = getOutputDir();
                if (!dir.isEmpty()) {
                    WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(dir));
                }
            });

    m_copyContentBtn = box->addButton(tr("Copy Content"), QDialogButtonBox::ActionRole);
    m_copyContentBtn->setToolTip(tr("Copy exported file content"));
    m_copyContentBtn->setEnabled(false);
    connect(m_copyContentBtn, &QPushButton::clicked,
            this, [this]() {
                if (m_exportedFile.isEmpty()) {
                    return;
                }

                const auto content = FileUtils::readTextFile(m_exportedFile);
                if (!content.isEmpty()) {
                    ClipboardUtils::setTextToClipboard(content);
                }
            });
}

QString ExportDialog::getOutputDir() const
{
    return m_outputDirLineEdit->text();
}

void ExportDialog::initOptions()
{
    // Read it from config.
    m_option = ConfigMgr::getInst().getSessionConfig().getExportOption();

    const auto &theme = VNoteX::getInst().getThemeMgr().getCurrentTheme();
    m_option.m_renderingStyleFile = theme.getFile(Theme::File::WebStyleSheet);
    m_option.m_syntaxHighlightStyleFile = theme.getFile(Theme::File::HighlightStyleSheet);

    if (m_option.m_outputDir.isEmpty()) {
        m_option.m_outputDir = getDefaultOutputDir();
    }
}

void ExportDialog::restoreFields(const ExportOption &p_option)
{
    {
        int idx = m_sourceComboBox->findData(static_cast<int>(p_option.m_source));
        if (idx != -1) {
            m_sourceComboBox->setCurrentIndex(idx);
        }
    }

    {
        int idx = m_targetFormatComboBox->findData(static_cast<int>(p_option.m_targetFormat));
        if (idx != -1) {
            m_targetFormatComboBox->setCurrentIndex(idx);
        }
    }

    m_transparentBgCheckBox->setChecked(p_option.m_useTransparentBg);

    {
        int idx = m_renderingStyleComboBox->findData(p_option.m_renderingStyleFile);
        if (idx != -1) {
            m_renderingStyleComboBox->setCurrentIndex(idx);
        }
    }

    {
        int idx = m_syntaxHighlightStyleComboBox->findData(p_option.m_syntaxHighlightStyleFile);
        if (idx != -1) {
            m_syntaxHighlightStyleComboBox->setCurrentIndex(idx);
        }
    }

    m_outputDirLineEdit->setText(p_option.m_outputDir);

    m_recursiveCheckBox->setChecked(p_option.m_recursive);

    m_exportAttachmentsCheckBox->setChecked(p_option.m_exportAttachments);
}

void ExportDialog::saveFields(ExportOption &p_option)
{
    p_option.m_source = static_cast<ExportSource>(m_sourceComboBox->currentData().toInt());
    p_option.m_targetFormat = static_cast<ExportFormat>(m_targetFormatComboBox->currentData().toInt());
    p_option.m_useTransparentBg = m_transparentBgCheckBox->isChecked();
    p_option.m_renderingStyleFile = m_renderingStyleComboBox->currentData().toString();
    p_option.m_syntaxHighlightStyleFile = m_syntaxHighlightStyleComboBox->currentData().toString();
    p_option.m_outputDir = getOutputDir();
    p_option.m_recursive = m_recursiveCheckBox->isChecked();
    p_option.m_exportAttachments = m_exportAttachmentsCheckBox->isChecked();

    if (m_advancedSettings[AdvancedSettings::HTML]) {
        saveFields(p_option.m_htmlOption);
    }

    if (m_advancedSettings[AdvancedSettings::PDF]) {
        saveFields(p_option.m_pdfOption);
    }
}

void ExportDialog::startExport()
{
    if (m_exportOngoing) {
        return;
    }

    // On start.
    {
        clearInformationText();
        m_exportedFile.clear();
        m_exportOngoing = true;
        updateUIOnExport();
    }

    saveFields(m_option);

    int ret = doExport(m_option);
    appendLog(tr("%n file(s) exported", "", ret));

    // On end.
    {
        m_exportOngoing = false;
        updateUIOnExport();
    }
}

void ExportDialog::rejectedButtonClicked()
{
    if (m_exportOngoing) {
        // Just cancel the export.
        appendLog(tr("Cancelling the export."));
        m_exporter->stop();
    } else {
        Dialog::rejectedButtonClicked();
    }
}

void ExportDialog::appendLog(const QString &p_log)
{
    appendInformationText(">>> " + p_log);
    QCoreApplication::sendPostedEvents();
}

void ExportDialog::updateUIOnExport()
{
    m_exportBtn->setEnabled(!m_exportOngoing);
    if (m_exportOngoing) {
        m_progressBar->setMaximum(0);
        m_progressBar->show();
    } else {
        m_progressBar->hide();
    }
    m_copyContentBtn->setEnabled(!m_exportedFile.isEmpty());
}

int ExportDialog::doExport(ExportOption p_option)
{
    if (p_option.m_targetFormat == ExportFormat::PDF && p_option.m_pdfOption.m_useWkhtmltopdf) {
        // Check wkhtmltopdf executable.
        const auto &wkExePath = p_option.m_pdfOption.m_wkhtmltopdfExePath;
        if (wkExePath.isEmpty() || !QFileInfo::exists(wkExePath)) {
            appendLog(tr("Please specify a valid wkhtmltopdf executable file (%1)").arg(wkExePath));
            return 0;
        }
    }

    int exportedFilesCount = 0;

    switch (p_option.m_source) {
    case ExportSource::CurrentBuffer:
    {
        Q_ASSERT(m_buffer);
        const auto outputFile = getExporter()->doExport(p_option, m_buffer);
        exportedFilesCount = outputFile.isEmpty() ? 0 : 1;
        if (exportedFilesCount == 1 && p_option.m_targetFormat == ExportFormat::HTML) {
            m_exportedFile = outputFile;
        }
        break;
    }

    case ExportSource::CurrentFolder:
    {
        Q_ASSERT(m_folder);
        const auto outputFiles = getExporter()->doExport(p_option, m_folder);
        exportedFilesCount = outputFiles.size();
        break;
    }

    case ExportSource::CurrentNotebook:
    {
        Q_ASSERT(m_notebook);
        const auto outputFiles = getExporter()->doExport(p_option, m_notebook);
        exportedFilesCount = outputFiles.size();
        break;
    }
    }

    return exportedFilesCount;
}

Exporter *ExportDialog::getExporter()
{
    if (!m_exporter) {
        m_exporter = new Exporter(this);
        connect(m_exporter, &Exporter::progressUpdated,
                this, &ExportDialog::updateProgress);
        connect(m_exporter, &Exporter::logRequested,
                this, &ExportDialog::appendLog);
    }
    return m_exporter;
}

void ExportDialog::updateProgress(int p_val, int p_maximum)
{
    m_progressBar->setMaximum(p_maximum);
    m_progressBar->setValue(p_val);
}

QWidget *ExportDialog::getHtmlAdvancedSettings()
{
    if (!m_advancedSettings[AdvancedSettings::HTML]) {
        // Setup HTML advanced settings.
        QWidget *widget = new QWidget(m_advancedGroupBox);
        auto layout = WidgetsFactory::createFormLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);

        {
            m_embedStylesCheckBox = WidgetsFactory::createCheckBox(tr("Embed styles"), widget);
            layout->addRow(m_embedStylesCheckBox);
        }

        {
            m_embedImagesCheckBox = WidgetsFactory::createCheckBox(tr("Embed images"), widget);
            layout->addRow(m_embedImagesCheckBox);
        }

        {
            m_completePageCheckBox = WidgetsFactory::createCheckBox(tr("Complete page"), widget);
            m_completePageCheckBox->setToolTip(tr("Export the whole page along with images which may change the links structure"));
            connect(m_completePageCheckBox, &QCheckBox::stateChanged,
                    this, [this](int p_state) {
                        bool checked = p_state == Qt::Checked;
                        m_embedImagesCheckBox->setEnabled(checked);
                    });
            layout->addRow(m_completePageCheckBox);
        }

        {
            m_useMimeHtmlFormatCheckBox = WidgetsFactory::createCheckBox(tr("Mime HTML format"), widget);
            connect(m_useMimeHtmlFormatCheckBox, &QCheckBox::stateChanged,
                    this, [this](int p_state) {
                        bool checked = p_state == Qt::Checked;
                        m_embedStylesCheckBox->setEnabled(!checked);
                        m_completePageCheckBox->setEnabled(!checked);
                    });
            // TODO: do not support MHTML for now.
            m_useMimeHtmlFormatCheckBox->setEnabled(false);
            layout->addRow(m_useMimeHtmlFormatCheckBox);
        }

        {
            m_addOutlinePanelCheckBox = WidgetsFactory::createCheckBox(tr("Add outline panel"), widget);
            layout->addRow(m_addOutlinePanelCheckBox);
        }

        m_advancedGroupBox->layout()->addWidget(widget);

        m_advancedSettings[AdvancedSettings::HTML] = widget;

        restoreFields(m_option.m_htmlOption);
    }

    return m_advancedSettings[AdvancedSettings::HTML];
}

void ExportDialog::showAdvancedSettings(AdvancedSettings p_settings)
{
    for (int i = AdvancedSettings::General + 1; i < m_advancedSettings.size(); ++i) {
        if (m_advancedSettings[i]) {
            m_advancedSettings[i]->hide();
        }
    }

    QWidget *widget = nullptr;
    switch (p_settings) {
    case AdvancedSettings::HTML:
        widget = getHtmlAdvancedSettings();
        break;

    case AdvancedSettings::PDF:
        widget = getPdfAdvancedSettings();
        break;

    default:
        break;
    }

    if (widget) {
        widget->show();
    }
}

void ExportDialog::restoreFields(const ExportHtmlOption &p_option)
{
    m_embedStylesCheckBox->setChecked(p_option.m_embedStyles);
    m_embedImagesCheckBox->setChecked(p_option.m_embedImages);
    m_completePageCheckBox->setChecked(p_option.m_completePage);
    m_useMimeHtmlFormatCheckBox->setChecked(p_option.m_useMimeHtmlFormat);
    m_addOutlinePanelCheckBox->setChecked(p_option.m_addOutlinePanel);
}

void ExportDialog::saveFields(ExportHtmlOption &p_option)
{
    p_option.m_embedStyles = m_embedStylesCheckBox->isChecked();
    p_option.m_embedImages = m_embedImagesCheckBox->isChecked();
    p_option.m_completePage = m_completePageCheckBox->isChecked();
    p_option.m_useMimeHtmlFormat = m_useMimeHtmlFormatCheckBox->isChecked();
    p_option.m_addOutlinePanel = m_addOutlinePanelCheckBox->isChecked();
}

QWidget *ExportDialog::getPdfAdvancedSettings()
{
    if (!m_advancedSettings[AdvancedSettings::PDF]) {
        QWidget *widget = new QWidget(m_advancedGroupBox);
        auto layout = WidgetsFactory::createFormLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);

        {
            m_pageLayoutBtn = new QPushButton(tr("Settings"), widget);
            connect(m_pageLayoutBtn, &QPushButton::clicked,
                    this, [this]() {
                        QPrinter printer;
                        printer.setPageLayout(*m_pageLayout);

                        QPageSetupDialog dlg(&printer, this);
                        if (dlg.exec() != QDialog::Accepted) {
                            return;
                        }

                        m_pageLayout->setUnits(QPageLayout::Millimeter);
                        m_pageLayout->setPageSize(printer.pageLayout().pageSize());
                        m_pageLayout->setMargins(printer.pageLayout().margins(QPageLayout::Millimeter));
                        m_pageLayout->setOrientation(printer.pageLayout().orientation());

                        updatePageLayoutButtonLabel();
                    });
            layout->addRow(tr("Page layout:"), m_pageLayoutBtn);
        }

        {
            m_addTableOfContentsCheckBox = WidgetsFactory::createCheckBox(tr("Add Table-Of-Contents"), widget);
            layout->addRow(m_addTableOfContentsCheckBox);
        }

        {
            auto useLayout = new QHBoxLayout();

            m_useWkhtmltopdfCheckBox = WidgetsFactory::createCheckBox(tr("Use wkhtmltopdf"), widget);
            useLayout->addWidget(m_useWkhtmltopdfCheckBox);

            auto downloadBtn = new QPushButton(tr("Download"), widget);
            connect(downloadBtn, &QPushButton::clicked,
                    this, []() {
                        WidgetUtils::openUrlByDesktop(QUrl("https://wkhtmltopdf.org/downloads.html"));
                    });
            useLayout->addWidget(downloadBtn);

            layout->addRow(useLayout);
        }

        {
            auto pathLayout = new QHBoxLayout();

            m_wkhtmltopdfExePathLineEdit = WidgetsFactory::createLineEdit(widget);
            pathLayout->addWidget(m_wkhtmltopdfExePathLineEdit);

            auto browseBtn = new QPushButton(tr("Browse"), widget);
            pathLayout->addWidget(browseBtn);
            connect(browseBtn, &QPushButton::clicked,
                    this, [this]() {
                        QString filePath = QFileDialog::getOpenFileName(this,
                                                                        tr("Select wkhtmltopdf Executable"),
                                                                        QCoreApplication::applicationDirPath());

                        if (!filePath.isEmpty()) {
                            m_wkhtmltopdfExePathLineEdit->setText(filePath);
                        }
                    });

            layout->addRow(tr("Wkhtmltopdf path:"), pathLayout);
        }

        {
            m_wkhtmltopdfArgsLineEdit = WidgetsFactory::createLineEdit(widget);
            layout->addRow(tr("Wkhtmltopdf arguments:"), m_wkhtmltopdfArgsLineEdit);
        }

        m_advancedGroupBox->layout()->addWidget(widget);

        m_advancedSettings[AdvancedSettings::PDF] = widget;

        restoreFields(m_option.m_pdfOption);
    }

    return m_advancedSettings[AdvancedSettings::PDF];
}

void ExportDialog::restoreFields(const ExportPdfOption &p_option)
{
    m_pageLayout = p_option.m_layout;
    updatePageLayoutButtonLabel();

    m_addTableOfContentsCheckBox->setChecked(p_option.m_addTableOfContents);
    m_useWkhtmltopdfCheckBox->setChecked(p_option.m_useWkhtmltopdf);
    m_wkhtmltopdfExePathLineEdit->setText(p_option.m_wkhtmltopdfExePath);
    m_wkhtmltopdfArgsLineEdit->setText(p_option.m_wkhtmltopdfArgs);
}

void ExportDialog::saveFields(ExportPdfOption &p_option)
{
    p_option.m_layout = m_pageLayout;
    p_option.m_addTableOfContents = m_addTableOfContentsCheckBox->isChecked();
    p_option.m_useWkhtmltopdf = m_useWkhtmltopdfCheckBox->isChecked();
    p_option.m_wkhtmltopdfExePath = m_wkhtmltopdfExePathLineEdit->text();
    p_option.m_wkhtmltopdfArgs = m_wkhtmltopdfArgsLineEdit->text();
}

void ExportDialog::updatePageLayoutButtonLabel()
{
    Q_ASSERT(m_pageLayout);
    m_pageLayoutBtn->setText(
        QString("%1, %2").arg(m_pageLayout->pageSize().name(),
                              m_pageLayout->orientation() == QPageLayout::Portrait ? tr("Portrait") : tr("Landscape")));
}
