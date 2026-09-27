#include "notebooksyncinfodialog2.h"

#include <memory>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#include <controllers/notebooksyncinfocontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/syncerrorpresenter.h>
#include <core/services/synclog.h>
#include <core/services/syncservice.h>
#include <utils/widgetutils.h>

#include "../inlinebanner.h"
#include "../propertydefs.h"
#include "../widgetsfactory.h"

using namespace vnotex;

namespace {

// QObject names that tests use to discover widgets via findChild<>(). MUST be
// kept in sync with the documentation in the dialog header.
const char *const kBackendComboName = "syncBackendCombo";
const char *const kWebdavUsernameEditName = "webdavUsernameEdit";
const char *const kWebdavHintName = "webdavHint";
const char *const kRemoteUrlEditName = "remoteUrlEdit";
const char *const kRemoteUrlHintLabelName = "remoteUrlHintLabel";
const char *const kPatEditName = "patEdit";
const char *const kGitUsernameEditName = "gitUsernameEdit";
const char *const kLastSyncLabelName = "lastSyncLabel";
const char *const kCurrentStateLabelName = "currentStateLabel";
const char *const kDisableSyncButtonName = "disableSyncButton";
const char *const kNotebookNameLabelName = "notebookNameLabel";
const char *const kApplyButtonName = "applyButton";
const char *const kOkButtonName = "okButton";
const char *const kResetButtonName = "resetButton";
const char *const kCancelButtonName = "cancelButton";
// T29: object name for the read-only banner label; consumed by
// test_notebook_sync_info_dialog2_readonly to assert visibility per RO state.
const char *const kReadOnlyBannerLabelName = "readOnlyBannerLabel";

const char *const kBootstrapModeProperty = "bootstrapMode";

} // namespace

NotebookSyncInfoDialog2::NotebookSyncInfoDialog2(ServiceLocator &p_services,
                                                 const QString &p_notebookId, QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services), m_notebookId(p_notebookId) {
  // T11 wired the real controller. Owned by this dialog (parented to it).
  m_controller = new NotebookSyncInfoController(m_services, m_notebookId, this);

  setupUI();

  connectSyncServiceSignals();

  // Initialize bootstrap-mode property to false explicitly so tests that read
  // the property always observe a defined value.
  setProperty(kBootstrapModeProperty, false);

  refreshDirtyButtons();

  // T29: query the notebook's read-only state once at open and reflect it in
  // the banner. Cached in m_isReadOnlyNotebook so the post-PAT-save modal
  // path agrees with the banner the user just saw.
  refreshReadOnlyBanner();

  // Populate read-only labels via the controller (authoritative source for
  // notebook display name + remote URL + formatted last-sync timestamp). The
  // controller emits dataLoaded(...); subscribe locally to update the labels
  // and snapshot the last-applied URL.
  if (m_controller) {
    connect(m_controller, &NotebookSyncInfoController::dataLoaded, this,
            [this](const QString &p_name, const QString &p_remoteUrl, const QString &p_lastSync) {
              m_syncEnabled = m_controller->syncEnabled();
              m_rawNotebook = m_controller->isRawNotebook();
              m_lastAppliedBackend = m_controller->backend();
              if (m_lastAppliedBackend.isEmpty()) {
                m_lastAppliedBackend = QStringLiteral("git");
              }
              setBackend(m_lastAppliedBackend);
              if (m_notebookNameLabel && !p_name.isEmpty()) {
                m_notebookNameLabel->setText(p_name);
              }
              if (m_remoteUrlEdit) {
                m_remoteUrlEdit->setText(p_remoteUrl);
              }
              m_lastAppliedRemoteUrl = p_remoteUrl;
              if (m_lastSyncLabel) {
                m_lastSyncLabel->setText(p_lastSync.isEmpty() ? tr("Never") : p_lastSync);
              }
              refreshDirtyButtons();
            });
    // B1: confirm destructive URL change on a registered notebook.
    connect(m_controller, &NotebookSyncInfoController::confirmUrlChangeRequested, this,
            &NotebookSyncInfoDialog2::onConfirmUrlChange);
    // B2: surface controller errors to the user without closing the dialog.
    connect(m_controller, &NotebookSyncInfoController::error, this,
            &NotebookSyncInfoDialog2::onError);
    m_controller->loadInitialData();
  }

  if (isSupportedSyncBackend(m_backendCombo->currentData().toString()) && !m_rawNotebook) {
    setCurrentStateLabel(SyncStateLevel::Idle, tr("Idle"));
  }
}

