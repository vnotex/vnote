#include "importfolderdialog2.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <controllers/importfoldercontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <utils/pathutils.h>
#include <vxcore/notebook_json_keys.h>

#include "../locationinputwithbrowsebutton.h"
#include "folderfilesfilterwidget.h"

using namespace vnotex;

namespace {

// QObject names that tests use to discover widgets via findChild<>().
const char *const kExternalModeRadioName = "externalModeRadio";
const char *const kBundleModeRadioName = "bundleModeRadio";
const char *const kModeStackName = "modeStack";
const char *const kFilterWidgetName = "folderFilesFilterWidget";
const char *const kBundleInputName = "bundlePathInput";
const char *const kBundlePreviewName = "bundlePreviewLabel";
const char *const kParentPathLabelName = "parentPathLabel";
const char *const kOkButtonName = "importOkButton";
const char *const kCancelButtonName = "importCancelButton";

} // namespace

ImportFolderDialog2::ImportFolderDialog2(ServiceLocator &p_services,
                                         const NodeIdentifier &p_parentId, QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services), m_parentId(p_parentId) {
  // Create controller.
  m_controller = new ImportFolderController(m_services, this);

  setupUI();
  applyBundleModeAvailability();

  m_filterWidget->getFolderPathEdit()->setFocus();
}

ImportFolderDialog2::~ImportFolderDialog2() = default;

void ImportFolderDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *mainLayout = new QVBoxLayout(mainWidget);

  // Show parent folder path for context.
  auto *notebookService = m_services.get<NotebookCoreService>();
  QString parentPath;
  if (notebookService) {
    QJsonObject notebookConfig = notebookService->getNotebookConfig(m_parentId.notebookId);
    QString rootPath = notebookConfig.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
    parentPath = PathUtils::concatenateFilePath(rootPath, m_parentId.relativePath);
  }

  auto *label = new QLabel(tr("Import folder into (%1).").arg(parentPath), mainWidget);
  label->setObjectName(QLatin1String(kParentPathLabelName));
  label->setWordWrap(true);
  mainLayout->addWidget(label);

  // Top: mode selector radios.
  auto *modeRow = new QHBoxLayout();
  m_externalModeRadio = new QRadioButton(tr("External folder"), mainWidget);
  m_externalModeRadio->setObjectName(QLatin1String(kExternalModeRadioName));
  m_externalModeRadio->setToolTip(
      tr("Copy an ordinary folder from disk into this notebook. New ids and timestamps are "
         "generated for the imported notes."));
  m_externalModeRadio->setChecked(true);

  m_bundleModeRadio = new QRadioButton(tr("Shared folder from VNote"), mainWidget);
  m_bundleModeRadio->setObjectName(QLatin1String(kBundleModeRadioName));
  m_bundleModeRadio->setToolTip(
      tr("Import a folder shared from VNote, keeping its notes' ids, dates, tags and "
         "attachments."));

  m_modeGroup = new QButtonGroup(this);
  m_modeGroup->setExclusive(true);
  m_modeGroup->addButton(m_externalModeRadio, static_cast<int>(ExternalMode));
  m_modeGroup->addButton(m_bundleModeRadio, static_cast<int>(BundleMode));

  modeRow->addWidget(m_externalModeRadio);
  modeRow->addWidget(m_bundleModeRadio);
  modeRow->addStretch();
  mainLayout->addLayout(modeRow);

  // Middle: stacked mode-specific area.
  m_modeStack = new QStackedWidget(mainWidget);
  m_modeStack->setObjectName(QLatin1String(kModeStackName));

  auto *externalPage = new QWidget(m_modeStack);
  setupExternalPage(externalPage);
  m_modeStack->addWidget(externalPage);

  auto *bundlePage = new QWidget(m_modeStack);
  setupBundlePage(bundlePage);
  m_modeStack->addWidget(bundlePage);

  m_modeStack->setCurrentIndex(static_cast<int>(ExternalMode));
  mainLayout->addWidget(m_modeStack);

  setCentralWidget(mainWidget);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  if (auto *box = getDialogButtonBox()) {
    if (auto *okBtn = box->button(QDialogButtonBox::Ok)) {
      okBtn->setObjectName(QLatin1String(kOkButtonName));
    }
    if (auto *cancelBtn = box->button(QDialogButtonBox::Cancel)) {
      cancelBtn->setObjectName(QLatin1String(kCancelButtonName));
    }
  }
  setButtonEnabled(QDialogButtonBox::Ok, false);

  setWindowTitle(tr("Import Folder"));

  // Wire mode toggle. Both radios are connected to ONE handler, so the stack,
  // the banner and the button state are updated exactly once per switch.
  connect(m_externalModeRadio, &QRadioButton::toggled, this, &ImportFolderDialog2::onModeChanged);
  connect(m_bundleModeRadio, &QRadioButton::toggled, this, &ImportFolderDialog2::onModeChanged);
}

