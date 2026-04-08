#include "exportdialog2.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPageLayout>
#include <QPageSetupDialog>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

#include <controllers/exportcontroller.h>
#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/sessionconfig.h>
#include <gui/services/themeservice.h>

#include "../locationinputwithbrowsebutton.h"
#include "../widgetsfactory.h"

using namespace vnotex;

namespace {
QString sourceText(ExportSource p_source, const ExportContext &p_context) {
  switch (p_source) {
  case ExportSource::CurrentBuffer:
    if (!p_context.bufferName.isEmpty()) {
      return ExportDialog2::tr("Current Buffer (%1)").arg(p_context.bufferName);
    }
    return ExportDialog2::tr("Current Buffer");

  case ExportSource::CurrentNote:
    if (p_context.currentNodeId.isValid()) {
      return ExportDialog2::tr("Current Note (%1)").arg(p_context.currentNodeId.relativePath);
    }
    return ExportDialog2::tr("Current Note");

  case ExportSource::CurrentFolder:
    if (p_context.currentFolderId.isValid()) {
      return ExportDialog2::tr("Current Folder (%1)").arg(p_context.currentFolderId.relativePath);
    }
    return ExportDialog2::tr("Current Folder");

  case ExportSource::CurrentNotebook:
    if (!p_context.notebookId.isEmpty()) {
      return ExportDialog2::tr("Current Notebook (%1)").arg(p_context.notebookId);
    }
    return ExportDialog2::tr("Current Notebook");
  }

  return ExportDialog2::tr("Unknown Source");
}

bool sourceAvailable(ExportSource p_source, const ExportContext &p_context) {
  switch (p_source) {
  case ExportSource::CurrentBuffer:
    return !p_context.bufferContent.isEmpty();

  case ExportSource::CurrentNote:
    return p_context.currentNodeId.isValid() && !p_context.currentNodeId.relativePath.isEmpty();

  case ExportSource::CurrentFolder:
    return p_context.currentFolderId.isValid();

  case ExportSource::CurrentNotebook:
    return !p_context.notebookId.isEmpty();
  }

  return false;
}

QString sourceUnavailableReason(ExportSource p_source) {
  switch (p_source) {
  case ExportSource::CurrentBuffer:
    return ExportDialog2::tr("No current buffer content available");

  case ExportSource::CurrentNote:
    return ExportDialog2::tr("No current note available");

  case ExportSource::CurrentFolder:
    return ExportDialog2::tr("No current folder available");

  case ExportSource::CurrentNotebook:
    return ExportDialog2::tr("No current notebook available");
  }

  return QString();
}

int findCustomOption(const QVector<ExportCustomOption> &p_options, const QString &p_name) {
  if (p_name.isEmpty()) {
    return -1;
  }

  for (int i = 0; i < p_options.size(); ++i) {
    if (p_options[i].m_name == p_name) {
      return i;
    }
  }

  return -1;
}
} // namespace

ExportDialog2::ExportDialog2(ServiceLocator &p_services, const ExportContext &p_context,
                             QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services), m_context(p_context) {
  m_controller = new ExportController(m_services, this);

  setupUI();

  connect(this, &QDialog::finished, this, [this](int) {
    saveConfig();
    m_controller->stop();
  });

  connect(m_controller, &ExportController::exportFinished, this, &ExportDialog2::onExportFinished);
  connect(m_controller, &ExportController::progressUpdated, this,
          &ExportDialog2::onProgressUpdated);
  connect(m_controller, &ExportController::logRequested, this, &ExportDialog2::onLogRequested);

  loadConfig();
  m_sourceCombo->setFocus();
}

ExportDialog2::~ExportDialog2() = default;

void ExportDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *mainLayout = new QVBoxLayout(mainWidget);

  // Top: source + format.
  auto *topLayout = new QHBoxLayout();

  auto *sourceLabel = new QLabel(tr("Source:"), mainWidget);
  topLayout->addWidget(sourceLabel);

  m_sourceCombo = WidgetsFactory::createComboBox(mainWidget);
  for (int i = static_cast<int>(ExportSource::CurrentBuffer);
       i <= static_cast<int>(ExportSource::CurrentNotebook); ++i) {
    auto source = static_cast<ExportSource>(i);
    m_sourceCombo->addItem(sourceText(source, m_context), i);
    const int idx = m_sourceCombo->count() - 1;
    auto item = m_sourceCombo->model()->index(idx, 0);
    m_sourceCombo->model()->setData(item, 1, Qt::UserRole - 1);

    if (!sourceAvailable(source, m_context)) {
      m_sourceCombo->model()->setData(item, 0, Qt::UserRole - 1);
      m_sourceCombo->setItemData(idx, sourceUnavailableReason(source), Qt::ToolTipRole);
    }
  }
  topLayout->addWidget(m_sourceCombo, 1);

  auto *formatLabel = new QLabel(tr("Format:"), mainWidget);
  topLayout->addWidget(formatLabel);

  m_formatCombo = WidgetsFactory::createComboBox(mainWidget);
  m_formatCombo->addItem(tr("Markdown"), static_cast<int>(ExportFormat::Markdown));
  m_formatCombo->addItem(tr("HTML"), static_cast<int>(ExportFormat::HTML));
  m_formatCombo->addItem(tr("PDF"), static_cast<int>(ExportFormat::PDF));
  m_formatCombo->addItem(tr("Custom"), static_cast<int>(ExportFormat::Custom));
  topLayout->addWidget(m_formatCombo, 1);

  mainLayout->addLayout(topLayout);

  // Middle: format-specific stacked pages.
  m_stackedWidget = new QStackedWidget(mainWidget);
  connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), m_stackedWidget,
          &QStackedWidget::setCurrentIndex);

  // Markdown page.
  {
    auto *page = new QWidget(m_stackedWidget);
    auto *layout = WidgetsFactory::createFormLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_exportAttachmentsCheck = WidgetsFactory::createCheckBox(tr("Export attachments"), page);
    layout->addRow(m_exportAttachmentsCheck);

    m_stackedWidget->addWidget(page);
  }

  // HTML page.
  {
    auto *page = new QWidget(m_stackedWidget);
    auto *layout = WidgetsFactory::createFormLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_embedStylesCheck = WidgetsFactory::createCheckBox(tr("Embed styles"), page);
    layout->addRow(m_embedStylesCheck);

    m_embedImagesCheck = WidgetsFactory::createCheckBox(tr("Embed images"), page);
    layout->addRow(m_embedImagesCheck);

    m_completePageCheck = WidgetsFactory::createCheckBox(tr("Complete page"), page);
    layout->addRow(m_completePageCheck);
    connect(m_completePageCheck, &QCheckBox::stateChanged, this,
            [this](int p_state) { m_embedImagesCheck->setEnabled(p_state == Qt::Checked); });

    m_addOutlinePanelCheck = WidgetsFactory::createCheckBox(tr("Add outline panel"), page);
    layout->addRow(m_addOutlinePanelCheck);

    m_useMimeHtmlFormatCheck = WidgetsFactory::createCheckBox(tr("MIME HTML format"), page);
    // MIME HTML format is not yet supported by WebViewExporter.
    m_useMimeHtmlFormatCheck->setChecked(false);
    m_useMimeHtmlFormatCheck->setEnabled(false);
    m_useMimeHtmlFormatCheck->setVisible(false);
    layout->addRow(m_useMimeHtmlFormatCheck);
    connect(m_useMimeHtmlFormatCheck, &QCheckBox::stateChanged, this, [this](int p_state) {
      const bool checked = p_state == Qt::Checked;
      m_embedStylesCheck->setEnabled(!checked);
      m_completePageCheck->setEnabled(!checked);
    });

    m_scrollableCheck = WidgetsFactory::createCheckBox(tr("Scrollable page"), page);
    layout->addRow(m_scrollableCheck);

    m_stackedWidget->addWidget(page);
  }

  // PDF page.
  {
    auto *page = new QWidget(m_stackedWidget);
    auto *layout = WidgetsFactory::createFormLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_pageLayoutBtn = new QPushButton(tr("Settings"), page);
    layout->addRow(tr("Page layout:"), m_pageLayoutBtn);
    connect(m_pageLayoutBtn, &QPushButton::clicked, this, [this]() {
      QPrinter printer;
      if (m_pageLayout) {
        printer.setPageLayout(*m_pageLayout);
      }

      QPageSetupDialog dlg(&printer, this);
      if (dlg.exec() != QDialog::Accepted) {
        return;
      }

      if (!m_pageLayout) {
        m_pageLayout.reset(new QPageLayout(printer.pageLayout()));
      }

      m_pageLayout->setUnits(QPageLayout::Millimeter);
      m_pageLayout->setPageSize(printer.pageLayout().pageSize());
      m_pageLayout->setMargins(printer.pageLayout().margins(QPageLayout::Millimeter));
      m_pageLayout->setOrientation(printer.pageLayout().orientation());
      updatePageLayoutButtonLabel();
    });

    m_addTableOfContentsCheck = WidgetsFactory::createCheckBox(tr("Add table of contents"), page);
    layout->addRow(m_addTableOfContentsCheck);

    m_useWkhtmltopdfCheck =
        WidgetsFactory::createCheckBox(tr("Use wkhtmltopdf (outline supported)"), page);
    layout->addRow(m_useWkhtmltopdfCheck);

    m_wkhtmltopdfExePathEdit = WidgetsFactory::createLineEdit(page);
    layout->addRow(tr("Wkhtmltopdf path:"), m_wkhtmltopdfExePathEdit);

    m_wkhtmltopdfArgsEdit = WidgetsFactory::createLineEdit(page);
    layout->addRow(tr("Wkhtmltopdf arguments:"), m_wkhtmltopdfArgsEdit);

    m_pdfAllInOneCheck = WidgetsFactory::createCheckBox(tr("All-in-One"), page);
    m_pdfAllInOneCheck->setToolTip(tr("Export all source files into one file"));
    layout->addRow(m_pdfAllInOneCheck);

    connect(m_useWkhtmltopdfCheck, &QCheckBox::stateChanged, this,
            [this](int) { updatePdfWidgetsByWkhtmltopdf(); });

    m_stackedWidget->addWidget(page);
  }

  // Custom page.
  {
    auto *page = new QWidget(m_stackedWidget);
    auto *layout = WidgetsFactory::createFormLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *schemeLayout = new QHBoxLayout();
    m_customSchemeCombo = WidgetsFactory::createComboBox(page);
    schemeLayout->addWidget(m_customSchemeCombo, 1);

    auto *newSchemeBtn = new QPushButton(tr("New"), page);
    schemeLayout->addWidget(newSchemeBtn);
    connect(newSchemeBtn, &QPushButton::clicked, this, &ExportDialog2::addCustomScheme);

    auto *deleteSchemeBtn = new QPushButton(tr("Delete"), page);
    schemeLayout->addWidget(deleteSchemeBtn);
    connect(deleteSchemeBtn, &QPushButton::clicked, this, &ExportDialog2::removeCustomScheme);

    layout->addRow(tr("Scheme:"), schemeLayout);

    m_targetSuffixEdit = WidgetsFactory::createLineEdit(page);
    layout->addRow(tr("Target suffix:"), m_targetSuffixEdit);

    m_resourceSeparatorEdit = WidgetsFactory::createLineEdit(page);
    layout->addRow(tr("Resource path separator:"), m_resourceSeparatorEdit);

    m_useHtmlInputCheck = WidgetsFactory::createCheckBox(tr("Use HTML input"), page);
    layout->addRow(m_useHtmlInputCheck);

    m_customAllInOneCheck = WidgetsFactory::createCheckBox(tr("All-in-One"), page);
    m_customAllInOneCheck->setToolTip(tr("Export all source files into one file"));
    layout->addRow(m_customAllInOneCheck);

    m_targetScrollableCheck = WidgetsFactory::createCheckBox(tr("Target page scrollable"), page);
    layout->addRow(m_targetScrollableCheck);

    m_customCommandEdit = WidgetsFactory::createPlainTextEdit(page);
    m_customCommandEdit->setMaximumHeight(m_customCommandEdit->minimumSizeHint().height());
    layout->addRow(tr("Command:"), m_customCommandEdit);

    connect(m_customSchemeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ExportDialog2::onCustomSchemeChanged);

    m_stackedWidget->addWidget(page);
  }

  mainLayout->addWidget(m_stackedWidget);

  // Common options.
  auto *commonWidget = new QWidget(mainWidget);
  auto *commonLayout = new QGridLayout(commonWidget);

  m_outputDirInput = new LocationInputWithBrowseButton(commonWidget, getDefaultOutputDir());
  m_outputDirInput->setBrowseType(LocationInputWithBrowseButton::Folder,
                                  tr("Select Export Output Directory"));
  commonLayout->addWidget(new QLabel(tr("Output directory:"), commonWidget), 0, 0);
  commonLayout->addWidget(m_outputDirInput, 0, 1, 1, 3);

  const auto webStyles = m_services.get<ThemeService>()->getWebStyles();

  m_renderingStyleCombo = WidgetsFactory::createComboBox(commonWidget);
  m_syntaxStyleCombo = WidgetsFactory::createComboBox(commonWidget);
  for (const auto &style : webStyles) {
    m_renderingStyleCombo->addItem(style.first, style.second);
    m_syntaxStyleCombo->addItem(style.first, style.second);
  }

  commonLayout->addWidget(new QLabel(tr("Rendering style:"), commonWidget), 1, 0);
  commonLayout->addWidget(m_renderingStyleCombo, 1, 1);
  commonLayout->addWidget(new QLabel(tr("Syntax style:"), commonWidget), 1, 2);
  commonLayout->addWidget(m_syntaxStyleCombo, 1, 3);

  m_transparentBgCheck =
      WidgetsFactory::createCheckBox(tr("Use transparent background"), commonWidget);
  m_recursiveCheck = WidgetsFactory::createCheckBox(tr("Process sub-folders"), commonWidget);
  commonLayout->addWidget(m_transparentBgCheck, 2, 0, 1, 2);
  commonLayout->addWidget(m_recursiveCheck, 2, 2, 1, 2);

  mainLayout->addWidget(commonWidget);

  // Bottom: progress + log.
  m_progressBar = new QProgressBar(mainWidget);
  m_progressBar->hide();
  m_progressBar->setRange(0, 0);
  mainLayout->addWidget(m_progressBar);

  m_logEdit = WidgetsFactory::createPlainTextConsole(mainWidget);
  m_logEdit->setReadOnly(true);
  m_logEdit->setMaximumHeight(m_logEdit->minimumSizeHint().height());
  mainLayout->addWidget(m_logEdit);

  setCentralWidget(mainWidget);

  // Bottom buttons.
  setDialogButtonBox(QDialogButtonBox::Cancel);
  auto *box = getDialogButtonBox();

  m_exportBtn = box->addButton(tr("Export"), QDialogButtonBox::ActionRole);
  connect(m_exportBtn, &QPushButton::clicked, this, &ExportDialog2::onExportClicked);

  m_openOutputDirBtn = box->addButton(tr("Open Output Dir"), QDialogButtonBox::ActionRole);
  connect(m_openOutputDirBtn, &QPushButton::clicked, this, [this]() {
    const auto dir = m_outputDirInput->text();
    if (!dir.isEmpty()) {
      QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    }
  });

  m_cancelBtn = box->button(QDialogButtonBox::Cancel);
  connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
    m_controller->stop();
    close();
  });

  setWindowTitle(tr("Export"));
}

