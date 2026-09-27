#include "opennotebookdialog2.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <controllers/opennotebookcontroller.h>
#include <core/servicelocator.h>
#include <utils/pathutils.h>

#include "../locationinputwithbrowsebutton.h"
#include "../widgetsfactory.h"

using namespace vnotex;

namespace {

// QObject names that tests use to discover widgets via findChild<>().
const char *const kLocalModeRadioName = "localModeRadio";
const char *const kRemoteModeRadioName = "remoteModeRadio";
const char *const kModeStackName = "modeStack";
const char *const kLocalRootInputName = "localRootInput";
const char *const kRemoteUrlEditName = "remoteUrlEdit";
const char *const kRemotePatEditName = "remotePatEdit";
const char *const kGitUsernameEditName = "gitUsernameEdit";
const char *const kBackendComboName = "syncBackendCombo";
const char *const kWebdavUsernameEditName = "webdavUsernameEdit";
const char *const kRemoteDestInputName = "remoteDestInput";
const char *const kProgressBarName = "openNotebookProgressBar";
const char *const kOpenButtonName = "openButton";
const char *const kCancelButtonName = "cancelButton";
const char *const kOpenV3ButtonName = "openV3NotebookButton";

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
  m_localModeRadio->setToolTip(tr("Open an existing VNote notebook from a local folder on disk"));
  m_localModeRadio->setChecked(true);

  m_remoteModeRadio = new QRadioButton(tr("Remote URL"), mainWidget);
  m_remoteModeRadio->setObjectName(QLatin1String(kRemoteModeRadioName));
  m_remoteModeRadio->setToolTip(
      tr("Download a VNote notebook from a Git remote or a WebDAV collection"));

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

    // Secondary action, leftmost in the row: ResetRole is the only role that
    // sorts ahead of BOTH Open and Cancel on every QDialogButtonBox platform
    // layout (ActionRole lands between them on Windows).
    //
    // NOTE: Dialog::setDialogButtonBox also routes ResetRole clicks to the
    // virtual resetButtonClicked(), which is a no-op in the base Dialog and is
    // NOT overridden here. If this dialog ever overrides resetButtonClicked(),
    // drop the explicit connect below or the handler will fire twice.
    m_openV3Button = box->addButton(tr("Open V3 Notebook"), QDialogButtonBox::ResetRole);
    m_openV3Button->setObjectName(QLatin1String(kOpenV3ButtonName));
    // Load-bearing: the Open button above is explicitly setDefault(true), and a
    // freshly added auto-default push button would contend for the Enter key.
    m_openV3Button->setAutoDefault(false);
    m_openV3Button->setToolTip(tr("Close this dialog and import a legacy VNote3 notebook"));
    connect(m_openV3Button, &QPushButton::clicked, this,
            &OpenNotebookDialog2::onOpenV3NotebookClicked);
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
      tr("Root folder of an existing VNote notebook (must contain a valid notebook config)"));
  layout->addRow(tr("Root folder path"), m_localRootInput);

  connect(m_localRootInput, &LocationInputWithBrowseButton::textChanged, this,
          &OpenNotebookDialog2::onLocalRootChanged);
}