// Pre-create mode collects settings without constructing a controller.
NotebookSyncInfoDialog2::NotebookSyncInfoDialog2(ServiceLocator &p_services, QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services), m_preCreateMode(true) {
  setupUI();

  // In pre-create mode, hide elements that don't apply (no notebook yet, no
  // sync history, no state to monitor).
  if (m_notebookNameLabel) {
    m_notebookNameLabel->hide();
  }
  if (m_lastSyncLabel) {
    m_lastSyncLabel->hide();
  }
  if (m_currentStateLabel) {
    m_currentStateLabel->hide();
  }
  if (m_disableSyncButton) {
    m_disableSyncButton->hide();
  }

  // Hide Apply/Reset buttons (not applicable in pre-create mode).
  auto *box = getDialogButtonBox();
  if (box) {
    if (auto *applyBtn = box->button(QDialogButtonBox::Apply)) {
      applyBtn->setVisible(false);
    }
    if (auto *resetBtn = box->button(QDialogButtonBox::Reset)) {
      resetBtn->setVisible(false);
    }
  }

  // Set window title for clarity.
  setWindowTitle(tr("Configure Sync"));

  refreshBackendFields();
}

void NotebookSyncInfoDialog2::setupUI() {
  auto *centralWidget = new QWidget(this);
  auto *formLayout = new QFormLayout(centralWidget);

  // T29: Read-only banner shown at the top of the dialog when the notebook
  // is read-only. Constructed hidden; refreshReadOnlyBanner toggles visibility
  // post-setup once the notebook's RO state has been queried via
  // NotebookCoreService::isNotebookReadOnly (T21).
  m_readOnlyBanner =
      new InlineBanner(InlineBanner::Severity::Warning,
                       tr("This notebook is currently open in read-only mode. To enable editing, "
                          "close this notebook and re-open it from the remote URL with valid "
                          "credentials. Credentials added here will be saved, but editing "
                          "will only become available after closing and re-opening the notebook."),
                       centralWidget);
  m_readOnlyBanner->setObjectName(QString::fromLatin1(kReadOnlyBannerLabelName));
  m_readOnlyBanner->hide();
  formLayout->addRow(m_readOnlyBanner);

  // 1. Notebook name (read-only).
  m_notebookNameLabel = new QLabel(centralWidget);
  m_notebookNameLabel->setObjectName(QString::fromLatin1(kNotebookNameLabelName));
  m_notebookNameLabel->setText(m_notebookId);
  m_notebookNameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  formLayout->addRow(tr("Notebook"), m_notebookNameLabel);

  m_backendCombo = WidgetsFactory::createComboBox(centralWidget);
  m_backendCombo->setObjectName(QLatin1String(kBackendComboName));
  m_backendCombo->addItem(tr("Git"), QStringLiteral("git"));
  m_backendCombo->addItem(tr("WebDAV"), QStringLiteral("webdav"));
  m_backendCombo->setToolTip(tr("Disable sync before switching the active backend"));
  formLayout->addRow(tr("Sync method"), m_backendCombo);

  // 2. Remote URL (editable).
  m_remoteUrlEdit = WidgetsFactory::createLineEdit(centralWidget);
  m_remoteUrlEdit->setObjectName(QString::fromLatin1(kRemoteUrlEditName));
  m_remoteUrlEdit->setPlaceholderText(tr("https://github.com/example/notes.git"));
  m_remoteUrlEdit->setToolTip(tr("Remote git repository URL used for syncing this notebook"));
  m_remoteUrlLabel = new QLabel(tr("Remote URL"), centralWidget);
  formLayout->addRow(m_remoteUrlLabel, m_remoteUrlEdit);
  connect(m_remoteUrlEdit, &QLineEdit::textChanged, this, &NotebookSyncInfoDialog2::onFieldEdited);

  // 2b. Remote URL prerequisite hint (visible in both legacy and pre-create
  // modes). The bootstrap/git_sync backend requires the remote repository to
  // already exist on the Git host; a missing repo returns libgit2 404 and the
  // failed-bootstrap rollback can leave inconsistent local state. The hint
  // makes this prerequisite discoverable in the UI.
  m_remoteUrlHintLabel = new QLabel(centralWidget);
  m_remoteUrlHintLabel->setObjectName(QString::fromLatin1(kRemoteUrlHintLabelName));
  m_remoteUrlHintLabel->setText(
      tr("The remote repository must already exist. Create an empty repo on your Git host first."));
  m_remoteUrlHintLabel->setWordWrap(true);
  // Muted hint text. NOT setEnabled(false): that would advertise
  // QAccessible::State::unavailable for what is ordinary instructional text,
  // and the themes style QLabel unconditionally with no :disabled variant, so
  // it would not even change the color. Italics come from the font, so no
  // color literal is needed anywhere.
  WidgetUtils::setPropertyDynamically(m_remoteUrlHintLabel, PropertyDefs::c_mutedText, true);
  {
    QFont hintFont = m_remoteUrlHintLabel->font();
    hintFont.setItalic(true);
    m_remoteUrlHintLabel->setFont(hintFont);
  }
  formLayout->addRow(QString(), m_remoteUrlHintLabel);

  m_webdavHint = new InlineBanner(
      InlineBanner::Severity::Info,
      tr("Use a dedicated existing HTTPS collection. Notes remain ordinary files. "
         "Safe conditional writes are required; empty remote folders may be retained."),
      centralWidget);
  m_webdavHint->setObjectName(QLatin1String(kWebdavHintName));
  formLayout->addRow(m_webdavHint);

  m_gitUsernameEdit = WidgetsFactory::createUrlUserNameEdit(m_remoteUrlEdit, centralWidget);
  m_gitUsernameEdit->setObjectName(QLatin1String(kGitUsernameEditName));
  m_gitUsernameEdit->setPlaceholderText(tr("Required by Gitee; optional for GitHub"));
  m_gitUsernameEdit->setToolTip(
      tr("Login of the Personal Access Token owner, not necessarily the repository owner"));
  m_gitUsernameLabel = new QLabel(tr("Git user name"), centralWidget);
  formLayout->addRow(m_gitUsernameLabel, m_gitUsernameEdit);

  m_webdavUsernameEdit = WidgetsFactory::createLineEdit(centralWidget);
  m_webdavUsernameEdit->setObjectName(QLatin1String(kWebdavUsernameEditName));
  m_webdavUsernameEdit->setToolTip(
      tr("Changing the username requires a new password or app password"));
  m_webdavUsernameLabel = new QLabel(tr("Username"), centralWidget);
  formLayout->addRow(m_webdavUsernameLabel, m_webdavUsernameEdit);
  connect(m_webdavUsernameEdit, &QLineEdit::textChanged, this,
          &NotebookSyncInfoDialog2::onFieldEdited);

  // 3. PAT (editable, password-masked, NEVER prefilled).
  m_patEdit = WidgetsFactory::createLineEdit(centralWidget);
  m_patEdit->setObjectName(QString::fromLatin1(kPatEditName));
  m_patEdit->setEchoMode(QLineEdit::Password);
  m_patEdit->setPlaceholderText(tr("Leave blank to keep existing"));
  m_patEdit->setToolTip(tr("Personal Access Token used to authenticate against the remote.\n"
                           "Leave blank to keep the existing token"));
  m_secretLabel = new QLabel(tr("Personal Access Token"), centralWidget);
  formLayout->addRow(m_secretLabel, m_patEdit);
  connect(m_patEdit, &QLineEdit::textChanged, this, &NotebookSyncInfoDialog2::onFieldEdited);

  // 4. Last Sync (read-only).
  m_lastSyncLabel = new QLabel(centralWidget);
  m_lastSyncLabel->setObjectName(QString::fromLatin1(kLastSyncLabelName));
  m_lastSyncLabel->setText(tr("Never"));
  m_lastSyncLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  formLayout->addRow(tr("Last sync"), m_lastSyncLabel);

  // 5. Current State (read-only, color-coded).
  m_currentStateLabel = new QLabel(centralWidget);
  m_currentStateLabel->setObjectName(QString::fromLatin1(kCurrentStateLabelName));
  m_currentStateLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  formLayout->addRow(tr("Current state"), m_currentStateLabel);

  // Action button row above the standard dialog button box.
  auto *actionRow = new QWidget(centralWidget);
  auto *actionLayout = new QHBoxLayout(actionRow);
  actionLayout->setContentsMargins(0, 0, 0, 0);
  actionLayout->addStretch();

  m_disableSyncButton = new QPushButton(tr("Disable Sync"), actionRow);
  m_disableSyncButton->setObjectName(QString::fromLatin1(kDisableSyncButtonName));
  // Mark as a destructive action for QSS theming. WidgetsFactory does not
  // currently expose a danger-button helper, so we set the property directly.
  m_disableSyncButton->setProperty(PropertyDefs::c_dangerButton, true);
  m_disableSyncButton->setToolTip(
      tr("Disable sync for this notebook and delete credentials from the system keychain. "
         "Local files and recovery data are preserved"));
  actionLayout->addWidget(m_disableSyncButton);
  connect(m_disableSyncButton, &QPushButton::clicked, this,
          &NotebookSyncInfoDialog2::onDisableSyncClicked);

  formLayout->addRow(actionRow);

  setCentralWidget(centralWidget);

  // Standard dialog buttons. Mirror ManageNotebooksDialog2's button set so the
  // Apply/Reset dirty-flag pattern works the same way.
  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Reset |
                     QDialogButtonBox::Cancel);

  // Tag the standard buttons with objectNames so tests can findChild() them.
  if (auto *box = getDialogButtonBox()) {
    if (auto *btn = box->button(QDialogButtonBox::Ok)) {
      btn->setObjectName(QString::fromLatin1(kOkButtonName));
    }
    if (auto *btn = box->button(QDialogButtonBox::Apply)) {
      btn->setObjectName(QString::fromLatin1(kApplyButtonName));
    }
    if (auto *btn = box->button(QDialogButtonBox::Reset)) {
      btn->setObjectName(QString::fromLatin1(kResetButtonName));
    }
    if (auto *btn = box->button(QDialogButtonBox::Cancel)) {
      btn->setObjectName(QString::fromLatin1(kCancelButtonName));
    }
  }

  setWindowTitle(tr("Sync Info"));
  connect(m_backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
    m_patEdit->clear();
    m_webdavUsernameEdit->clear();
    refreshBackendFields();
  });
  refreshBackendFields();
}