void ExportDialog2::loadConfig() {
  auto *cfg = m_services.get<ConfigMgr2>();
  const auto option = cfg->getSessionConfig().getExportOption();
  m_customOptions = cfg->getSessionConfig().getCustomExportOptions();
  refreshCustomSchemes(option.m_customExport);
  restoreFields(option);

  const int presetIdx = m_sourceCombo->findData(static_cast<int>(m_context.presetSource));
  if (presetIdx >= 0 &&
      m_sourceCombo->model()
              ->data(m_sourceCombo->model()->index(presetIdx, 0), Qt::UserRole - 1)
              .toInt() == 1) {
    m_sourceCombo->setCurrentIndex(presetIdx);
  }
}

void ExportDialog2::saveConfig() {
  m_services.get<ConfigMgr2>()->getSessionConfig().setExportOption(collectFields());
  m_services.get<ConfigMgr2>()->getSessionConfig().setCustomExportOptions(m_customOptions);
}

void ExportDialog2::restoreFields(const ExportOption &p_option) {
  int idx = m_sourceCombo->findData(static_cast<int>(p_option.m_source));
  if (idx >= 0 && m_sourceCombo->model()
                          ->data(m_sourceCombo->model()->index(idx, 0), Qt::UserRole - 1)
                          .toInt() == 1) {
    m_sourceCombo->setCurrentIndex(idx);
  }

  idx = m_formatCombo->findData(static_cast<int>(p_option.m_targetFormat));
  if (idx >= 0) {
    m_formatCombo->setCurrentIndex(idx);
    m_stackedWidget->setCurrentIndex(idx);
  }

  m_outputDirInput->setText(p_option.m_outputDir.isEmpty() ? getDefaultOutputDir()
                                                           : p_option.m_outputDir);
  m_transparentBgCheck->setChecked(p_option.m_useTransparentBg);
  m_recursiveCheck->setChecked(p_option.m_recursive);
  m_exportAttachmentsCheck->setChecked(p_option.m_exportAttachments);

  idx = m_renderingStyleCombo->findData(p_option.m_renderingStyleFile);
  if (idx >= 0) {
    m_renderingStyleCombo->setCurrentIndex(idx);
  }

  idx = m_syntaxStyleCombo->findData(p_option.m_syntaxHighlightStyleFile);
  if (idx >= 0) {
    m_syntaxStyleCombo->setCurrentIndex(idx);
  }

  restoreHtmlFields(p_option.m_htmlOption);
  restorePdfFields(p_option.m_pdfOption);

  const int customIdx = findCustomOption(m_customOptions, p_option.m_customExport);
  if (customIdx >= 0) {
    m_customSchemeCombo->setCurrentIndex(customIdx);
  } else {
    onCustomSchemeChanged(m_customSchemeCombo->currentIndex());
  }
}