void OpenNotebookDialog2::setupRemotePage(QWidget *p_page) {
  auto *layout = new QFormLayout(p_page);

  m_backendCombo = WidgetsFactory::createComboBox(p_page);
  m_backendCombo->setObjectName(QLatin1String(kBackendComboName));
  m_backendCombo->addItem(tr("Git"), QStringLiteral("git"));
  m_backendCombo->addItem(tr("WebDAV"), QStringLiteral("webdav"));
  layout->addRow(tr("Sync method"), m_backendCombo);

  // Remote URL field.
  m_remoteUrlEdit = new QLineEdit(p_page);
  m_remoteUrlEdit->setObjectName(QLatin1String(kRemoteUrlEditName));
  m_remoteUrlEdit->setPlaceholderText(
      tr("https://github.com/user/repo.git  or  file:///path/to/repo.git"));
  m_remoteUrlEdit->setToolTip(tr("Remote git URL. Only HTTPS and file:// schemes are supported"));
  m_remoteUrlLabel = new QLabel(tr("Remote URL"), p_page);
  layout->addRow(m_remoteUrlLabel, m_remoteUrlEdit);

  m_remoteUsernameEdit = WidgetsFactory::createUrlUserNameEdit(m_remoteUrlEdit, p_page);
  m_remoteUsernameEdit->setObjectName(QLatin1String(kGitUsernameEditName));
  m_remoteUsernameEdit->setPlaceholderText(tr("Required by Gitee; optional for GitHub"));
  m_remoteUsernameEdit->setToolTip(
      tr("Login of the Personal Access Token owner, not necessarily the repository owner"));
  m_gitUsernameLabel = new QLabel(tr("Git username"), p_page);
  layout->addRow(m_gitUsernameLabel, m_remoteUsernameEdit);

  m_webdavUsernameEdit = WidgetsFactory::createLineEdit(p_page);
  m_webdavUsernameEdit->setObjectName(QLatin1String(kWebdavUsernameEditName));
  m_webdavUsernameEdit->setPlaceholderText(tr("Optional for anonymous download"));
  m_webdavUsernameEdit->setToolTip(
      tr("WebDAV account username; leave both credentials empty for anonymous download"));
  m_webdavUsernameLabel = new QLabel(tr("Username"), p_page);
  layout->addRow(m_webdavUsernameLabel, m_webdavUsernameEdit);

  // PAT field (password echo). "(optional)" hint lives in the placeholder so
  // the label stays compact; see plan refine-open-notebook-dialog.
  m_remotePatEdit = new QLineEdit(p_page);
  m_remotePatEdit->setObjectName(QLatin1String(kRemotePatEditName));
  m_remotePatEdit->setEchoMode(QLineEdit::Password);
  m_remotePatEdit->setPlaceholderText(tr("Optional — leave empty to open without syncing yet"));
  m_remotePatEdit->setToolTip(
      tr("If empty, the notebook opens normally (fully editable) with sync configured but "
         "inactive. Add a token later to start syncing"));
  m_secretLabel = new QLabel(tr("Personal Access Token"), p_page);
  layout->addRow(m_secretLabel, m_remotePatEdit);

  // The owned staging directory is renamed into a destination that must not exist.
  m_remoteDestInput = new LocationInputWithBrowseButton(p_page);
  m_remoteDestInput->setObjectName(QLatin1String(kRemoteDestInputName));
  m_remoteDestInput->setBrowseType(LocationInputWithBrowseButton::Folder,
                                   tr("Select Local Root Folder"));
  m_remoteDestInput->setPlaceholderText(tr("Folder to download into (must not exist)"));
  m_remoteDestInput->setToolTip(
      tr("New local folder that will receive the notebook; the parent folder must already exist"));
  layout->addRow(tr("Local root folder"), m_remoteDestInput);

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
  connect(m_webdavUsernameEdit, &QLineEdit::textChanged, this,
          &OpenNotebookDialog2::onRemoteFieldsChanged);
  connect(m_backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
    m_remotePatEdit->clear();
    m_webdavUsernameEdit->clear();
    refreshBackendFields();
    onRemoteFieldsChanged();
  });
  refreshBackendFields();
}

SyncSettings OpenNotebookDialog2::enteredSettings() const {
  SyncSettings settings;
  settings.m_backend = m_backendCombo->currentData().toString();
  settings.m_remoteUrl = m_remoteUrlEdit->text().trimmed();
  settings.m_credentials.m_backend = settings.m_backend;
  settings.m_credentials.m_secret = m_remotePatEdit->text();
  if (settings.m_backend == QLatin1String("webdav")) {
    settings.m_credentials.m_username = m_webdavUsernameEdit->text();
  }
  return settings;
}

