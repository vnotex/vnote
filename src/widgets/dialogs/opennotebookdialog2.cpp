#include "opennotebookdialog2.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <controllers/opennotebookcontroller.h>
#include <core/servicelocator.h>
#include <utils/pathutils.h>

#include "../locationinputwithbrowsebutton.h"

using namespace vnotex;

namespace {

// QObject names that tests use to discover widgets via findChild<>().
const char *const kLocalModeRadioName = "localModeRadio";
const char *const kRemoteModeRadioName = "remoteModeRadio";
const char *const kModeStackName = "modeStack";
const char *const kLocalRootInputName = "localRootInput";
const char *const kRemoteUrlEditName = "remoteUrlEdit";
const char *const kRemotePatEditName = "remotePatEdit";
const char *const kRemoteDestInputName = "remoteDestInput";
const char *const kProgressBarName = "openNotebookProgressBar";
const char *const kOpenButtonName = "openButton";
const char *const kCancelButtonName = "cancelButton";

// Single source of truth for the URL-scheme guard. T22's
// OpenNotebookController::validateCloneInput() MUST mirror this regex so the
// dialog and controller agree on what is acceptable. Accepts:
//   * https://...
//   * file:///...   (note: three slashes; covers Windows/POSIX file URLs)
// Rejects everything else (ssh, http, git, scp-like, bare paths, ...).
const char *const kRemoteUrlSchemeRegex = "^(https://|file:///)\\S+$";

} // namespace

OpenNotebookDialog2::OpenNotebookDialog2(ServiceLocator &p_services, QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services) {
  m_controller = new OpenNotebookController(m_services, this);

  setupUI();

  // Application-modal (NOT system-modal). exec() defaults to this, but be
  // explicit per the project convention for modal dialogs.
  setWindowModality(Qt::ApplicationModal);

  // openurl-followups Item 2: wire controller signals so remote-mode clone
  // completion and progress updates land in this dialog. Both signals carry
  // primitive / QObject-safe payloads (the result struct is a Q_DECLARE_METATYPE
  // value type registered by the controller's ctor) so default Qt::AutoConnection
  // is fine — the controller emits via QueuedConnection from its worker tail.
  connect(m_controller, &OpenNotebookController::cloneFinished, this,
          &OpenNotebookDialog2::onCloneFinished);
  connect(m_controller, &OpenNotebookController::cloneProgressUpdated, this,
          &OpenNotebookDialog2::onCloneProgressUpdated);

  // Initialize button + validation state.
  updateOpenButtonState();
}

OpenNotebookDialog2::~OpenNotebookDialog2() = default;

void OpenNotebookDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *mainLayout = new QVBoxLayout(mainWidget);

  // Top: mode selector radios.
  auto *modeRow = new QHBoxLayout();
  m_localModeRadio = new QRadioButton(tr("Local folder"), mainWidget);
  m_localModeRadio->setObjectName(QLatin1String(kLocalModeRadioName));
  m_localModeRadio->setToolTip(tr("Open an existing VNote notebook from a local folder on disk."));
  m_localModeRadio->setChecked(true);

  m_remoteModeRadio = new QRadioButton(tr("Remote URL"), mainWidget);
  m_remoteModeRadio->setObjectName(QLatin1String(kRemoteModeRadioName));
  m_remoteModeRadio->setToolTip(
      tr("Clone a VNote notebook from a remote git URL or a file:// path."));

  m_modeGroup = new QButtonGroup(this);
  m_modeGroup->setExclusive(true);
  m_modeGroup->addButton(m_localModeRadio, static_cast<int>(LocalMode));
  m_modeGroup->addButton(m_remoteModeRadio, static_cast<int>(RemoteMode));

  modeRow->addWidget(m_localModeRadio);
  modeRow->addWidget(m_remoteModeRadio);
  modeRow->addStretch();
  mainLayout->addLayout(modeRow);

  // Middle: stacked mode-specific area.
  m_modeStack = new QStackedWidget(mainWidget);
  m_modeStack->setObjectName(QLatin1String(kModeStackName));

  m_localPage = new QWidget(m_modeStack);
  setupLocalPage(m_localPage);
  m_modeStack->addWidget(m_localPage);

  m_remotePage = new QWidget(m_modeStack);
  setupRemotePage(m_remotePage);
  m_modeStack->addWidget(m_remotePage);

  m_modeStack->setCurrentIndex(static_cast<int>(LocalMode));
  mainLayout->addWidget(m_modeStack);

  // Bottom: progress bar (hidden by default; shown during a remote clone).
  m_progressBar = new QProgressBar(mainWidget);
  m_progressBar->setObjectName(QLatin1String(kProgressBarName));
  m_progressBar->setRange(0, 0); // indeterminate by default
  m_progressBar->hide();
  mainLayout->addWidget(m_progressBar);

  setCentralWidget(mainWidget);

  // Dialog buttons: Open (accept role) + Cancel (reject role).
  setDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
  if (auto *box = getDialogButtonBox()) {
    if (auto *openBtn = box->button(QDialogButtonBox::Open)) {
      openBtn->setObjectName(QLatin1String(kOpenButtonName));
      openBtn->setText(tr("Open"));
      openBtn->setDefault(true);
    }
    if (auto *cancelBtn = box->button(QDialogButtonBox::Cancel)) {
      cancelBtn->setObjectName(QLatin1String(kCancelButtonName));
      // openurl-followups Item 2: cache the Cancel button so
      // rejectedButtonClicked() can flip its enabled state during
      // cancellation. The base Dialog wiring connects the button's
      // clicked signal to rejectedButtonClicked via QDialogButtonBox's
      // rejected() signal, so overriding rejectedButtonClicked() is
      // sufficient — no extra connect needed here.
      m_cancelButton = cancelBtn;
    }
  }
  setButtonEnabled(QDialogButtonBox::Open, false);

  setWindowTitle(tr("Open Notebook"));

  // Wire mode toggle.
  connect(m_localModeRadio, &QRadioButton::toggled, this, &OpenNotebookDialog2::onModeChanged);
  connect(m_remoteModeRadio, &QRadioButton::toggled, this, &OpenNotebookDialog2::onModeChanged);
}

void OpenNotebookDialog2::setupLocalPage(QWidget *p_page) {
  auto *layout = new QFormLayout(p_page);

  m_localRootInput = new LocationInputWithBrowseButton(p_page);
  m_localRootInput->setObjectName(QLatin1String(kLocalRootInputName));
  m_localRootInput->setBrowseType(LocationInputWithBrowseButton::Folder,
                                  tr("Select Notebook Root Folder"));
  m_localRootInput->setPlaceholderText(tr("Select the root folder of an existing VNote notebook"));
  m_localRootInput->setToolTip(
      tr("Root folder of an existing VNote notebook (must contain a valid notebook config)."));
  layout->addRow(tr("Root folder path:"), m_localRootInput);

  connect(m_localRootInput, &LocationInputWithBrowseButton::textChanged, this,
          &OpenNotebookDialog2::onLocalRootChanged);
}