void NotebookSyncInfoDialog2::connectSyncServiceSignals() {
  auto *syncSvc = m_services.get<SyncService>();
  if (!syncSvc) {
    // SyncService is registered by main.cpp at startup; absence indicates a
    // misconfigured ServiceLocator (most likely in a unit-test that did not
    // wire SyncService). Skip live updates rather than crashing.
    return;
  }

  // Both ends are GUI-thread (SyncService re-emits on the GUI thread; the
  // dialog also lives on the GUI thread). Default Qt::AutoConnection is
  // therefore equivalent to DirectConnection.
  connect(syncSvc, &SyncService::syncStarted, this, &NotebookSyncInfoDialog2::onSyncStarted);
  connect(syncSvc, &SyncService::syncFinished, this, &NotebookSyncInfoDialog2::onSyncFinished);
  connect(syncSvc, &SyncService::syncFailed, this, &NotebookSyncInfoDialog2::onSyncFailed);
  connect(syncSvc, &SyncService::conflictsDetected, this,
          &NotebookSyncInfoDialog2::onConflictsDetected);
}

void NotebookSyncInfoDialog2::setBootstrapMode(bool p_enabled) {
  m_bootstrapMode = p_enabled;
  setProperty(kBootstrapModeProperty, p_enabled);

  auto *box = getDialogButtonBox();
  if (!box) {
    return;
  }

  if (auto *applyBtn = box->button(QDialogButtonBox::Apply)) {
    applyBtn->setVisible(!p_enabled && !m_preCreateMode);
  }
  if (auto *resetBtn = box->button(QDialogButtonBox::Reset)) {
    resetBtn->setVisible(!p_enabled && !m_preCreateMode);
  }
  if (m_disableSyncButton) {
    m_disableSyncButton->setVisible(!p_enabled && m_syncEnabled && !m_rawNotebook);
  }
  if (auto *okBtn = box->button(QDialogButtonBox::Ok)) {
    okBtn->setText(p_enabled ? tr("Bootstrap") : tr("OK"));
  }

  refreshDirtyButtons();
}

