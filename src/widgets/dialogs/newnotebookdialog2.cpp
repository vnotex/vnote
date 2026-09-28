#include "newnotebookdialog2.h"

#include <memory>

#include <QFileInfo>
#include <QPushButton>
#include <QVBoxLayout>

#include <controllers/newnotebookcontroller.h>
#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/syncerrorpresenter.h>
#include <core/services/synclog.h>
#include <core/sessionconfig.h>

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

  connect(m_infoWidget, &NotebookInfoWidget::configureSyncRequested, this,
          &NewNotebookDialog2::onConfigureSyncClicked);
  connect(m_infoWidget, &NotebookInfoWidget::syncMethodChanged, this,
          [this](const QString &p_backend) {
            if (p_backend != m_pendingSettings.m_backend) {
              m_syncConfigured = false;
              m_pendingSettings = SyncSettings();
              m_pendingSettings.m_backend = p_backend;
            }
            updateOkButtonState();
          });

  setCentralWidget(mainWidget);
  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  setWindowTitle(tr("New Notebook"));
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
  input.syncMethod = m_infoWidget->getSyncMethod();
  input.syncSettings = m_pendingSettings;

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

  if (input.syncMethod == QStringLiteral("none")) {
    // Non-sync path: the notebook is fully ready. Accept and close.
    accept();
    return;
  }

  // Chain bootstrapSync for either backend. The dialog stays
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

  *failConn = connect(
      m_controller, &NewNotebookController::bootstrapFailed, this,
      [this, succConn, failConn](const QString &p_id, const QString &p_errMsg) {
        if (p_id != m_newNotebookId) {
          return;
        }
        QObject::disconnect(*succConn);
        QObject::disconnect(*failConn);
        qCDebug(syncCategory) << "NewNotebookDialog2::acceptedButtonClicked: bootstrap failed "
                                 "notebookId:"
                              << p_id << "err:" << p_errMsg;
        // bootstrapSync already rolled back the partial notebook
        // (closed + removed root). User can adjust and retry.
        m_newNotebookId.clear(); // notebook no longer exists

        const auto presented = SyncErrorPresenter::present(
            SyncErrorPresenter::Context::CredentialWrite, VXCORE_ERR_UNKNOWN, p_errMsg);
        QString message = presented.primary;
        if (!presented.details.trimmed().isEmpty() && presented.details != presented.primary) {
          message += QLatin1Char('\n') + presented.details;
        }
        setInformationText(message, ScrollDialog::InformationLevel::Error);
        if (!presented.details.isEmpty()) {
          qCDebug(syncCategory) << "SyncErrorPresenter details:" << presented.details;
        }
        // Dialog stays open.
      });

  m_controller->bootstrapSync(result.notebookId, input.syncSettings, this);
}

QString NewNotebookDialog2::getNewNotebookId() const { return m_newNotebookId; }

void NewNotebookDialog2::onConfigureSyncClicked() {
  NotebookSyncInfoDialog2 dlg(m_services, this);
  dlg.setBackend(m_infoWidget->getSyncMethod());
  dlg.setPreCreateNotebookName(m_infoWidget->getName().trimmed());
  if (dlg.exec() == QDialog::Accepted) {
    auto settings = dlg.enteredSettings();
    // The inner selector is authoritative; changing it also changes creation.
    m_infoWidget->setSyncMethod(settings.m_backend);
    m_pendingSettings = std::move(settings);
    m_syncConfigured = validateSyncSettings(m_pendingSettings, true).isEmpty();
  }
  updateOkButtonState();
}

void NewNotebookDialog2::updateOkButtonState() {
  const QString syncMethod = m_infoWidget->getSyncMethod();
  const bool needsSync = isSupportedSyncBackend(syncMethod);
  const bool ok = !needsSync || (m_syncConfigured && m_pendingSettings.m_backend == syncMethod);
  setButtonEnabled(QDialogButtonBox::Ok, ok);
  if (auto *box = getDialogButtonBox()) {
    if (auto *okBtn = box->button(QDialogButtonBox::Ok)) {
      okBtn->setToolTip(ok ? QString() : tr("Click 'Configure' to set up sync first"));
    }
  }
}