void OpenNotebookDialog2::setupRemotePage(QWidget *p_page) {
  auto *layout = new QFormLayout(p_page);

  // Remote URL field.
  m_remoteUrlEdit = new QLineEdit(p_page);
  m_remoteUrlEdit->setObjectName(QLatin1String(kRemoteUrlEditName));
  m_remoteUrlEdit->setPlaceholderText(
      tr("https://github.com/user/repo.git  or  file:///path/to/repo.git"));
  m_remoteUrlEdit->setToolTip(tr("Remote git URL. Only HTTPS and file:// schemes are supported."));
  layout->addRow(tr("Remote URL:"), m_remoteUrlEdit);

  // PAT field (password echo). "(optional)" hint lives in the placeholder so
  // the label stays compact; see plan refine-open-notebook-dialog.
  m_remotePatEdit = new QLineEdit(p_page);
  m_remotePatEdit->setObjectName(QLatin1String(kRemotePatEditName));
  m_remotePatEdit->setEchoMode(QLineEdit::Password);
  m_remotePatEdit->setPlaceholderText(tr("Optional (empty to open as read-only)"));
  m_remotePatEdit->setToolTip(
      tr("If empty, the notebook opens read-only (you cannot edit, only view)."));
  layout->addRow(tr("Personal Access Token:"), m_remotePatEdit);

  // Local root folder: project-standard LocationInputWithBrowseButton. Must
  // either not exist (will be created during clone) or be an existing empty
  // directory. Validation enforces the contract; see validateRemoteInputs()
  // and OpenNotebookController::validateCloneInput().
  m_remoteDestInput = new LocationInputWithBrowseButton(p_page);
  m_remoteDestInput->setObjectName(QLatin1String(kRemoteDestInputName));
  m_remoteDestInput->setBrowseType(LocationInputWithBrowseButton::Folder,
                                   tr("Select Local Root Folder"));
  m_remoteDestInput->setPlaceholderText(tr("Folder to clone into (must not exist or be empty)"));
  m_remoteDestInput->setToolTip(
      tr("Local folder that will receive the cloned notebook. It must either not exist yet "
         "(it will be created) or be an existing empty directory."));
  layout->addRow(tr("Local root folder:"), m_remoteDestInput);

  // Wire field-change validation. Per plan refine-open-notebook-dialog the
  // dialog deliberately suppresses banner updates on URL/PAT changes; only
  // Local-root-folder changes can surface a banner message. The wiring is
  // identical across the three fields — the per-field gating lives in
  // updateOpenButtonState() via the RemoteValidation::surfaceInBanner flag.
  connect(m_remoteUrlEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { onRemoteFieldsChanged(); });
  connect(m_remotePatEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { onRemoteFieldsChanged(); });
  connect(m_remoteDestInput, &LocationInputWithBrowseButton::textChanged, this,
          [this](const QString &) { onRemoteFieldsChanged(); });
}

OpenNotebookDialog2::Mode OpenNotebookDialog2::currentMode() const {
  return m_remoteModeRadio && m_remoteModeRadio->isChecked() ? RemoteMode : LocalMode;
}

void OpenNotebookDialog2::onModeChanged() {
  const Mode mode = currentMode();
  m_modeStack->setCurrentIndex(static_cast<int>(mode));
  // Clear stale validation text from the prior mode.
  setInformationText(QString(), InformationLevel::Info);
  updateOpenButtonState();
}

void OpenNotebookDialog2::onLocalRootChanged() {
  if (currentMode() != LocalMode) {
    return;
  }
  updateOpenButtonState();
}

void OpenNotebookDialog2::onRemoteFieldsChanged() {
  if (currentMode() != RemoteMode) {
    return;
  }
  updateOpenButtonState();
}

void OpenNotebookDialog2::updateOpenButtonState() {
  if (currentMode() == LocalMode) {
    const QString path = m_localRootInput ? m_localRootInput->text().trimmed() : QString();
    if (path.isEmpty()) {
      setInformationText(QString(), InformationLevel::Info);
      setButtonEnabled(QDialogButtonBox::Open, false);
      return;
    }
    OpenNotebookValidationResult validation = m_controller->validateRootFolder(path);
    if (!validation.valid) {
      setInformationText(validation.message, InformationLevel::Error);
      setButtonEnabled(QDialogButtonBox::Open, false);
      return;
    }
    setInformationText(QString(), InformationLevel::Info);
    setButtonEnabled(QDialogButtonBox::Open, true);
    return;
  }

  // Remote mode. Per plan refine-open-notebook-dialog, URL / PAT errors
  // disable Open SILENTLY (no banner update) so the dialog stays quiet and
  // stable in size while the user is typing. Only Local-root-folder errors
  // surface in the banner (via RemoteValidation::surfaceInBanner).
  const RemoteValidation remote = validateRemoteInputs();
  if (!remote.valid) {
    setButtonEnabled(QDialogButtonBox::Open, false);
    if (remote.surfaceInBanner) {
      setInformationText(remote.message, InformationLevel::Error);
    } else {
      setInformationText(QString(), InformationLevel::Info);
    }
    return;
  }
  setInformationText(QString(), InformationLevel::Info);
  setButtonEnabled(QDialogButtonBox::Open, true);
}