void ImportFolderDialog2::setupExternalPage(QWidget *p_page) {
  auto *layout = new QVBoxLayout(p_page);
  layout->setContentsMargins(0, 0, 0, 0);

  m_filterWidget = new FolderFilesFilterWidget(p_page);
  m_filterWidget->setObjectName(QLatin1String(kFilterWidgetName));
  layout->addWidget(m_filterWidget);
  connect(m_filterWidget, &FolderFilesFilterWidget::filesChanged, this,
          &ImportFolderDialog2::validateInputs);
}

void ImportFolderDialog2::setupBundlePage(QWidget *p_page) {
  auto *layout = new QVBoxLayout(p_page);
  layout->setContentsMargins(0, 0, 0, 0);

  auto *hint = new QLabel(tr("Select a folder shared from VNote (its name usually ends with "
                             "\"-bundle\")."),
                          p_page);
  hint->setWordWrap(true);
  layout->addWidget(hint);

  m_bundleInput = new LocationInputWithBrowseButton(p_page);
  m_bundleInput->setObjectName(QLatin1String(kBundleInputName));
  m_bundleInput->setBrowseType(LocationInputWithBrowseButton::Folder, tr("Select Shared Folder"));
  m_bundleInput->setPlaceholderText(tr("Folder produced by Share Folder"));
  layout->addWidget(m_bundleInput);
  connect(m_bundleInput, &LocationInputWithBrowseButton::textChanged, this,
          [this](const QString &) { onBundlePathChanged(); });

  m_bundlePreviewLabel = new QLabel(p_page);
  m_bundlePreviewLabel->setObjectName(QLatin1String(kBundlePreviewName));
  m_bundlePreviewLabel->setWordWrap(true);
  layout->addWidget(m_bundlePreviewLabel);

  layout->addStretch(1);
}

void ImportFolderDialog2::applyBundleModeAvailability() {
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService || !m_bundleModeRadio) {
    return;
  }

  // vxcore owns the judgement: bundle import is bundled-only and needs a
  // writable notebook. Ask it once, up front, rather than duplicating the rule.
  const FolderImportPaths paths =
      notebookService->getFolderImportPaths(m_parentId.notebookId, m_parentId.relativePath);
  if (paths.isValid()) {
    return;
  }

  QString reason;
  switch (paths.m_error) {
  case VXCORE_ERR_UNSUPPORTED:
    reason = tr("Only bundled notebooks can import a shared folder.");
    break;
  case VXCORE_ERR_READ_ONLY:
    reason = tr("This notebook is read-only.");
    break;
  default:
    reason = tr("This destination cannot accept a shared folder.");
    break;
  }

  m_bundleModeRadio->setEnabled(false);
  m_bundleModeRadio->setToolTip(reason);
  m_externalModeRadio->setChecked(true);
}

ImportFolderDialog2::Mode ImportFolderDialog2::currentMode() const {
  return m_bundleModeRadio && m_bundleModeRadio->isChecked() ? BundleMode : ExternalMode;
}

void ImportFolderDialog2::onModeChanged() {
  m_modeStack->setCurrentIndex(static_cast<int>(currentMode()));
  // Clear stale validation text from the prior mode.
  setInformationText(QString(), InformationLevel::Info);
  validateInputs();
}

void ImportFolderDialog2::onBundlePathChanged() {
  // Per-mode handlers early-return when the mode does not match, so a signal
  // from the hidden page can never move the OK button.
  if (currentMode() != BundleMode) {
    return;
  }
  validateInputs();
}

void ImportFolderDialog2::validateInputs() {
  if (currentMode() == BundleMode) {
    validateBundleInputs();
  } else {
    validateExternalInputs();
  }
}

void ImportFolderDialog2::validateExternalInputs() {
  if (currentMode() != ExternalMode) {
    return;
  }

  if (!m_filterWidget->isReady()) {
    // Still scanning - disable OK button but don't show error.
    setButtonEnabled(QDialogButtonBox::Ok, false);
    return;
  }

  const auto folderPath = m_filterWidget->getFolderPath();

  // Validate using controller.
  const auto result = m_controller->validateSourceFolder(m_parentId.notebookId,
                                                         m_parentId.relativePath, folderPath);
  setInformationText(result.valid ? QString() : result.message,
                     result.valid ? ScrollDialog::InformationLevel::Info
                                  : ScrollDialog::InformationLevel::Error);
  setButtonEnabled(QDialogButtonBox::Ok, result.valid);
}