void NotebookSyncInfoDialog2::setPreCreateNotebookName(const QString &p_name) {
  if (!m_notebookNameLabel) {
    return;
  }

  const QString name = p_name.trimmed();
  if (name.isEmpty()) {
    m_notebookNameLabel->hide();
  } else {
    m_notebookNameLabel->setText(name);
    m_notebookNameLabel->show();
  }
}

bool NotebookSyncInfoDialog2::changesPending() const {
  return m_remoteUrlEdit &&
         (m_remoteUrlEdit->text() != m_lastAppliedRemoteUrl ||
          m_backendCombo->currentData().toString() != m_lastAppliedBackend ||
          !m_patEdit->text().isEmpty() || !m_webdavUsernameEdit->text().isEmpty());
}

void NotebookSyncInfoDialog2::onFieldEdited() { refreshDirtyButtons(); }

void NotebookSyncInfoDialog2::refreshDirtyButtons() {
  const bool available =
      !m_rawNotebook && isSupportedSyncBackend(m_backendCombo->currentData().toString());
  setButtonEnabled(QDialogButtonBox::Ok, available && !m_applying);
  setButtonEnabled(QDialogButtonBox::Apply, available && !m_applying && changesPending());
  setButtonEnabled(QDialogButtonBox::Reset, !m_applying && changesPending());
}