ExportOption ExportDialog2::collectFields() {
  ExportOption option;
  option.m_source = static_cast<ExportSource>(m_sourceCombo->currentData().toInt());
  option.m_targetFormat = static_cast<ExportFormat>(m_formatCombo->currentData().toInt());
  option.m_outputDir = m_outputDirInput->text();
  option.m_useTransparentBg = m_transparentBgCheck->isChecked();
  option.m_recursive = m_recursiveCheck->isChecked();
  option.m_exportAttachments = m_exportAttachmentsCheck->isChecked();
  option.m_renderingStyleFile = m_renderingStyleCombo->currentData().toString();
  option.m_syntaxHighlightStyleFile = m_syntaxStyleCombo->currentData().toString();

  saveHtmlFields(option.m_htmlOption);
  savePdfFields(option.m_pdfOption);

  saveCurrentCustomScheme();
  option.m_customExport = m_customSchemeCombo->currentData().toString();
  const int customIdx = findCustomOption(m_customOptions, option.m_customExport);
  if (customIdx >= 0) {
    option.m_customOption = &m_customOptions[customIdx];
  }

  return option;
}

void ExportDialog2::restoreHtmlFields(const ExportHtmlOption &p_option) {
  m_embedStylesCheck->setChecked(p_option.m_embedStyles);
  m_embedImagesCheck->setChecked(p_option.m_embedImages);
  m_completePageCheck->setChecked(p_option.m_completePage);
  m_addOutlinePanelCheck->setChecked(p_option.m_addOutlinePanel);
  m_useMimeHtmlFormatCheck->setChecked(false);
  m_scrollableCheck->setChecked(p_option.m_scrollable);
}