void ImportFolderDialog2::validateBundleInputs() {
  if (currentMode() != BundleMode) {
    return;
  }

  const QString bundlePath = m_bundleInput ? m_bundleInput->text().trimmed() : QString();
  if (bundlePath.isEmpty()) {
    // Nothing chosen yet: stay quiet rather than blinking a banner.
    m_bundlePreviewLabel->clear();
    setInformationText(QString(), ScrollDialog::InformationLevel::Info);
    setButtonEnabled(QDialogButtonBox::Ok, false);
    return;
  }

  ImportBundleInput input;
  input.notebookId = m_parentId.notebookId;
  input.parentFolderPath = m_parentId.relativePath;
  input.bundlePath = bundlePath;

  const ImportBundleValidationResult result = m_controller->validateBundle(input);
  if (!result.valid) {
    m_bundlePreviewLabel->clear();
    setInformationText(result.message, ScrollDialog::InformationLevel::Error);
    setButtonEnabled(QDialogButtonBox::Ok, false);
    return;
  }

  m_bundlePreviewLabel->setText(tr("%1 — %2 notes, %3 subfolders")
                                    .arg(result.folderName)
                                    .arg(result.fileCount)
                                    .arg(result.subfolderCount));
  setInformationText(QString(), ScrollDialog::InformationLevel::Info);
  setButtonEnabled(QDialogButtonBox::Ok, true);
}

void ImportFolderDialog2::acceptedButtonClicked() {
  if (isCompleted()) {
    accept();
    return;
  }

  if (currentMode() == BundleMode) {
    importBundle();
  } else {
    importExternalFolder();
  }
}

void ImportFolderDialog2::importExternalFolder() {
  // Collect input.
  ImportFolderInput input;
  input.notebookId = m_parentId.notebookId;
  input.parentFolderPath = m_parentId.relativePath;
  input.sourceFolderPath = m_filterWidget->getFolderPath();
  input.suffixes = m_filterWidget->getSuffixes();

  // Delegate to controller.
  ImportFolderResult result = m_controller->importFolder(input);

  if (result.success) {
    m_newNodeId = result.nodeId;

    // Show warning if any, but still complete.
    if (!result.warningMessage.isEmpty()) {
      setInformationText(result.warningMessage, ScrollDialog::InformationLevel::Warning);
      completeButStay();
      return;
    }

    accept();
  } else {
    setInformationText(result.errorMessage, ScrollDialog::InformationLevel::Error);
  }
}

void ImportFolderDialog2::importBundle() {
  // The progress dialog pumps the event loop, so a second Ok is genuinely
  // reachable. The controller refuses re-entry too; this keeps the UI quiet.
  if (m_controller->isBusy()) {
    return;
  }

  ImportBundleInput input;
  input.notebookId = m_parentId.notebookId;
  input.parentFolderPath = m_parentId.relativePath;
  input.bundlePath = m_bundleInput ? m_bundleInput->text().trimmed() : QString();

  // LIFETIME: the progress dialog is stack-local and deliberately UNPARENTED.
  // The import is synchronous but pumps the event loop, so a parented dialog
  // could be destroyed twice (once by the parent, once by this scope). The
  // QPointer then covers the "do not touch the dialog afterwards" case.
  QProgressDialog progress(tr("Preparing…"), tr("Cancel"), 0, 100, nullptr);
  progress.setWindowTitle(tr("Import Folder"));
  // Application-modal without a parent: modality is what stops the user from
  // mutating the notebook mid-import.
  progress.setWindowModality(Qt::ApplicationModal);
  progress.setAutoClose(false);
  progress.setAutoReset(false);
  // Show immediately: even a small bundle does a full hash-verify pass, and a
  // silent freeze is worse than a dialog that flashes.
  progress.setMinimumDuration(0);
  progress.setValue(0);

  QPointer<ImportFolderDialog2> selfGuard(this);
  ImportFolderController::Callbacks callbacks;
  callbacks.m_labelChanged = [&progress](const QString &p_label) {
    progress.setLabelText(p_label);
  };
  callbacks.m_progress = [&progress](qint64 p_done, qint64 p_total) {
    if (p_total <= 0) {
      return;
    }
    progress.setValue(static_cast<int>((p_done * 100) / p_total));
  };
  // Losing the dialog underneath us counts as a cancellation, so the importer
  // unwinds and writes nothing rather than finishing into a dead UI.
  callbacks.m_isCancelled = [&progress, selfGuard]() {
    return !selfGuard || progress.wasCanceled();
  };

  const ImportFolderResult result = m_controller->importBundle(input, callbacks);
  progress.reset();
  progress.close();

  if (!selfGuard) {
    return; // Destroyed by a nested event; nothing left to report to.
  }

  if (result.success) {
    m_newNodeId = result.nodeId;
    accept();
    return;
  }

  setInformationText(result.errorMessage, ScrollDialog::InformationLevel::Error);
}

NodeIdentifier ImportFolderDialog2::getNewNodeId() const { return m_newNodeId; }