void NotebookSyncInfoDialog2::onDisableSyncClicked() {
  const QMessageBox::StandardButton ret = QMessageBox::warning(
      this, tr("Disable Sync"),
      tr("Disable sync for this notebook? Local files and recovery data will be preserved. "
         "No further syncing will occur, and credentials will be deleted from the keychain."),
      QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);

  if (ret != QMessageBox::Ok) {
    return;
  }

  // T11: hand the destructive action off to the controller, which calls
  // SyncService::disableSyncForNotebook + clears the keychain entry.
  if (m_controller) {
    m_controller->disableSync();
  }

  // T1: close the dialog after successful disable to prevent subsequent OK
  // click from triggering the empty-credential path via acceptedButtonClicked.
  accept();
}

void NotebookSyncInfoDialog2::acceptedButtonClicked() { applySettings(true); }

void NotebookSyncInfoDialog2::appliedButtonClicked() { applySettings(false); }

void NotebookSyncInfoDialog2::applySettings(bool p_closeOnSuccess) {
  if (m_applying || m_rawNotebook) {
    return;
  }
  const auto settings = enteredSettings();
  auto error = validateSyncSettings(settings, m_preCreateMode);
  if (error.isEmpty() && settings.m_backend == QLatin1String("webdav") &&
      !settings.m_credentials.m_username.isEmpty() && settings.m_credentials.m_secret.isEmpty()) {
    error = tr("Enter a new password or app password when specifying a username.");
  }
  if (!error.isEmpty()) {
    setInformationText(error, InformationLevel::Error);
    return;
  }
  if (m_preCreateMode) {
    accept();
    return;
  }
  if (!m_controller) {
    return;
  }

  auto *syncSvc = m_services.get<SyncService>();
  const bool bootstrap =
      m_bootstrapMode || !m_syncEnabled || (syncSvc && !syncSvc->isSyncRegistered(m_notebookId));
  const bool showReadOnlyNotice =
      m_isReadOnlyNotebook && !settings.m_credentials.m_secret.isEmpty();
  m_applying = true;
  m_backendCombo->setEnabled(false);
  m_remoteUrlEdit->setEnabled(false);
  m_gitUsernameEdit->setEnabled(false);
  m_webdavUsernameEdit->setEnabled(false);
  m_patEdit->setEnabled(false);
  m_disableSyncButton->setEnabled(false);
  refreshDirtyButtons();
  auto conn = std::make_shared<QMetaObject::Connection>();
  *conn = connect(m_controller, &NotebookSyncInfoController::applyComplete, this,
                  [this, conn, p_closeOnSuccess, showReadOnlyNotice](bool p_success) {
                    QObject::disconnect(*conn);
                    m_applying = false;
                    if (p_success) {
                      m_lastAppliedRemoteUrl = m_remoteUrlEdit->text();
                      m_lastAppliedBackend = m_backendCombo->currentData().toString();
                      m_syncEnabled = m_controller->syncEnabled();
                      m_patEdit->clear();
                      m_webdavUsernameEdit->clear();
                      if (showReadOnlyNotice) {
                        QMessageBox::information(
                            this, tr("Credentials Saved"),
                            tr("Credentials have been saved. Please close and re-open this "
                               "notebook to enable editing."));
                      }
                    }
                    refreshBackendFields();
                    if (p_success && p_closeOnSuccess) {
                      accept();
                    }
                  });
  if (bootstrap) {
    m_controller->bootstrapApply(settings);
  } else {
    m_controller->applyChanges(settings);
  }
}