void ExportDialog2::saveHtmlFields(ExportHtmlOption &p_option) const {
  p_option.m_embedStyles = m_embedStylesCheck->isChecked();
  p_option.m_embedImages = m_embedImagesCheck->isChecked();
  p_option.m_completePage = m_completePageCheck->isChecked();
  p_option.m_addOutlinePanel = m_addOutlinePanelCheck->isChecked();
  p_option.m_useMimeHtmlFormat = false;
  p_option.m_scrollable = m_scrollableCheck->isChecked();
}

void ExportDialog2::restorePdfFields(const ExportPdfOption &p_option) {
  m_pageLayout = p_option.m_layout;
  updatePageLayoutButtonLabel();

  m_addTableOfContentsCheck->setChecked(p_option.m_addTableOfContents);
  m_useWkhtmltopdfCheck->setChecked(p_option.m_useWkhtmltopdf);
  m_wkhtmltopdfExePathEdit->setText(p_option.m_wkhtmltopdfExePath);
  m_wkhtmltopdfArgsEdit->setText(p_option.m_wkhtmltopdfArgs);
  m_pdfAllInOneCheck->setChecked(p_option.m_allInOne);

  updatePdfWidgetsByWkhtmltopdf();
}

void ExportDialog2::savePdfFields(ExportPdfOption &p_option) const {
  p_option.m_layout = m_pageLayout;
  p_option.m_addTableOfContents = m_addTableOfContentsCheck->isChecked();
  p_option.m_useWkhtmltopdf = m_useWkhtmltopdfCheck->isChecked();
  p_option.m_wkhtmltopdfExePath = m_wkhtmltopdfExePathEdit->text();
  p_option.m_wkhtmltopdfArgs = m_wkhtmltopdfArgsEdit->text();
  p_option.m_allInOne = m_pdfAllInOneCheck->isChecked();
}