OpenNotebookDialog2::RemoteValidation OpenNotebookDialog2::validateRemoteInputs() const {
  RemoteValidation result;

  // URL: empty -> silent invalid; bad scheme -> silent invalid (banner stays
  // quiet per the "no banner on URL change" mandate). Open just disables.
  const QString url = m_remoteUrlEdit ? m_remoteUrlEdit->text().trimmed() : QString();
  if (url.isEmpty()) {
    return result;
  }
  static const QRegularExpression scheme(QString::fromLatin1(kRemoteUrlSchemeRegex));
  if (!scheme.match(url).hasMatch()) {
    result.message = tr("Remote URL must use HTTPS or file:// scheme (got: %1).").arg(url);
    // surfaceInBanner stays false: URL errors are silent.
    return result;
  }

  // Local root folder: empty -> silent invalid; populated-but-invalid ->
  // surfaceInBanner=true so the user gets actionable feedback.
  const QString dest = m_remoteDestInput ? m_remoteDestInput->text().trimmed() : QString();
  if (dest.isEmpty()) {
    return result;
  }
  if (!PathUtils::isLegalPath(dest)) {
    result.message = tr("Local root folder path is not valid.");
    result.surfaceInBanner = true;
    return result;
  }
  const QFileInfo destInfo(dest);
  if (destInfo.exists()) {
    if (!destInfo.isDir()) {
      result.message = tr("Local root folder must be a directory.");
      result.surfaceInBanner = true;
      return result;
    }
    // Must be empty (no visible OR hidden/system entries) so the worker's
    // pre-rename rmdir hop is safe and the atomic rename can proceed.
    const QDir destDir(dest);
    const QStringList entries =
        destDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    if (!entries.isEmpty()) {
      result.message =
          tr("Local root folder must be empty (contains %1 item(s)).").arg(entries.size());
      result.surfaceInBanner = true;
      return result;
    }
  } else {
    const QFileInfo parentInfo(destInfo.absolutePath());
    if (!parentInfo.exists() || !parentInfo.isDir()) {
      result.message = tr("Parent folder does not exist.");
      result.surfaceInBanner = true;
      return result;
    }
    if (!parentInfo.isWritable()) {
      result.message = tr("Parent folder is not writable.");
      result.surfaceInBanner = true;
      return result;
    }
  }

  result.valid = true;
  return result;
}

void OpenNotebookDialog2::acceptedButtonClicked() {
  if (currentMode() == LocalMode) {
    handleLocalOpen();
  } else {
    handleRemoteOpen();
  }
}

void OpenNotebookDialog2::rejectedButtonClicked() {
  // openurl-followups Item 2: while a remote clone is in flight, Cancel
  // requests an abort via the controller rather than closing the dialog.
  // The controller cancels the cancellation token; the worker observes
  // VXCORE_ERR_CANCELLED and fires onCloneFinished with a "cancelled by
  // user" message, where we re-enable inputs and let the user retry or
  // dismiss the dialog.
  if (m_cloneInProgress) {
    m_controller->cancelClone();
    if (m_cancelButton) {
      // Prevent double-cancel: until cloneFinished arrives, the user
      // cannot click Cancel again. The button re-enables in
      // onCloneFinished regardless of outcome.
      m_cancelButton->setEnabled(false);
    }
    setInformationText(tr("Cancelling clone..."), InformationLevel::Info);
    return;
  }
  // No clone in flight: fall through to the base Dialog::reject() behavior.
  ScrollDialog::rejectedButtonClicked();
}

void OpenNotebookDialog2::handleLocalOpen() {
  OpenNotebookInput input;
  input.rootFolderPath = m_localRootInput->text().trimmed();

  OpenNotebookResult result = m_controller->openNotebook(input);
  if (!result.success) {
    setInformationText(result.errorMessage, InformationLevel::Error);
    return;
  }

  m_openedNotebookId = result.notebookId;
  emit notebookOpened(m_openedNotebookId);
  accept();
}

