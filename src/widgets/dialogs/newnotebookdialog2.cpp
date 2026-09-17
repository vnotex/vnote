#include "newnotebookdialog2.h"

#include <memory>

#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <controllers/newnotebookcontroller.h>
#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/syncerrorpresenter.h>
#include <core/services/synclog.h>
#include <core/sessionconfig.h>

#include "../widgetsfactory.h"
#include "notebookinfowidget.h"
#include "notebooksyncinfodialog2.h"

using namespace vnotex;

NewNotebookDialog2::NewNotebookDialog2(ServiceLocator &p_services, QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services) {
  // Create controller.
  m_controller = new NewNotebookController(m_services, this);

  setupUI();

  m_infoWidget->setFocus();
}

NewNotebookDialog2::~NewNotebookDialog2() = default;

void NewNotebookDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *layout = new QVBoxLayout(mainWidget);
  m_infoWidget = new NotebookInfoWidget(m_services, NotebookInfoWidget::Mode::Create, mainWidget);
  layout->addWidget(m_infoWidget);

  auto *syncLayout = WidgetsFactory::createFormLayout();
  layout->addLayout(syncLayout);

  // Sync method selection. Visible only for Bundled notebooks.
  // For Git sync, the actual remote URL + PAT are collected BEFORE notebook
  // creation via the Configure... button (pre-create flow per
  // notebook-sync-config-pre-create plan).
  m_syncMethodLabel = new QLabel(tr("Sync method"), mainWidget);
  m_syncMethodLabel->setObjectName(QStringLiteral("syncMethodLabel"));
  m_syncMethodCombo = WidgetsFactory::createComboBox(mainWidget);
  m_syncMethodCombo->setObjectName(QStringLiteral("syncMethodCombo"));
  m_syncMethodCombo->addItem(tr("None"), QStringLiteral("none"));
  m_syncMethodCombo->addItem(tr("Git"), QStringLiteral("git"));
  m_syncMethodCombo->setToolTip(tr("Git sync is supported only for bundled notebooks. "
                                   "Sync settings are configured immediately via the "
                                   "Configure button before notebook creation"));

  m_configureSyncButton = new QPushButton(tr("Configure"), mainWidget);
  m_configureSyncButton->setObjectName(QStringLiteral("configureSyncButton"));
  m_configureSyncButton->setToolTip(tr("Configure Git sync remote URL and credentials"));
  m_configureSyncButton->hide(); // shown only when Git is the active selection

  m_syncMethodContainer = new QWidget(mainWidget);
  auto *syncMethodLayout = new QHBoxLayout(m_syncMethodContainer);
  syncMethodLayout->setContentsMargins(0, 0, 0, 0);
  syncMethodLayout->addWidget(m_syncMethodCombo, 1);
  syncMethodLayout->addWidget(m_configureSyncButton);

  syncLayout->addRow(m_syncMethodLabel, m_syncMethodContainer);

  connect(m_infoWidget, &NotebookInfoWidget::typeChanged, this,
          &NewNotebookDialog2::onTypeComboChanged);

  connect(m_configureSyncButton, &QPushButton::clicked, this,
          &NewNotebookDialog2::onConfigureSyncClicked);

  connect(m_syncMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) {
            const bool gitSelected =
                m_syncMethodCombo->currentData().toString() == QStringLiteral("git");
            m_configureSyncButton->setVisible(gitSelected && m_syncMethodContainer->isVisible());
            updateOkButtonState();
          });

  setCentralWidget(mainWidget);
  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  setWindowTitle(tr("New Notebook"));
  onTypeComboChanged();
}

void NewNotebookDialog2::onTypeComboChanged() {
  const bool isBundled = m_infoWidget->getType() == NotebookType::Bundled;
  if (!isBundled) {
    // Force back to "None" so an accidental Raw selection doesn't persist a
    // git sync choice.
    m_syncMethodCombo->setCurrentIndex(0);
    // Reset pre-create sync config when switching to Raw.
    m_syncConfigured = false;
    m_pendingRemoteUrl.clear();
    m_pendingPat.clear();
  }
  m_syncMethodLabel->setVisible(isBundled);
  m_syncMethodContainer->setVisible(isBundled);
  updateOkButtonState();
}