void ExportDialog2::refreshCustomSchemes(const QString &p_currentName) {
  m_customSchemeCombo->clear();
  for (const auto &opt : m_customOptions) {
    m_customSchemeCombo->addItem(opt.m_name, opt.m_name);
  }

  int idx = findCustomOption(m_customOptions, p_currentName);
  if (idx < 0 && !m_customOptions.isEmpty()) {
    idx = 0;
  }

  m_customSchemeCombo->setCurrentIndex(idx);
}

void ExportDialog2::onCustomSchemeChanged(int p_comboIdx) {
  const bool enabled = p_comboIdx >= 0;
  m_targetSuffixEdit->setEnabled(enabled);
  m_resourceSeparatorEdit->setEnabled(enabled);
  m_useHtmlInputCheck->setEnabled(enabled);
  m_customAllInOneCheck->setEnabled(enabled);
  m_targetScrollableCheck->setEnabled(enabled);
  m_customCommandEdit->setEnabled(enabled);

  if (!enabled) {
    m_targetSuffixEdit->clear();
    m_resourceSeparatorEdit->clear();
    m_useHtmlInputCheck->setChecked(false);
    m_customAllInOneCheck->setChecked(false);
    m_targetScrollableCheck->setChecked(false);
    m_customCommandEdit->clear();
    return;
  }

  const auto &opt = m_customOptions[p_comboIdx];
  m_targetSuffixEdit->setText(opt.m_targetSuffix);
  m_resourceSeparatorEdit->setText(opt.m_resourcePathSeparator);
  m_useHtmlInputCheck->setChecked(opt.m_useHtmlInput);
  m_customAllInOneCheck->setChecked(opt.m_allInOne);
  m_targetScrollableCheck->setChecked(opt.m_targetPageScrollable);
  m_customCommandEdit->setPlainText(opt.m_command);
}

void ExportDialog2::addCustomScheme() {
  saveCurrentCustomScheme();

  QString name;
  while (true) {
    name = QInputDialog::getText(this, tr("New Custom Export Scheme"), tr("Scheme name:"));
    if (name.isEmpty()) {
      return;
    }

    if (findCustomOption(m_customOptions, name) != -1) {
      setInformationText(tr("Name conflicts with existing scheme."),
                         ScrollDialog::InformationLevel::Warning);
      continue;
    }

    break;
  }

  ExportCustomOption option;
  if (m_customSchemeCombo->currentIndex() >= 0) {
    option = m_customOptions[m_customSchemeCombo->currentIndex()];
  }
  option.m_name = name;

  m_customOptions.append(option);
  refreshCustomSchemes(name);
}