void NotebookSyncInfoDialog2::resetButtonClicked() {
  m_remoteUrlEdit->setText(m_lastAppliedRemoteUrl);
  setBackend(m_lastAppliedBackend);
  m_webdavUsernameEdit->clear();
  m_patEdit->clear();
  refreshDirtyButtons();
}

void NotebookSyncInfoDialog2::onSyncStarted(const QString &p_notebookId) {
  if (p_notebookId != m_notebookId) {
    return;
  }
  setCurrentStateLabel(SyncStateLevel::Syncing, tr("Syncing..."));
}

void NotebookSyncInfoDialog2::onSyncFinished(const QString &p_notebookId, VxCoreError p_result) {
  if (p_notebookId != m_notebookId) {
    return;
  }
  if (p_result == VXCORE_OK) {
    setCurrentStateLabel(SyncStateLevel::Idle, tr("Idle"));
    // Refresh the "Last sync" label live: SyncService::onSyncFinished has just
    // persisted the new timestamp (on the GUI thread, before emitting
    // syncFinished), so re-reading via the controller yields a fresh value. We
    // deliberately do NOT call loadInitialData() here, which would also reset
    // the remote-URL field and clobber any in-progress edits.
    if (m_controller && m_lastSyncLabel) {
      const QString lastSync = m_controller->lastSyncTime();
      m_lastSyncLabel->setText(lastSync.isEmpty() ? tr("Never") : lastSync);
    }
  } else {
    setCurrentStateLabel(SyncStateLevel::Error,
                         tr("Error (code %1)").arg(static_cast<int>(p_result)));
  }
}

void NotebookSyncInfoDialog2::onSyncFailed(const QString &p_notebookId, VxCoreError p_code,
                                           const QString &p_message) {
  if (p_notebookId != m_notebookId) {
    return;
  }
  Q_UNUSED(p_code);
  const QString text = p_message.isEmpty() ? tr("Error") : tr("Error: %1").arg(p_message);
  setCurrentStateLabel(SyncStateLevel::Error, text);
}

void NotebookSyncInfoDialog2::onConflictsDetected(const QString &p_notebookId,
                                                  const QStringList &p_conflictFiles) {
  if (p_notebookId != m_notebookId) {
    return;
  }
  setCurrentStateLabel(SyncStateLevel::Conflict,
                       tr("Conflicts (%1 file(s))").arg(p_conflictFiles.size()));
}