void OpenNotebookDialog2::handleRemoteOpen() {
  // openurl-followups Item 2: drive the controller's async clone+open path.
  // The previous T24 stub displayed a QMessageBox::information with a
  // "wired in T25" message; T25 only wired the explorer button to spawn
  // this dialog, NOT the dialog's internal remote-mode handler. This is
  // the actual wiring.
  CloneAndOpenInput input;
  input.remoteUrl = m_remoteUrlEdit ? m_remoteUrlEdit->text().trimmed() : QString();
  input.pat = m_remotePatEdit ? m_remotePatEdit->text() : QString();
  input.finalDestDir = m_remoteDestInput ? m_remoteDestInput->text().trimmed() : QString();
  // Backend / intervalSeconds keep CloneAndOpenInput's defaults ("git", 60).

  // Disable inputs so the user cannot mutate them mid-clone. The Cancel
  // button stays ENABLED so the user can abort the in-flight clone (the
  // rejectedButtonClicked override branches on m_cloneInProgress).
  setButtonEnabled(QDialogButtonBox::Open, false);
  if (m_localModeRadio)
    m_localModeRadio->setEnabled(false);
  if (m_remoteModeRadio)
    m_remoteModeRadio->setEnabled(false);
  setRemoteInputsEnabled(false);

  // Show indeterminate progress bar; the controller's coarse progress
  // callback (cloneProgressUpdated) will update the info text only — the
  // bar stays indeterminate because libgit2's clone has no usable progress
  // numerator/denominator for our use case.
  if (m_progressBar) {
    m_progressBar->setRange(0, 0);
    m_progressBar->show();
  }
  setInformationText(tr("Cloning..."), InformationLevel::Info);

  m_cloneInProgress = true;
  m_controller->cloneAndOpen(input);
}

void OpenNotebookDialog2::setRemoteInputsEnabled(bool p_enabled) {
  if (m_remoteUrlEdit)
    m_remoteUrlEdit->setEnabled(p_enabled);
  if (m_remotePatEdit)
    m_remotePatEdit->setEnabled(p_enabled);
  if (m_remoteDestInput)
    m_remoteDestInput->setEnabled(p_enabled);
}

void OpenNotebookDialog2::onCloneProgressUpdated(int p_current, int p_total,
                                                 const QString &p_phase) {
  // Coarse progress: the controller fires this once at the start ("Cloning...")
  // and may fire more in the future. The progress bar stays indeterminate;
  // only the info text changes.
  (void)p_current;
  (void)p_total;
  setInformationText(p_phase, InformationLevel::Info);
}

void OpenNotebookDialog2::onCloneFinished(const CloneAndOpenResult &p_result) {
  m_cloneInProgress = false;
  if (m_progressBar) {
    m_progressBar->hide();
  }
  // Re-enable Cancel button regardless of outcome — even on success the
  // dialog will accept() and close so this is harmless; on cancel/failure
  // the user needs Cancel available again to dismiss the dialog.
  if (m_cancelButton) {
    m_cancelButton->setEnabled(true);
  }

  if (p_result.success) {
    m_openedNotebookId = p_result.notebookId;
    emit notebookOpened(m_openedNotebookId);
    accept();
    return;
  }

  // Failure path: re-enable mode radios + remote inputs + Open button so the
  // user can retry. Distinguish "cancelled by user" from a generic error:
  // cancelled shows a neutral Info banner; everything else shows an Error
  // banner. Per the brief, NO error modal — inline feedback only.
  if (m_localModeRadio)
    m_localModeRadio->setEnabled(true);
  if (m_remoteModeRadio)
    m_remoteModeRadio->setEnabled(true);
  setRemoteInputsEnabled(true);
  // Restore Open button state via the standard validation path so it
  // re-enables only when the current inputs are valid.
  updateOpenButtonState();

  if (p_result.errorMessage.contains(QStringLiteral("cancel"), Qt::CaseInsensitive)) {
    setInformationText(tr("Clone cancelled."), InformationLevel::Info);
  } else {
    setInformationText(p_result.errorMessage, InformationLevel::Error);
  }
}

QString OpenNotebookDialog2::getOpenedNotebookId() const { return m_openedNotebookId; }