void ExportDialog2::removeCustomScheme() {
  const int idx = m_customSchemeCombo->currentIndex();
  if (idx < 0) {
    return;
  }

  m_customOptions.remove(idx);
  refreshCustomSchemes();
}

void ExportDialog2::saveCurrentCustomScheme() {
  const int idx = m_customSchemeCombo->currentIndex();
  if (idx < 0) {
    return;
  }

  auto &opt = m_customOptions[idx];
  opt.m_targetSuffix = m_targetSuffixEdit->text().trimmed();
  opt.m_resourcePathSeparator = m_resourceSeparatorEdit->text().trimmed();
  opt.m_useHtmlInput = m_useHtmlInputCheck->isChecked();
  opt.m_allInOne = m_customAllInOneCheck->isChecked();
  opt.m_targetPageScrollable = m_targetScrollableCheck->isChecked();
  opt.m_command = m_customCommandEdit->toPlainText().trimmed();
  const int lineIdx = opt.m_command.indexOf(QLatin1Char('\n'));
  if (lineIdx > -1) {
    opt.m_command = opt.m_command.left(lineIdx);
  }
}

void ExportDialog2::updatePageLayoutButtonLabel() {
  if (!m_pageLayout) {
    m_pageLayout.reset(new QPageLayout());
  }

  m_pageLayoutBtn->setText(QStringLiteral("%1, %2").arg(
      m_pageLayout->pageSize().name(),
      m_pageLayout->orientation() == QPageLayout::Portrait ? tr("Portrait") : tr("Landscape")));
}

QString ExportDialog2::getDefaultOutputDir() const {
  return ConfigMgr2::getDocumentOrHomePath() + QStringLiteral("/") + tr("vnote_exports");
}

void ExportDialog2::onExportClicked() {
  if (m_exporting || m_controller->isExporting()) {
    return;
  }

  clearInformationText();
  m_logEdit->clear();
  saveCurrentCustomScheme();

  updateUiOnExportState(true);
  m_controller->doExport(collectFields(), m_context);
}

void ExportDialog2::onExportFinished(const QStringList &p_outputFiles) {
  updateUiOnExportState(false);
  m_logEdit->appendPlainText(tr("Export finished: %1 file(s)").arg(p_outputFiles.size()));
}

void ExportDialog2::onProgressUpdated(int p_val, int p_maximum) {
  if (p_maximum < m_progressBar->value()) {
    m_progressBar->setValue(p_val);
    m_progressBar->setMaximum(p_maximum);
  } else {
    m_progressBar->setMaximum(p_maximum);
    m_progressBar->setValue(p_val);
  }
}

void ExportDialog2::onLogRequested(const QString &p_log) {
  m_logEdit->appendPlainText(QStringLiteral(">>> ") + p_log);
  m_logEdit->moveCursor(QTextCursor::End);
  QCoreApplication::sendPostedEvents();
}

void ExportDialog2::updatePdfWidgetsByWkhtmltopdf() {
  const bool enabled = m_useWkhtmltopdfCheck->isChecked();
  m_wkhtmltopdfExePathEdit->setEnabled(enabled);
  m_wkhtmltopdfArgsEdit->setEnabled(enabled);
  m_pdfAllInOneCheck->setEnabled(enabled);
}

void ExportDialog2::updateUiOnExportState(bool p_exporting) {
  m_exporting = p_exporting;
  m_exportBtn->setEnabled(!p_exporting);
  m_sourceCombo->setEnabled(!p_exporting);
  m_formatCombo->setEnabled(!p_exporting);

  if (p_exporting) {
    m_progressBar->setMaximum(0);
    m_progressBar->show();
  } else {
    m_progressBar->hide();
  }
}

void ExportDialog2::rejectedButtonClicked() {
  if (m_exporting || m_controller->isExporting()) {
    m_controller->stop();
  }

  Dialog::rejectedButtonClicked();
}