void NotebookSyncInfoDialog2::refreshReadOnlyBanner() {
  // T29: pre-create mode has no notebook ID yet to query, so the banner is
  // always hidden. The empty-id guard is defense-in-depth in case the
  // (services, notebookId, parent) ctor is ever invoked with an empty id.
  if (m_preCreateMode || m_notebookId.isEmpty()) {
    if (m_readOnlyBanner) {
      m_readOnlyBanner->hide();
    }
    m_isReadOnlyNotebook = false;
    return;
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    // Defensive: the dialog is only buildable with a wired ServiceLocator,
    // but a unit-test ServiceLocator could lack NotebookCoreService. Fail
    // closed (no banner) rather than asserting and crashing.
    if (m_readOnlyBanner) {
      m_readOnlyBanner->hide();
    }
    m_isReadOnlyNotebook = false;
    return;
  }

  m_isReadOnlyNotebook = notebookService->isNotebookReadOnly(m_notebookId);
  if (m_readOnlyBanner) {
    m_readOnlyBanner->setVisible(m_isReadOnlyNotebook);
  }
}

void NotebookSyncInfoDialog2::setCurrentStateLabel(SyncStateLevel p_level, const QString &p_text) {
  if (!m_currentStateLabel) {
    return;
  }

  m_currentStateLabel->setText(p_text);

  // Severity is published as a QSS property, never as a hardcoded color: every
  // theme maps SeverityText to its own info/warning/error foreground, and the
  // label re-themes itself when the user switches theme.
  QString severity;
  switch (p_level) {
  case SyncStateLevel::Idle:
    break; // Default color.
  case SyncStateLevel::Syncing:
    severity = QStringLiteral("info");
    break;
  case SyncStateLevel::Conflict:
    severity = QStringLiteral("warning");
    break;
  case SyncStateLevel::Error:
    severity = QStringLiteral("error");
    break;
  }
  WidgetUtils::setPropertyDynamically(m_currentStateLabel, PropertyDefs::c_severityText, severity);
}

QString NotebookSyncInfoDialog2::enteredRemoteUrl() const {
  return m_remoteUrlEdit ? m_remoteUrlEdit->text() : QString();
}

SyncSettings NotebookSyncInfoDialog2::enteredSettings() const {
  SyncSettings settings;
  settings.m_backend = m_backendCombo->currentData().toString();
  settings.m_remoteUrl = enteredRemoteUrl().trimmed();
  settings.m_credentials.m_backend = settings.m_backend;
  if (settings.m_backend == QLatin1String("webdav")) {
    settings.m_credentials.m_username = m_webdavUsernameEdit->text();
  }
  settings.m_credentials.m_secret = m_patEdit->text();
  return settings;
}

bool NotebookSyncInfoDialog2::isPreCreateMode() const { return m_preCreateMode; }

void NotebookSyncInfoDialog2::setBackend(const QString &p_backend) {
  int index = m_backendCombo->findData(p_backend);
  if (index < 0) {
    m_backendCombo->addItem(tr("Unsupported (%1)").arg(p_backend), p_backend);
    index = m_backendCombo->count() - 1;
  }
  m_backendCombo->setCurrentIndex(index);
  refreshBackendFields();
}