void OpenNotebookDialog2::refreshBackendFields() {
  const bool webdav = m_backendCombo->currentData().toString() == QLatin1String("webdav");
  m_remoteUrlLabel->setText(webdav ? tr("Collection URL") : tr("Remote URL"));
  m_remoteUrlEdit->setPlaceholderText(
      webdav ? tr("https://example.com/dav/notebook/")
             : tr("https://github.com/user/repo.git  or  file:///path/to/repo.git"));
  m_remoteUrlEdit->setToolTip(
      webdav ? tr("Existing HTTPS collection containing one VNote notebook")
             : tr("Remote git URL. Only HTTPS and file:// schemes are supported"));
  m_secretLabel->setText(webdav ? tr("Password or app password") : tr("Personal Access Token"));
  m_remotePatEdit->setToolTip(
      tr("Leave credentials empty to download anonymously if the server permits it. "
         "The notebook remains editable; add credentials later to start syncing"));
  m_gitUsernameLabel->setVisible(!webdav);
  m_remoteUsernameEdit->setVisible(!webdav);
  m_webdavUsernameLabel->setVisible(webdav);
  m_webdavUsernameEdit->setVisible(webdav);
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
  if (m_cloneInProgress) {
    return;
  }
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

  const auto settings = enteredSettings();
  const bool suppliedWebdavCredentials =
      settings.m_backend == QLatin1String("webdav") &&
      (!settings.m_credentials.m_username.isEmpty() || !settings.m_credentials.m_secret.isEmpty());
  result.message = validateSyncSettings(settings, suppliedWebdavCredentials);
  if (!result.message.isEmpty()) {
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
  if (destInfo.exists() || destInfo.isSymLink()) {
    result.message = tr("Local root folder must not already exist.");
    result.surfaceInBanner = true;
    return result;
  }
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

void OpenNotebookDialog2::onOpenV3NotebookClicked() {
  // Defensive: the button is disabled while a clone is running, but never
  // abandon an in-flight clone by closing the dialog under it.
  if (m_cloneInProgress) {
    return;
  }
  // done() (not reject()) so the caller can distinguish "user cancelled" from
  // "user wants the V3 flow". No notebookOpened is emitted on this path.
  done(OpenV3NotebookRequested);
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
  emit notebookOpened(m_openedNotebookId, false);
  accept();
}

void OpenNotebookDialog2::handleRemoteOpen() {
  if (m_cloneInProgress) {
    return;
  }
  const auto validation = validateRemoteInputs();
  if (!validation.valid) {
    setInformationText(validation.message, InformationLevel::Error);
    return;
  }
  CloneAndOpenInput input;
  input.syncSettings = enteredSettings();
  input.finalDestDir = m_remoteDestInput ? m_remoteDestInput->text().trimmed() : QString();
  // autoSyncEnabled keeps the controller input's default.

  // Disable inputs so the user cannot mutate them mid-clone. The Cancel
  // button stays ENABLED so the user can abort the in-flight clone (the
  // rejectedButtonClicked override branches on m_cloneInProgress).
  setButtonEnabled(QDialogButtonBox::Open, false);
  if (m_localModeRadio)
    m_localModeRadio->setEnabled(false);
  if (m_remoteModeRadio)
    m_remoteModeRadio->setEnabled(false);
  if (m_openV3Button)
    m_openV3Button->setEnabled(false);
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
  m_backendCombo->setEnabled(p_enabled);
  m_webdavUsernameEdit->setEnabled(p_enabled);
  if (m_remoteUrlEdit)
    m_remoteUrlEdit->setEnabled(p_enabled);
  if (m_remotePatEdit)
    m_remotePatEdit->setEnabled(p_enabled);
  if (m_remoteUsernameEdit)
    m_remoteUsernameEdit->setEnabled(p_enabled &&
                                     m_remoteUrlEdit->text().startsWith(QLatin1String("https://")));
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
    emit notebookOpened(m_openedNotebookId, p_result.partialSyncMissingCredentials);
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
  if (m_openV3Button)
    m_openV3Button->setEnabled(true);
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
