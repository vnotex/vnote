#include "newnotebookdialog2.h"

#include <memory>

#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <controllers/newnotebookcontroller.h>
#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/snippetcoreservice.h>
#include <core/services/syncerrorpresenter.h>
#include <core/services/synclog.h>
#include <core/sessionconfig.h>
#include <utils/pathutils.h>

#include "../lineeditwithsnippet.h"
#include "../locationinputwithbrowsebutton.h"
#include "../widgetsfactory.h"
#include "notebooksyncinfodialog2.h"

using namespace vnotex;

NewNotebookDialog2::NewNotebookDialog2(ServiceLocator &p_services, QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services) {
  // Create controller.
  m_controller = new NewNotebookController(m_services, this);

  setupUI();

  m_nameEdit->setFocus();
}

NewNotebookDialog2::~NewNotebookDialog2() = default;

void NewNotebookDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *layout = new QFormLayout(mainWidget);

  // Name input.
  auto *snippetService = m_services.get<SnippetCoreService>();
  m_nameEdit = WidgetsFactory::createLineEditWithSnippet(snippetService, mainWidget);
  layout->addRow(tr("Name:"), m_nameEdit);

  // Description input.
  m_descriptionEdit = new QPlainTextEdit(mainWidget);
  m_descriptionEdit->setPlaceholderText(tr("Optional description for the notebook"));
  m_descriptionEdit->setMaximumHeight(100);
  layout->addRow(tr("Description:"), m_descriptionEdit);

  // Root folder input with browse button.
  // Get default path from session config.
  QString defaultRootPath;
  {
    auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
    defaultRootPath = sessionConfig.getNewNotebookDefaultRootFolderPath();
  }
  m_rootFolderInput = new LocationInputWithBrowseButton(mainWidget, defaultRootPath);
  m_rootFolderInput->setBrowseType(LocationInputWithBrowseButton::Folder,
                                   tr("Select Notebook Root Folder"));
  m_rootFolderInput->setPlaceholderText(tr("Select a folder as notebook root"));
  connect(m_rootFolderInput, &LocationInputWithBrowseButton::textChanged, this,
          &NewNotebookDialog2::handleRootFolderPathChanged);
  layout->addRow(tr("Root Folder:"), m_rootFolderInput);

  // Notebook type combo.
  m_typeCombo = WidgetsFactory::createComboBox(mainWidget);
  m_typeCombo->addItem(tr("Bundled Notebook"), static_cast<int>(NotebookType::Bundled));
  m_typeCombo->addItem(tr("Raw Notebook"), static_cast<int>(NotebookType::Raw));
  m_typeCombo->setToolTip(tr("Bundled: Notebook with metadata stored in config files.\n"
                             "Raw: Plain folder structure with minimal VNote metadata."));
  layout->addRow(tr("Type:"), m_typeCombo);

  // Update root folder tooltip based on selected notebook type.
  auto updateRootFolderTooltip = [this]() {
    NotebookType type = static_cast<NotebookType>(m_typeCombo->currentData().toInt());
    if (type == NotebookType::Raw) {
      m_rootFolderInput->setToolTip(
          tr("Root folder of the notebook.\n"
             "For raw notebooks, you can select an existing folder with files.\n"
             "The folder's contents will be indexed as notebook nodes."));
    } else {
      m_rootFolderInput->setToolTip(
          tr("Root folder of the notebook.\n"
             "A new notebook requires an empty folder or a non-existent path (will be created)."));
    }
  };

  connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          updateRootFolderTooltip);

  // Initialize tooltip for default type.
  updateRootFolderTooltip();

  // Sync method selection. Visible only for Bundled notebooks.
  // For Git sync, the actual remote URL + PAT are collected BEFORE notebook
  // creation via the Configure... button (pre-create flow per
  // notebook-sync-config-pre-create plan).
  m_syncMethodLabel = new QLabel(tr("Sync Method:"), mainWidget);
  m_syncMethodLabel->setObjectName(QStringLiteral("syncMethodLabel"));
  m_syncMethodCombo = WidgetsFactory::createComboBox(mainWidget);
  m_syncMethodCombo->setObjectName(QStringLiteral("syncMethodCombo"));
  m_syncMethodCombo->addItem(tr("None"), QStringLiteral("none"));
  m_syncMethodCombo->addItem(tr("Git"), QStringLiteral("git"));
  m_syncMethodCombo->setToolTip(tr("Git sync is supported only for Bundled notebooks. "
                                   "Sync settings are configured immediately via the "
                                   "Configure... button before notebook creation."));

  m_configureSyncButton = new QPushButton(tr("Configure..."), mainWidget);
  m_configureSyncButton->setObjectName(QStringLiteral("configureSyncButton"));
  m_configureSyncButton->setToolTip(tr("Configure Git sync remote URL and credentials"));
  m_configureSyncButton->hide(); // shown only when Git is the active selection

  m_syncMethodContainer = new QWidget(mainWidget);
  auto *syncMethodLayout = new QHBoxLayout(m_syncMethodContainer);
  syncMethodLayout->setContentsMargins(0, 0, 0, 0);
  syncMethodLayout->addWidget(m_syncMethodCombo, 1);
  syncMethodLayout->addWidget(m_configureSyncButton);

  layout->addRow(m_syncMethodLabel, m_syncMethodContainer);

  {
    const bool isBundled =
        m_typeCombo->currentData().toInt() == static_cast<int>(NotebookType::Bundled);
    m_syncMethodLabel->setVisible(isBundled);
    m_syncMethodContainer->setVisible(isBundled);
  }

  connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
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

  // Advanced section toggle.
  m_advancedToggle = new QToolButton(mainWidget);
  m_advancedToggle->setText(tr("Advanced"));
  m_advancedToggle->setCheckable(true);
  m_advancedToggle->setArrowType(Qt::RightArrow);
  m_advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  layout->addRow(m_advancedToggle);

  // Advanced section content.
  m_advancedSection = new QWidget(mainWidget);
  auto *advancedLayout = new QFormLayout(m_advancedSection);

  m_assetsFolderEdit = new QLineEdit(m_advancedSection);
  m_assetsFolderEdit->setText(QStringLiteral("vx_assets"));
  m_assetsFolderEdit->setToolTip(
      tr("Name or path for the assets folder.\n"
         "Can be a folder name (vx_assets), relative path, or absolute path.\n"
         "Relative paths resolve against each note file's parent directory."));
  advancedLayout->addRow(tr("Assets Folder:"), m_assetsFolderEdit);

  m_advancedSection->setVisible(false);
  layout->addRow(m_advancedSection);

  connect(m_advancedToggle, &QToolButton::toggled, this, [this](bool p_checked) {
    m_advancedSection->setVisible(p_checked);
    m_advancedToggle->setArrowType(p_checked ? Qt::DownArrow : Qt::RightArrow);
  });

  setCentralWidget(mainWidget);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("New Notebook"));

  updateOkButtonState();
}

void NewNotebookDialog2::handleRootFolderPathChanged() {
  // Auto-fill name from folder name if name is empty.
  if (m_nameEdit->text().isEmpty()) {
    QString rootPath = m_rootFolderInput->text().trimmed();
    if (!rootPath.isEmpty()) {
      m_nameEdit->setText(PathUtils::dirName(rootPath));
    }
  }
}

void NewNotebookDialog2::onTypeComboChanged(int p_index) {
  Q_UNUSED(p_index);
  if (!m_syncMethodCombo || !m_syncMethodLabel) {
    return;
  }
  const bool isBundled =
      m_typeCombo->currentData().toInt() == static_cast<int>(NotebookType::Bundled);
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
  input.name = m_nameEdit->evaluatedText();
  input.description = m_descriptionEdit->toPlainText();
  input.rootFolderPath = m_rootFolderInput->text();
  input.type = static_cast<NotebookType>(m_typeCombo->currentData().toInt());
  input.assetsFolder = m_assetsFolderEdit->text();
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
  const QString notebookName = m_nameEdit->evaluatedText().trimmed();
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
      okBtn->setToolTip(ok ? QString() : tr("Click 'Configure...' to set up Git sync first"));
    }
  }
}
