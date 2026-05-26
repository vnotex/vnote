#include "notebooksyncinfodialog2.h"

#include <memory>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QShowEvent>

#include <controllers/notebooksyncinfocontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synclog.h>
#include <core/services/syncservice.h>

#include "../propertydefs.h"
#include "../widgetsfactory.h"

using namespace vnotex;

namespace {

// QObject names that tests use to discover widgets via findChild<>(). MUST be
// kept in sync with the documentation in the dialog header.
const char *const kRemoteUrlEditName = "remoteUrlEdit";
const char *const kRemoteUrlHintLabelName = "remoteUrlHintLabel";
const char *const kPatEditName = "patEdit";
const char *const kLastSyncLabelName = "lastSyncLabel";
const char *const kCurrentStateLabelName = "currentStateLabel";
const char *const kDisableSyncButtonName = "disableSyncButton";
const char *const kNotebookNameLabelName = "notebookNameLabel";
const char *const kApplyButtonName = "applyButton";
const char *const kOkButtonName = "okButton";
const char *const kResetButtonName = "resetButton";
const char *const kCancelButtonName = "cancelButton";

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

  // Populate read-only labels via the controller (authoritative source for
  // notebook display name + remote URL + formatted last-sync timestamp). The
  // controller emits dataLoaded(...); subscribe locally to update the labels
  // and snapshot the last-applied URL.
  if (m_controller) {
    connect(m_controller, &NotebookSyncInfoController::dataLoaded, this,
            [this](const QString &p_name, const QString &p_remoteUrl, const QString &p_lastSync) {
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

  // Default current state to Idle. SyncService signals will move this to
  // Syncing/Conflict/Error as events arrive.
  setCurrentStateLabel(SyncStateLevel::Idle, tr("Idle"));
}

// Pre-create overload: used by NewNotebookDialog2 to collect URL + PAT before
// the notebook exists in vxcore. The controller is NOT constructed; values are
// read back via enteredRemoteUrl()/enteredPat().
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

  // Override the "leave blank to keep existing" hint that setupUI() sets
  // by default — there's no existing PAT for a brand-new notebook.
  if (m_patEdit) {
    m_patEdit->setPlaceholderText(QString());
    m_patEdit->setToolTip(
        tr("Personal Access Token used to authenticate against the remote (optional)."));
  }

  // m_controller remains nullptr — this signals pre-create mode to
  // acceptedButtonClicked, which bypasses applyChanges.
}

void NotebookSyncInfoDialog2::setupUI() {
  auto *centralWidget = new QWidget(this);
  auto *formLayout = new QFormLayout(centralWidget);

  // 1. Notebook name (read-only).
  m_notebookNameLabel = new QLabel(centralWidget);
  m_notebookNameLabel->setObjectName(QString::fromLatin1(kNotebookNameLabelName));
  m_notebookNameLabel->setText(m_notebookId);
  m_notebookNameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  formLayout->addRow(tr("Notebook:"), m_notebookNameLabel);

  // 2. Remote URL (editable).
  m_remoteUrlEdit = WidgetsFactory::createLineEdit(centralWidget);
  m_remoteUrlEdit->setObjectName(QString::fromLatin1(kRemoteUrlEditName));
  m_remoteUrlEdit->setPlaceholderText(tr("https://github.com/example/notes.git"));
  m_remoteUrlEdit->setToolTip(tr("Remote git repository URL used for syncing this notebook."));
  formLayout->addRow(tr("Remote URL:"), m_remoteUrlEdit);
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
  m_remoteUrlHintLabel->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
  formLayout->addRow(QString(), m_remoteUrlHintLabel);

  // 3. PAT (editable, password-masked, NEVER prefilled).
  m_patEdit = WidgetsFactory::createLineEdit(centralWidget);
  m_patEdit->setObjectName(QString::fromLatin1(kPatEditName));
  m_patEdit->setEchoMode(QLineEdit::Password);
  m_patEdit->setPlaceholderText(tr("Leave blank to keep existing"));
  m_patEdit->setToolTip(tr("Personal Access Token used to authenticate against the remote.\n"
                           "Leave blank to keep the existing token."));
  formLayout->addRow(tr("Personal Access Token:"), m_patEdit);
  connect(m_patEdit, &QLineEdit::textChanged, this, &NotebookSyncInfoDialog2::onFieldEdited);

  // 4. Last Sync (read-only).
  m_lastSyncLabel = new QLabel(centralWidget);
  m_lastSyncLabel->setObjectName(QString::fromLatin1(kLastSyncLabelName));
  m_lastSyncLabel->setText(tr("Never"));
  m_lastSyncLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  formLayout->addRow(tr("Last Sync:"), m_lastSyncLabel);

  // 5. Current State (read-only, color-coded).
  m_currentStateLabel = new QLabel(centralWidget);
  m_currentStateLabel->setObjectName(QString::fromLatin1(kCurrentStateLabelName));
  m_currentStateLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  formLayout->addRow(tr("Current State:"), m_currentStateLabel);

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
      tr("Disable git sync for this notebook. Local commit history is preserved\n"
         "on disk, but no further syncing will occur and the stored credentials\n"
         "are deleted from the system keychain."));
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
    applyBtn->setVisible(!p_enabled);
  }
  if (auto *resetBtn = box->button(QDialogButtonBox::Reset)) {
    resetBtn->setVisible(!p_enabled);
  }
  if (m_disableSyncButton) {
    m_disableSyncButton->setVisible(!p_enabled);
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
  if (!m_remoteUrlEdit || !m_patEdit) {
    return false;
  }
  // PAT is dirty whenever the field is non-empty (any text means the user
  // wants to replace the existing PAT). URL is dirty when it differs from the
  // last-applied value.
  if (!m_patEdit->text().isEmpty()) {
    return true;
  }
  return m_remoteUrlEdit->text() != m_lastAppliedRemoteUrl;
}

void NotebookSyncInfoDialog2::onFieldEdited() { refreshDirtyButtons(); }

void NotebookSyncInfoDialog2::refreshDirtyButtons() {
  // Apply/Reset hidden in bootstrap mode, so don't toggle their state then.
  if (m_bootstrapMode) {
    return;
  }
  const bool dirty = changesPending();
  setButtonEnabled(QDialogButtonBox::Apply, dirty);
  setButtonEnabled(QDialogButtonBox::Reset, dirty);
}

void NotebookSyncInfoDialog2::onDisableSyncClicked() {
  const QMessageBox::StandardButton ret = QMessageBox::warning(
      this, tr("Disable Sync"),
      tr("Disable git sync for this notebook? Local commit history will be preserved\n"
         "on disk but no further syncing will occur. (The PAT will be deleted from\n"
         "the keychain.)"),
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

void NotebookSyncInfoDialog2::acceptedButtonClicked() {
  // Pre-create mode: caller (NewNotebookDialog2) reads URL/PAT via accessors
  // and does the actual create+bootstrap atomically. We just close the dialog.
  if (m_preCreateMode) {
    accept();
    return;
  }

  qCDebug(syncCategory) << "NotebookSyncInfoDialog2::acceptedButtonClicked: accept bootstrapMode:"
                        << m_bootstrapMode;

  // W3.T2: route through controller->bootstrapApply() when the user is
  // performing the initial enable (bootstrapMode==true) OR when the notebook
  // is partial (sync_enabled on disk but not yet registered at vxcore
  // runtime). In either case, applyChanges would silently fail / hit the
  // chicken-and-egg path, so bootstrapApply (which atomically enables sync +
  // persists URL on success) is the correct entry point.
  //
  // The dialog STAYS OPEN during the async bootstrap and only accept()s when
  // applyComplete(true) fires. On applyComplete(false) the dialog stays open
  // so the user can read the error (surfaced via the controller's error()
  // signal / state label) and retry without losing their input.
  if (m_controller) {
    auto *syncSvc = m_services.get<SyncService>();
    const bool partial = (syncSvc != nullptr) && !syncSvc->isSyncRegistered(m_notebookId);
    if (m_bootstrapMode || partial) {
      qCDebug(syncCategory) << "NotebookSyncInfoDialog2::acceptedButtonClicked: routing to "
                               "bootstrapApply bootstrapMode:"
                            << m_bootstrapMode << "partial:" << partial;
      // One-shot connection to applyComplete: only accept() on success.
      auto conn = std::make_shared<QMetaObject::Connection>();
      *conn = connect(m_controller, &NotebookSyncInfoController::applyComplete, this,
                      [this, conn](bool p_success) {
                        QObject::disconnect(*conn);
                        if (p_success) {
                          // Snapshot the URL and clear the PAT field per the
                          // leave-blank-to-keep semantics before closing.
                          if (m_remoteUrlEdit) {
                            m_lastAppliedRemoteUrl = m_remoteUrlEdit->text();
                          }
                          if (m_patEdit) {
                            m_patEdit->clear();
                          }
                          refreshDirtyButtons();
                          accept();
                        }
                        // On failure, the dialog stays open. The error()
                        // signal from the controller surfaces the message
                        // via the existing state label / log path.
                      });
      m_controller->bootstrapApply(m_remoteUrlEdit->text(), m_patEdit->text());
      return;
    }
  }

  // T11: the controller decides whether to send URL only, PAT only, or both
  // based on which fields are dirty. Then we still close the dialog so the
  // bootstrap-mode flow can complete.
  if (m_controller) {
    m_controller->applyChanges(m_remoteUrlEdit->text(), m_patEdit->text());
  }

  // Snapshot the current URL as the new "last applied" baseline so a
  // re-opened dialog reports no pending changes for an unchanged URL. PAT
  // field is cleared to enforce the leave-blank-to-keep semantics.
  m_lastAppliedRemoteUrl = m_remoteUrlEdit->text();
  m_patEdit->clear();
  refreshDirtyButtons();

  accept();
}

void NotebookSyncInfoDialog2::appliedButtonClicked() {
  // W3.T2: mirror the partial-detection branch from acceptedButtonClicked so
  // Apply on a partial / bootstrap-mode notebook routes to bootstrapApply
  // instead of the chicken-and-egg applyChanges path. Unlike OK, Apply must
  // NOT accept() the dialog on success — the Apply button is for in-place
  // updates only.
  if (m_controller) {
    auto *syncSvc = m_services.get<SyncService>();
    const bool partial = (syncSvc != nullptr) && !syncSvc->isSyncRegistered(m_notebookId);
    if (m_bootstrapMode || partial) {
      qCDebug(syncCategory)
          << "NotebookSyncInfoDialog2::appliedButtonClicked: routing to bootstrapApply "
             "bootstrapMode:"
          << m_bootstrapMode << "partial:" << partial;
      // Temporarily disable the Apply button during the async bootstrap so
      // the user cannot double-fire. Re-enabled via refreshDirtyButtons()
      // after applyComplete fires.
      if (auto *box = getDialogButtonBox()) {
        if (auto *applyBtn = box->button(QDialogButtonBox::Apply)) {
          applyBtn->setEnabled(false);
        }
      }
      auto conn = std::make_shared<QMetaObject::Connection>();
      *conn = connect(m_controller, &NotebookSyncInfoController::applyComplete, this,
                      [this, conn](bool p_success) {
                        QObject::disconnect(*conn);
                        if (p_success && m_remoteUrlEdit) {
                          m_lastAppliedRemoteUrl = m_remoteUrlEdit->text();
                        }
                        if (p_success && m_patEdit) {
                          m_patEdit->clear();
                        }
                        refreshDirtyButtons();
                      });
      m_controller->bootstrapApply(m_remoteUrlEdit->text(), m_patEdit->text());
      return;
    }

    m_controller->applyChanges(m_remoteUrlEdit->text(), m_patEdit->text());
  }

  m_lastAppliedRemoteUrl = m_remoteUrlEdit->text();
  m_patEdit->clear();
  refreshDirtyButtons();
}

void NotebookSyncInfoDialog2::resetButtonClicked() {
  m_remoteUrlEdit->setText(m_lastAppliedRemoteUrl);
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

void NotebookSyncInfoDialog2::setCurrentStateLabel(SyncStateLevel p_level, const QString &p_text) {
  if (!m_currentStateLabel) {
    return;
  }

  m_currentStateLabel->setText(p_text);

  // Inline color for state communication. Themes can override via QSS by
  // matching on objectName == "currentStateLabel".
  switch (p_level) {
  case SyncStateLevel::Idle:
    m_currentStateLabel->setStyleSheet(QString());
    break;
  case SyncStateLevel::Syncing:
    m_currentStateLabel->setStyleSheet(QStringLiteral("color: #1f6feb;")); // blue
    break;
  case SyncStateLevel::Conflict:
    m_currentStateLabel->setStyleSheet(QStringLiteral("color: #d29922;")); // orange
    break;
  case SyncStateLevel::Error:
    m_currentStateLabel->setStyleSheet(QStringLiteral("color: #cf222e;")); // red
    break;
  }
}

QString NotebookSyncInfoDialog2::enteredRemoteUrl() const {
  return m_remoteUrlEdit ? m_remoteUrlEdit->text() : QString();
}

QString NotebookSyncInfoDialog2::enteredPat() const {
  return m_patEdit ? m_patEdit->text() : QString();
}

bool NotebookSyncInfoDialog2::isPreCreateMode() const { return m_preCreateMode; }

void NotebookSyncInfoDialog2::onConfirmUrlChange(const QString &p_oldUrl, const QString &p_newUrl) {
  if (!m_controller) {
    return;
  }

  const QMessageBox::StandardButton ret =
      QMessageBox::warning(this, tr("Sync"),
                           tr("This will wipe local sync state and re-clone from the new URL.\n"
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
  const QString text = p_message.isEmpty() ? tr("Sync operation failed.") : p_message;
  QMessageBox::critical(this, tr("Sync"), text);
}