void NewNotebookDialog2::acceptedButtonClicked() {
  // Collect input from UI.
  NewNotebookInput input;
  input.name = m_infoWidget->getName();
  input.description = m_infoWidget->getDescription();
  input.rootFolderPath = m_infoWidget->getRootFolder();
  input.type = m_infoWidget->getType();
  input.assetsFolder = m_infoWidget->getAssetsFolder();
  input.lineEnding = m_infoWidget->getLineEnding();
  input.syncMethod = getSelectedSyncMethod();
  // T4: pass pre-collected sync config (set by onConfigureSyncClicked() in T3).
  input.remoteUrl = m_pendingRemoteUrl;
  input.pat = m_pendingPat;

  // Delegate to controller.
  NewNotebookResult result = m_controller->createNotebook(input);

  if (!result.success) {
    setInformationText(result.errorMessage, ScrollDialog::InformationLevel::Error);
    return;
  }

  // Save the parent directory as the default for next time.
  QFileInfo fi(input.rootFolderPath);
  QString parentDir = fi.absolutePath();
  auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
  sessionConfig.setNewNotebookDefaultRootFolderPath(parentDir);

  m_newNotebookId = result.notebookId;
  qCDebug(syncCategory) << "NewNotebookDialog2::acceptedButtonClicked: created syncMethod:"
                        << input.syncMethod << "newNotebookId:" << m_newNotebookId;

  if (input.syncMethod != QStringLiteral("git")) {
    // Non-sync path: the notebook is fully ready. Accept and close.
    accept();
    return;
  }

  // Git sync path (T4 of pre-create plan): chain bootstrapSync. Dialog stays
  // open until bootstrapSucceeded/bootstrapFailed arrives. bootstrapSync
  // shows its own progress modal on top of this dialog and rolls back on
  // failure (closes notebook, removes root) so the user can retry by
  // clicking OK again.
  // Use shared QMetaObject::Connection pair so each lambda can disconnect both
  // (matches the existing pattern in NewNotebookController::bootstrapSync).
  auto succConn = std::make_shared<QMetaObject::Connection>();
  auto failConn = std::make_shared<QMetaObject::Connection>();

  *succConn = connect(m_controller, &NewNotebookController::bootstrapSucceeded, this,
                      [this, succConn, failConn](const QString &p_id) {
                        if (p_id != m_newNotebookId) {
                          // Not our event; ignore.
                          return;
                        }
                        QObject::disconnect(*succConn);
                        QObject::disconnect(*failConn);
                        qCDebug(syncCategory)
                            << "NewNotebookDialog2::acceptedButtonClicked: bootstrap succeeded "
                               "notebookId:"
                            << p_id;
                        accept();
                      });

  *failConn =
      connect(m_controller, &NewNotebookController::bootstrapFailed, this,
              [this, succConn, failConn](const QString &p_id, const QString &p_errMsg) {
                if (p_id != m_newNotebookId) {
                  return;
                }
                QObject::disconnect(*succConn);
                QObject::disconnect(*failConn);
                qCDebug(syncCategory)
                    << "NewNotebookDialog2::acceptedButtonClicked: bootstrap failed "
                       "notebookId:"
                    << p_id << "err:" << p_errMsg;
                // bootstrapSync already rolled back the partial notebook
                // (closed + removed root). User can adjust and retry.
                m_newNotebookId.clear(); // notebook no longer exists

                const auto presented = SyncErrorPresenter::present(
                    SyncErrorPresenter::Context::CredentialWrite, VXCORE_ERR_UNKNOWN, p_errMsg);
                setInformationText(presented.primary, ScrollDialog::InformationLevel::Error);
                if (!presented.details.isEmpty()) {
                  qCDebug(syncCategory) << "SyncErrorPresenter details:" << presented.details;
                }
                // Dialog stays open.
              });

  m_controller->bootstrapSync(result.notebookId, input.remoteUrl, input.pat, this);
}

QString NewNotebookDialog2::getNewNotebookId() const { return m_newNotebookId; }

QString NewNotebookDialog2::getSelectedSyncMethod() const {
  if (!m_syncMethodCombo || !m_syncMethodCombo->isVisible()) {
    return QStringLiteral("none");
  }
  return m_syncMethodCombo->currentData().toString();
}

void NewNotebookDialog2::onConfigureSyncClicked() {
  // Pre-create overload of NotebookSyncInfoDialog2: collects remote URL + PAT
  // without persisting to vxcore. T4 reads them back via m_pendingRemoteUrl /
  // m_pendingPat in acceptedButtonClicked() to perform create+bootstrap
  // atomically.
  NotebookSyncInfoDialog2 dlg(m_services, this);
  const QString notebookName = m_infoWidget->getName().trimmed();
  dlg.setPreCreateNotebookName(notebookName);
  if (dlg.exec() == QDialog::Accepted) {
    m_pendingRemoteUrl = dlg.enteredRemoteUrl().trimmed();
    m_pendingPat = dlg.enteredPat();
    m_syncConfigured = !m_pendingRemoteUrl.isEmpty();
    qCDebug(syncCategory)
        << "NewNotebookDialog2::onConfigureSyncClicked: configured remoteUrlPresent:"
        << m_syncConfigured;
  }
  updateOkButtonState();
}

void NewNotebookDialog2::updateOkButtonState() {
  const QString syncMethod = getSelectedSyncMethod();
  const bool needsSync = (syncMethod == QStringLiteral("git"));
  const bool ok = !needsSync || m_syncConfigured;
  setButtonEnabled(QDialogButtonBox::Ok, ok);
  if (auto *box = getDialogButtonBox()) {
    if (auto *okBtn = box->button(QDialogButtonBox::Ok)) {
      okBtn->setToolTip(ok ? QString() : tr("Click 'Configure' to set up Git sync first"));
    }
  }
}