void NotebookSyncInfoDialog2::refreshBackendFields() {
  const auto backend = m_backendCombo->currentData().toString();
  const bool git = backend == QLatin1String("git");
  const bool webdav = backend == QLatin1String("webdav");
  const bool available = (git || webdav) && !m_rawNotebook;
  m_backendCombo->setEnabled(!m_syncEnabled && !m_rawNotebook && !m_applying);
  m_remoteUrlEdit->setEnabled(available && !m_applying);
  m_patEdit->setEnabled(available && !m_applying);
  m_remoteUrlLabel->setText(webdav ? tr("Collection URL") : tr("Remote URL"));
  m_remoteUrlEdit->setPlaceholderText(!available ? QString()
                                      : webdav   ? tr("https://example.com/dav/notebook/")
                                                 : tr("https://github.com/example/notes.git"));
  m_remoteUrlEdit->setToolTip(!available ? QString()
                              : webdav
                                  ? tr("Dedicated existing HTTPS notebook collection")
                                  : tr("Remote git repository URL used for syncing this notebook"));
  m_remoteUrlHintLabel->setVisible(git && !m_rawNotebook);
  m_webdavHint->setVisible(webdav && !m_rawNotebook);
  m_gitUsernameLabel->setVisible(git && !m_rawNotebook);
  m_gitUsernameEdit->setVisible(git && !m_rawNotebook);
  m_gitUsernameEdit->setEnabled(git && !m_rawNotebook && !m_applying &&
                                m_remoteUrlEdit->text().startsWith(QLatin1String("https://")));
  m_webdavUsernameLabel->setVisible(webdav && !m_rawNotebook);
  m_webdavUsernameEdit->setVisible(webdav && !m_rawNotebook);
  m_webdavUsernameEdit->setEnabled(available && !m_applying);
  m_webdavUsernameEdit->setPlaceholderText(m_syncEnabled ? tr("Leave blank to keep existing")
                                                         : QString());
  m_secretLabel->setText(webdav ? tr("Password or app password") : tr("Personal Access Token"));
  m_secretLabel->setVisible(available);
  m_patEdit->setVisible(available);
  m_patEdit->setPlaceholderText(m_syncEnabled ? tr("Leave blank to keep existing") : QString());
  m_patEdit->setToolTip(webdav
                            ? tr("Password or app password; never stored in notebook settings")
                            : tr("Personal Access Token used to authenticate against the remote"));
  m_disableSyncButton->setVisible(m_syncEnabled && !m_rawNotebook && !m_preCreateMode &&
                                  !m_bootstrapMode);
  m_disableSyncButton->setEnabled(!m_applying);
  if (!available) {
    const auto error = m_rawNotebook ? tr("Sync is unavailable for raw notebooks.")
                                     : tr("Unsupported sync backend.");
    setInformationText(error, InformationLevel::Error);
    setCurrentStateLabel(SyncStateLevel::Error, error);
  } else {
    setInformationText(QString(), InformationLevel::Info);
  }
  refreshDirtyButtons();
}

void NotebookSyncInfoDialog2::onConfirmUrlChange(const QString &p_oldUrl, const QString &p_newUrl) {
  if (!m_controller) {
    return;
  }

  const QMessageBox::StandardButton ret =
      QMessageBox::warning(this, tr("Sync"),
                           tr("This changes the sync endpoint. Current working files will be kept; "
                              "local sync state will be retired or rebuilt after confirmation.\n"
                              "Old URL: %1\nNew URL: %2\n\nContinue?")
                               .arg(p_oldUrl.isEmpty() ? tr("(none)") : p_oldUrl,
                                    p_newUrl.isEmpty() ? tr("(none)") : p_newUrl),
                           QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (ret == QMessageBox::Yes) {
    m_controller->confirmUrlChange(true);
  } else {
    // Revert the URL field so the dialog reflects the cancelled change.
    if (m_remoteUrlEdit) {
      m_remoteUrlEdit->setText(p_oldUrl);
    }
    refreshDirtyButtons();
    m_controller->confirmUrlChange(false);
  }
}

void NotebookSyncInfoDialog2::onError(const QString &p_message) {
  // Per plan: empty messages MUST NOT be suppressed silently — render a
  // generic fallback. Dialog stays open so the user can retry.
  const auto presented = SyncErrorPresenter::present(SyncErrorPresenter::Context::CredentialWrite,
                                                     VXCORE_ERR_UNKNOWN, p_message);
  const QString text =
      presented.primary.isEmpty() ? tr("Sync operation failed.") : presented.primary;
  if (!presented.details.isEmpty()) {
    qCDebug(syncCategory) << "SyncErrorPresenter details:" << presented.details;
  }
  QMessageBox::critical(this, tr("Sync"), text);
}
