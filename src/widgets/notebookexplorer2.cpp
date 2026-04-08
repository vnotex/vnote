#include "notebookexplorer2.h"

#include <QActionGroup>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include <controllers/notebooknodecontroller.h>
#include <controllers/opennotebookcontroller.h>
#include <controllers/recyclebincontroller.h>
#include <core/configmgr2.h>
#include <core/exception.h>
#include <core/fileopensettings.h>
#include <core/global.h>
#include <core/hooknames.h>
#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/sessionconfig.h>
#include <core/widgetconfig.h>
#include <gui/services/navigationmodeservice.h>
#include <gui/services/themeservice.h>
#include <utils/fileutils.h>
#include <utils/widgetutils.h>
#include <views/combinednodeexplorer.h>
#include <views/inodeexplorer.h>
#include <views/twocolumnsnodeexplorer.h>
// TODO: Migrate dialogs to use ServiceLocator DI pattern
#include <core/services/templateservice.h>
#include <snippet/snippetmgr.h>
#include <widgets/dialogs/importfolderdialog2.h>
#include <widgets/dialogs/managenotebooksdialog2.h>
#include <widgets/dialogs/newfolderdialog2.h>
#include <widgets/dialogs/newnotebookdialog2.h>
#include <widgets/dialogs/newnotedialog2.h>
#include <widgets/dialogs/selectdialog.h>
#include <widgets/mainwindow.h>
#include <widgets/messageboxhelper.h>
#include <widgets/notebookselector2.h>
#include <widgets/titlebar.h>

using namespace vnotex;

NotebookExplorer2::NotebookExplorer2(ServiceLocator &p_services, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  setupUI();
}

NotebookExplorer2::~NotebookExplorer2() {}

void NotebookExplorer2::setupUI() {
  m_mainLayout = new QVBoxLayout(this);
  m_mainLayout->setContentsMargins(0, 0, 0, 0);
  m_mainLayout->setSpacing(0);

  // Title bar with actions and menu
  setupTitleBar();
  m_mainLayout->addWidget(m_titleBar);

  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();

  // Notebook selector
  m_notebookSelector = new NotebookSelector2(m_services, this);
  m_notebookSelector->setWhatsThis(tr("Select one of all the notebooks as current notebook.<br/>"
                                      "Move mouse on one item to check its details."));
  m_notebookSelector->setViewOrder(widgetConfig.getNotebookSelectorViewOrder());
  m_mainLayout->addWidget(m_notebookSelector);

  // Get initial explore mode from config (0=Combined, 1=TwoColumns)
  int mode = m_services.get<ConfigMgr2>()->getWidgetConfig().getNodeExplorerExploreMode();
  m_exploreMode = (mode == TwoColumns) ? TwoColumns : Combined;

  // Create the initial explorer
  if (m_exploreMode == Combined) {
    setupCombinedMode();
  } else {
    setupTwoColumnsMode();
  }

  // Connect notebook selector - activated fires on user interaction only
  // Note: We don't use currentIndexChanged because it fires during addItem() before item data is
  // set
  connect(m_notebookSelector, QOverload<int>::of(&QComboBox::activated), this, [this](int p_idx) {
    QString guid = m_notebookSelector->itemData(p_idx, NotebookGuidRole).toString();
    setCurrentNotebookInternal(guid);
  });
  connect(m_notebookSelector, &NotebookSelector2::newNotebookRequested, this,
          &NotebookExplorer2::newNotebook);

  updateFocusProxy();
}

void NotebookExplorer2::setupTitleBar() {
  m_titleBar =
      new TitleBar(m_services.get<ThemeService>(), QString(), false, TitleBar::Action::Menu, this);
  m_titleBar->setWhatsThis(
      tr("This title bar contains buttons and menu to manage notebooks and notes."));
  m_titleBar->setActionButtonsAlwaysShown(true);

  // New Notebook button
  {
    auto btn = m_titleBar->addActionButton("add.svg", tr("New Notebook"));
    connect(btn, &QToolButton::clicked, this, &NotebookExplorer2::newNotebook);
  }

  // Open/Import Notebook button
  {
    auto btn = m_titleBar->addActionButton("open_notebook.svg", tr("Open Notebook"));
    connect(btn, &QToolButton::clicked, this, &NotebookExplorer2::importNotebook);
  }

  setupTitleBarMenu();
}

void NotebookExplorer2::setupTitleBarMenu() {
  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();

  m_titleBar->addMenuAction(tr("Manage Notebooks"), m_titleBar, [this]() { manageNotebooks(); });

  m_titleBar->addMenuAction(tr("Rebuild Database"), m_titleBar, [this]() { rebuildDatabase(); });

  setupRecycleBinMenu();

  {
    m_titleBar->addMenuSeparator();
    auto notebookMenu = m_titleBar->addMenuSubMenu(tr("Notebooks View Order"));
    setupViewMenu(notebookMenu, true);

    auto nodeMenu = m_titleBar->addMenuSubMenu(tr("Notes View Order"));
    setupViewMenu(nodeMenu, false);
  }

  // External files options
  {
    m_titleBar->addMenuSeparator();
    auto showAct =
        m_titleBar->addMenuAction(tr("Show External Files"), m_titleBar, [this](bool p_checked) {
          m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerExternalFilesVisible(
              p_checked);
          // Apply to current explorer
          if (m_nodeExplorer) {
            m_nodeExplorer->setExternalNodesVisible(p_checked);
          }
        });
    showAct->setCheckable(true);
    showAct->setChecked(widgetConfig.isNodeExplorerExternalFilesVisible());

    auto importAct = m_titleBar->addMenuAction(
        tr("Import External Files on Open"), m_titleBar, [this](bool p_checked) {
          m_services.get<ConfigMgr2>()
              ->getWidgetConfig()
              .setNodeExplorerAutoImportExternalFilesEnabled(p_checked);
        });
    importAct->setCheckable(true);
    importAct->setChecked(widgetConfig.getNodeExplorerAutoImportExternalFilesEnabled());
  }

  {
    m_titleBar->addMenuSeparator();
    auto act = m_titleBar->addMenuAction(
        tr("Close File Before External Open"), m_titleBar, [this](bool p_checked) {
          m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerCloseBeforeOpenWithEnabled(
              p_checked);
        });
    act->setCheckable(true);
    act->setChecked(widgetConfig.getNodeExplorerCloseBeforeOpenWithEnabled());
  }

  {
    m_titleBar->addMenuSeparator();
    auto act = m_titleBar->addMenuAction(
        tr("Single Click Activation"), m_titleBar, [this](bool p_checked) {
          m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerSingleClickActivation(
              p_checked);
        });
    act->setCheckable(true);
    act->setChecked(widgetConfig.isNodeExplorerSingleClickActivation());
  }

  setupExploreModeMenu();
}

void NotebookExplorer2::setupRecycleBinMenu() {
  m_titleBar->addMenuSeparator();

  auto openAction = m_titleBar->addMenuAction(tr("Open Recycle Bin"), this, [this]() {
    if (m_currentNotebookId.isEmpty()) {
      return;
    }

    RecycleBinController controller(m_services);
    RecycleBinResult result = controller.prepareRecycleBinPath(m_currentNotebookId);
    if (result.success) {
      WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(result.path));
    } else {
      MessageBoxHelper::notify(MessageBoxHelper::Information, result.errorMessage, window());
    }
  });

  auto emptyAction = m_titleBar->addMenuAction(tr("Empty Recycle Bin"), this, [this]() {
    if (m_currentNotebookId.isEmpty()) {
      return;
    }

    RecycleBinController controller(m_services);

    // Check if recycle bin is supported.
    QString recycleBinPath = controller.getRecycleBinPath(m_currentNotebookId);
    if (recycleBinPath.isEmpty()) {
      MessageBoxHelper::notify(MessageBoxHelper::Information,
                               tr("Recycle bin is not supported for this notebook type."),
                               window());
      return;
    }

    // Confirmation dialog.
    QString notebookName = controller.getNotebookName(m_currentNotebookId);
    if (notebookName.isEmpty()) {
      notebookName = tr("current notebook");
    }

    int ret = MessageBoxHelper::questionOkCancel(
        MessageBoxHelper::Question,
        tr("Are you sure you want to empty the recycle bin of notebook \"%1\"?\n\n"
           "This action is irreversible.")
            .arg(notebookName),
        QString(), QString(), window());

    if (ret != QMessageBox::Ok) {
      return;
    }

    RecycleBinResult result = controller.emptyRecycleBin(m_currentNotebookId);
    if (result.success) {
      MessageBoxHelper::notify(MessageBoxHelper::Information,
                               tr("Recycle bin emptied successfully."), window());
    } else {
      MessageBoxHelper::notify(MessageBoxHelper::Warning, result.errorMessage, window());
    }
  });
}

void NotebookExplorer2::setupExploreModeMenu() {
  m_titleBar->addMenuSeparator();

  auto ag = new QActionGroup(m_titleBar);

  auto act = ag->addAction(tr("Combined View"));
  act->setCheckable(true);
  act->setData(static_cast<int>(ExploreMode::Combined));
  m_titleBar->addMenuAction(act);

  act = ag->addAction(tr("Separate View, Double Columns"));
  act->setCheckable(true);
  act->setData(static_cast<int>(ExploreMode::TwoColumns));
  m_titleBar->addMenuAction(act);

  int mode = m_services.get<ConfigMgr2>()->getWidgetConfig().getNodeExplorerExploreMode();

  for (const auto &action : ag->actions()) {
    if (action->data().toInt() == mode) {
      action->setChecked(true);
    }
  }

  connect(ag, &QActionGroup::triggered, this, [this](QAction *action) {
    int mode = action->data().toInt();
    m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerExploreMode(mode);
    setExploreMode(static_cast<ExploreMode>(mode));
  });
}

void NotebookExplorer2::setupViewMenu(QMenu *p_menu, bool p_isNotebookView) {
  auto ag = new QActionGroup(p_menu);

  auto act = ag->addAction(tr("View By Configuration"));
  act->setCheckable(true);
  act->setChecked(true);
  act->setData(ViewOrder::OrderedByConfiguration);
  p_menu->addAction(act);

  act = ag->addAction(tr("View By Name"));
  act->setCheckable(true);
  act->setData(ViewOrder::OrderedByName);
  p_menu->addAction(act);

  act = ag->addAction(tr("View By Name (Reversed)"));
  act->setCheckable(true);
  act->setData(ViewOrder::OrderedByNameReversed);
  p_menu->addAction(act);

  act = ag->addAction(tr("View By Created Time"));
  act->setCheckable(true);
  act->setData(ViewOrder::OrderedByCreatedTime);
  p_menu->addAction(act);

  act = ag->addAction(tr("View By Created Time (Reversed)"));
  act->setCheckable(true);
  act->setData(ViewOrder::OrderedByCreatedTimeReversed);
  p_menu->addAction(act);

  if (!p_isNotebookView) {
    act = ag->addAction(tr("View By Modified Time"));
    act->setCheckable(true);
    act->setData(ViewOrder::OrderedByModifiedTime);
    p_menu->addAction(act);

    act = ag->addAction(tr("View By Modified Time (Reversed)"));
    act->setCheckable(true);
    act->setData(ViewOrder::OrderedByModifiedTimeReversed);
    p_menu->addAction(act);
  }

  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();
  int viewOrder = p_isNotebookView ? widgetConfig.getNotebookSelectorViewOrder()
                                   : widgetConfig.getNodeExplorerViewOrder();
  for (const auto &action : ag->actions()) {
    if (action->data().toInt() == viewOrder) {
      action->setChecked(true);
    }
  }

  connect(ag, &QActionGroup::triggered, this, [this, p_isNotebookView](QAction *p_action) {
    const int order = p_action->data().toInt();
    if (p_isNotebookView) {
      m_services.get<ConfigMgr2>()->getWidgetConfig().setNotebookSelectorViewOrder(order);
      m_notebookSelector->setViewOrder(order);
    } else {
      m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerViewOrder(order);
      setNodeViewOrder(static_cast<ViewOrder>(order));
    }
  });
}

void NotebookExplorer2::rebuildDatabase() {
  if (m_currentNotebookId.isEmpty()) {
    return;
  }

  auto &notebookService = *m_services.get<NotebookCoreService>();

  // Get notebook name for the dialog.
  QJsonObject config = notebookService.getNotebookConfig(m_currentNotebookId);
  QString notebookName = config.value("name").toString();
  if (notebookName.isEmpty()) {
    notebookName = tr("current notebook");
  }

  // Confirmation dialog.
  int ret = MessageBoxHelper::questionOkCancel(
      MessageBoxHelper::Question,
      tr("Are you sure you want to rebuild the database for notebook \"%1\"?\n\n"
         "This will re-scan all files and rebuild the metadata cache from the filesystem.")
          .arg(notebookName),
      QString(), QString(), window());
  if (ret != QMessageBox::Ok) {
    return;
  }

  // Show progress dialog (indeterminate for sync operation).
  QProgressDialog progress(tr("Rebuilding database for \"%1\"...").arg(notebookName),
                           QString(), // No cancel button for sync operation
                           0, 0,      // Indeterminate progress
                           window());
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0); // Show immediately
  progress.show();
  QCoreApplication::processEvents(); // Ensure dialog is displayed

  // Perform the rebuild (synchronous).
  bool success = notebookService.rebuildNotebookCache(m_currentNotebookId);

  progress.close();

  // Reload the current notebook to reflect changes.
  if (m_nodeExplorer) {
    m_nodeExplorer->setNotebookId(m_currentNotebookId);
  }

  if (success) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Database rebuilt successfully for \"%1\".").arg(notebookName),
                             window());
  } else {
    MessageBoxHelper::notify(MessageBoxHelper::Warning,
                             tr("Failed to rebuild database for \"%1\".").arg(notebookName),
                             window());
  }
}

void NotebookExplorer2::setupCombinedMode() {
  // Create encapsulated MVC widget for combined mode
  auto *explorer = new CombinedNodeExplorer(m_services, this);

  // Apply initial external files visibility from config
  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();
  explorer->setExternalNodesVisible(widgetConfig.isNodeExplorerExternalFilesVisible());

  // Add to layout
  m_mainLayout->addWidget(explorer, 1);

  // Connect GUI request signals from CombinedNodeExplorer
  connect(explorer, &CombinedNodeExplorer::newNoteRequested, this,
          &NotebookExplorer2::onNewNoteRequested);
  connect(explorer, &CombinedNodeExplorer::newFolderRequested, this,
          &NotebookExplorer2::onNewFolderRequested);
  connect(explorer, &CombinedNodeExplorer::renameRequested, this,
          &NotebookExplorer2::onRenameRequested);
  connect(explorer, &CombinedNodeExplorer::deleteRequested, this,
          &NotebookExplorer2::onDeleteRequested);
  connect(explorer, &CombinedNodeExplorer::removeFromNotebookRequested, this,
          &NotebookExplorer2::onRemoveFromNotebookRequested);
  connect(explorer, &CombinedNodeExplorer::propertiesRequested, this,
          &NotebookExplorer2::onPropertiesRequested);
  connect(explorer, &CombinedNodeExplorer::errorOccurred, this,
          &NotebookExplorer2::onErrorOccurred);
  connect(explorer, &CombinedNodeExplorer::infoMessage, this, &NotebookExplorer2::onInfoMessage);
  connect(explorer, &CombinedNodeExplorer::nodeActivated, this,
          &NotebookExplorer2::onNodeActivated);
  connect(explorer, &CombinedNodeExplorer::exportNodeRequested, this,
          &NotebookExplorer2::exportNodeRequested);

  m_nodeExplorer = explorer;
}

void NotebookExplorer2::setupTwoColumnsMode() {
  auto *explorer = new TwoColumnsNodeExplorer(m_services, this);

  // Add to layout
  m_mainLayout->addWidget(explorer, 1);

  // Connect GUI request signals
  connect(explorer, &TwoColumnsNodeExplorer::newNoteRequested, this,
          &NotebookExplorer2::onNewNoteRequested);
  connect(explorer, &TwoColumnsNodeExplorer::newFolderRequested, this,
          &NotebookExplorer2::onNewFolderRequested);
  connect(explorer, &TwoColumnsNodeExplorer::renameRequested, this,
          &NotebookExplorer2::onRenameRequested);
  connect(explorer, &TwoColumnsNodeExplorer::deleteRequested, this,
          &NotebookExplorer2::onDeleteRequested);
  connect(explorer, &TwoColumnsNodeExplorer::removeFromNotebookRequested, this,
          &NotebookExplorer2::onRemoveFromNotebookRequested);
  connect(explorer, &TwoColumnsNodeExplorer::propertiesRequested, this,
          &NotebookExplorer2::onPropertiesRequested);
  connect(explorer, &TwoColumnsNodeExplorer::errorOccurred, this,
          &NotebookExplorer2::onErrorOccurred);
  connect(explorer, &TwoColumnsNodeExplorer::infoMessage, this, &NotebookExplorer2::onInfoMessage);
  connect(explorer, &TwoColumnsNodeExplorer::nodeActivated, this,
          &NotebookExplorer2::onNodeActivated);
  connect(explorer, &TwoColumnsNodeExplorer::exportNodeRequested, this,
          &NotebookExplorer2::exportNodeRequested);

  m_nodeExplorer = explorer;
}

void NotebookExplorer2::loadNotebooks() {
  if (!m_notebookSelector) {
    return;
  }

  m_notebookSelector->loadNotebooks();

  // Sync m_currentNotebookId with the selector's restored selection.
  // This must happen AFTER loadNotebooks() completes because item data
  // is only available after all items are added.
  QString selectedId = m_notebookSelector->currentNotebookId();
  if (!selectedId.isEmpty()) {
    setCurrentNotebookInternal(selectedId);
  }
}

void NotebookExplorer2::setCurrentNotebook(const QString &p_notebookId) {
  if (m_currentNotebookId == p_notebookId) {
    return;
  }

  m_notebookSelector->setCurrentNotebook(p_notebookId);
  setCurrentNotebookInternal(p_notebookId);
}

void NotebookExplorer2::setCurrentNotebookInternal(const QString &p_notebookId) {
  m_currentNotebookId = p_notebookId;

  // Update current explorer
  if (m_nodeExplorer) {
    m_nodeExplorer->setNotebookId(p_notebookId);
  }

  emit currentNotebookChanged(p_notebookId);
  emit currentExploredFolderChanged(currentExploredFolderId());
}

QString NotebookExplorer2::currentNotebookId() const { return m_currentNotebookId; }

void NotebookExplorer2::setCurrentNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid() || !m_nodeExplorer) {
    return;
  }

  m_nodeExplorer->expandToNode(p_nodeId);
  m_nodeExplorer->selectNode(p_nodeId);
  m_nodeExplorer->scrollToNode(p_nodeId);
}

void NotebookExplorer2::locateNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Switch to the node's notebook if not already showing it.
  if (m_currentNotebookId != p_nodeId.notebookId) {
    setCurrentNotebook(p_nodeId.notebookId);
  }

  setCurrentNode(p_nodeId);
}

NodeIdentifier NotebookExplorer2::currentNodeId() const {
  return m_nodeExplorer ? m_nodeExplorer->currentNodeId() : NodeIdentifier();
}

NotebookSelector2 *NotebookExplorer2::getNotebookSelector() const { return m_notebookSelector; }

INodeExplorer *NotebookExplorer2::getNodeExplorer() const { return m_nodeExplorer; }

void NotebookExplorer2::setExploreMode(ExploreMode p_mode) {
  if (m_exploreMode == p_mode) {
    return;
  }

  m_exploreMode = p_mode;
  updateExploreMode();
}

NotebookExplorer2::ExploreMode NotebookExplorer2::exploreMode() const { return m_exploreMode; }

void NotebookExplorer2::updateExploreMode() {
  // Unregister old navigation wrapper before deleting explorer (prevents dangling pointer)
  auto *navService = m_services.get<NavigationModeService>();
  if (navService && m_nodeExplorer) {
    if (auto *oldWrapper = m_nodeExplorer->getNavigationModeWrapper()) {
      navService->unregisterNavigationTarget(oldWrapper);
    }
  }

  // Delete old explorer and create new one for the current mode
  m_mainLayout->removeWidget(m_nodeExplorer);
  delete m_nodeExplorer;

  if (m_exploreMode == Combined) {
    setupCombinedMode();
  } else {
    setupTwoColumnsMode();
  }

  // Register new navigation wrapper
  if (navService && m_nodeExplorer) {
    if (auto *newWrapper = m_nodeExplorer->getNavigationModeWrapper()) {
      navService->registerNavigationTarget(newWrapper);
    }
  }

  // Sync notebook to the explorer
  if (!m_currentNotebookId.isEmpty()) {
    m_nodeExplorer->setNotebookId(m_currentNotebookId);
  }

  updateFocusProxy();
}

void NotebookExplorer2::updateFocusProxy() { setFocusProxy(m_nodeExplorer); }

// --- Public Slots Implementation ---

void NotebookExplorer2::newNotebook() {
  // Use window() to get top-level MainWindow2 for dialog centering.
  NewNotebookDialog2 dialog(m_services, window());
  if (dialog.exec() == QDialog::Accepted) {
    // Reload notebooks to show the newly created one
    loadNotebooks();
  }
}

void NotebookExplorer2::newNotebookFromFolder() {
  // TODO: Migrate NewNotebookFromFolderDialog to use ServiceLocator DI pattern
  MessageBoxHelper::notify(
      MessageBoxHelper::Information,
      tr("New notebook from folder dialog is being migrated to use dependency injection."),
      window());
}

void NotebookExplorer2::importNotebook() {
  QString rootFolder = QFileDialog::getExistingDirectory(
      window(), tr("Select Notebook Root Folder"), QDir::homePath(),
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

  if (rootFolder.isEmpty()) {
    return;
  }

  OpenNotebookController controller(m_services);

  // Validate the selected folder.
  auto validation = controller.validateRootFolder(rootFolder);
  if (!validation.valid) {
    MessageBoxHelper::notify(MessageBoxHelper::Warning, validation.message, window());
    return;
  }

  // Open the notebook.
  OpenNotebookInput input;
  input.rootFolderPath = rootFolder;

  OpenNotebookResult result = controller.openNotebook(input);
  if (!result.success) {
    MessageBoxHelper::notify(MessageBoxHelper::Critical, result.errorMessage, window());
    return;
  }

  // Reload notebooks and select the newly-opened one.
  if (m_notebookSelector) {
    m_notebookSelector->loadNotebooks();
    setCurrentNotebook(result.notebookId);
  }
}

void NotebookExplorer2::manageNotebooks() {
  QString currentNotebookId;
  if (m_notebookSelector) {
    currentNotebookId = m_notebookSelector->currentNotebookId();
  }

  ManageNotebooksDialog2 dialog(m_services, currentNotebookId, window());
  dialog.exec();

  // Reload notebooks after dialog closes (changes may have occurred).
  if (m_notebookSelector) {
    m_notebookSelector->loadNotebooks();
  }
}

void NotebookExplorer2::newFolder() {
  NodeIdentifier parentId = currentExploredFolderId();
  if (!parentId.isValid()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Please first create a notebook to hold your data."), window());
    return;
  }

  // Delegate to the shared handler
  onNewFolderRequested(parentId);
}

void NotebookExplorer2::newNote() {
  NodeIdentifier parentId = currentExploredFolderId();
  if (!parentId.isValid()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Please first create a notebook to hold your data."), window());
    return;
  }

  // Delegate to the shared handler
  onNewNoteRequested(parentId);
}

void NotebookExplorer2::newQuickNote() {
  // Get quick note schemes from session config.
  auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
  const auto &schemes = sessionConfig.getQuickNoteSchemes();
  if (schemes.isEmpty()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Please set up quick note schemes in the Settings dialog first."),
                             window());
    return;
  }

  // Show selection dialog.
  SelectDialog dialog(tr("New Quick Note"), window());
  for (int i = 0; i < schemes.size(); ++i) {
    dialog.addSelection(schemes[i].m_name, i);
  }

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  int selection = dialog.getSelection();
  const auto &scheme = schemes[selection];

  // Resolve target folder.
  QString notebookId = m_currentNotebookId;
  QString folderPath;

  if (!scheme.m_folderPath.isEmpty()) {
    // Try to resolve the scheme folder path to a notebook.
    auto &notebookService = *m_services.get<NotebookCoreService>();
    QJsonObject resolved = notebookService.resolvePathToNotebook(scheme.m_folderPath);
    if (!resolved.isEmpty()) {
      notebookId = resolved[QStringLiteral("notebookId")].toString();
      folderPath = resolved[QStringLiteral("relativePath")].toString();
    } else {
      // Path not found in any notebook - use scheme.m_folderPath as-is if current notebook is
      // valid.
      if (notebookId.isEmpty()) {
        MessageBoxHelper::notify(
            MessageBoxHelper::Information,
            tr("The quick note folder path (%1) is not within any open notebook.")
                .arg(scheme.m_folderPath),
            window());
        return;
      }
      // Assume relative path within current notebook.
      folderPath = scheme.m_folderPath;
    }
  } else {
    // Use current explored folder.
    NodeIdentifier parentId = currentExploredFolderId();
    if (parentId.isValid()) {
      notebookId = parentId.notebookId;
      folderPath = parentId.relativePath;
    }
  }

  if (notebookId.isEmpty()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("The quick note should be created within a notebook."), window());
    return;
  }

  // Generate filename using snippet expansion.
  QString expandedName = SnippetMgr::getInst().applySnippetBySymbol(scheme.m_noteName);
  QFileInfo finfo(expandedName);

  // Get notebook root path to generate unique filename.
  auto &notebookService = *m_services.get<NotebookCoreService>();
  QJsonObject notebookConfig = notebookService.getNotebookConfig(notebookId);
  QString rootFolder = notebookConfig[QStringLiteral("rootFolder")].toString();
  QString parentAbsPath = folderPath.isEmpty() ? rootFolder : QDir(rootFolder).filePath(folderPath);

  QString newFileName = FileUtils::generateFileNameWithSequence(
      parentAbsPath, finfo.completeBaseName(), finfo.suffix());

  // Get template content if specified.
  QString templateContent;
  if (!scheme.m_template.isEmpty()) {
    auto &templateService = *m_services.get<TemplateService>();
    templateContent = templateService.getTemplateContent(scheme.m_template);
  }

  // Create the file via NotebookService.
  QString fileId = notebookService.createFile(notebookId, folderPath, newFileName);
  if (fileId.isEmpty()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Failed to create quick note from scheme (%1).").arg(scheme.m_name),
                             window());
    return;
  }

  // Write template content if present.
  if (!templateContent.isEmpty()) {
    QString filePath = QDir(parentAbsPath).filePath(newFileName);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      file.write(templateContent.toUtf8());
      file.close();
    }
  }

  NodeIdentifier newNodeId;
  newNodeId.notebookId = notebookId;
  newNodeId.relativePath =
      folderPath.isEmpty() ? newFileName : folderPath + QStringLiteral("/") + newFileName;

  auto *bufferSvc = m_services.get<BufferService>();
  if (bufferSvc) {
    FileOpenSettings settings;
    settings.m_mode = ViewWindowMode::Edit;
    settings.m_forceMode = true;
    settings.m_newFile = true;
    bufferSvc->openBuffer(newNodeId, settings);
  }
}

void NotebookExplorer2::importFile() {
  NodeIdentifier folderId = currentExploredFolderId();
  if (!folderId.isValid()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Please first create a notebook to hold your data."), window());
    return;
  }

  onImportFilesRequested(folderId);
}

void NotebookExplorer2::importFolder() {
  NodeIdentifier folderId = currentExploredFolderId();
  if (!folderId.isValid()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Please first create a notebook to hold your data."), window());
    return;
  }

  onImportFolderRequested(folderId);
}

NodeIdentifier NotebookExplorer2::currentExploredFolderId() const {
  NodeIdentifier nodeId = currentNodeId();
  if (!nodeId.isValid()) {
    // Return notebook root if no selection
    if (!m_currentNotebookId.isEmpty()) {
      NodeIdentifier rootId;
      rootId.notebookId = m_currentNotebookId;
      rootId.relativePath = QString();
      return rootId;
    }
    return NodeIdentifier();
  }

  // Check if it's a folder
  NodeInfo info;
  if (m_nodeExplorer) {
    info = m_nodeExplorer->getNodeInfo(nodeId);
  }

  if (info.isFolder && !info.isExternal) {
    return nodeId;
  }

  // Return parent folder for file nodes
  NodeIdentifier parentId;
  parentId.notebookId = nodeId.notebookId;
  parentId.relativePath = nodeId.parentPath();
  return parentId;
}

NodeIdentifier NotebookExplorer2::currentExploredNodeId() const { return currentNodeId(); }

QByteArray NotebookExplorer2::saveState() const {
  // Save current node path and view state
  QByteArray state;
  QDataStream stream(&state, QIODevice::WriteOnly);

  // Save explore mode
  stream << static_cast<int>(m_exploreMode);

  // Save splitter sizes if in TwoColumns mode
  auto *twoColumns = qobject_cast<TwoColumnsNodeExplorer *>(m_nodeExplorer);
  if (twoColumns) {
    stream << twoColumns->saveSplitterState();
  } else {
    stream << QByteArray();
  }

  return state;
}

void NotebookExplorer2::restoreState(const QByteArray &p_data) {
  if (p_data.isEmpty()) {
    return;
  }

  QDataStream stream(p_data);

  int mode;
  stream >> mode;
  setExploreMode(static_cast<ExploreMode>(mode));

  QByteArray splitterState;
  stream >> splitterState;
  if (!splitterState.isEmpty()) {
    auto *twoCol = qobject_cast<TwoColumnsNodeExplorer *>(m_nodeExplorer);
    if (twoCol) {
      twoCol->restoreSplitterState(splitterState);
    }
  }
}

void NotebookExplorer2::setNodeViewOrder(ViewOrder p_order) {
  m_nodeExplorer->setViewOrder(p_order);
}

// --- GUI request handlers from controller signals ---

void NotebookExplorer2::onNewNoteRequested(const NodeIdentifier &p_parentId) {
  NewNoteDialog2 dialog(m_services, p_parentId, window());
  if (dialog.exec() == QDialog::Accepted) {
    NodeIdentifier newNodeId = dialog.getNewNodeId();
    if (newNodeId.isValid()) {
      m_nodeExplorer->reloadNode(p_parentId);
      setCurrentNode(newNodeId);

      auto *bufferSvc = m_services.get<BufferService>();
      if (bufferSvc) {
        FileOpenSettings settings;
        settings.m_mode = ViewWindowMode::Edit;
        settings.m_forceMode = true;
        settings.m_newFile = true;
        bufferSvc->openBuffer(newNodeId, settings);
      }
    }
  }
}

void NotebookExplorer2::onNewFolderRequested(const NodeIdentifier &p_parentId) {
  NewFolderDialog2 dialog(m_services, p_parentId, window());
  if (dialog.exec() == QDialog::Accepted) {
    NodeIdentifier newNodeId = dialog.getNewNodeId();
    if (newNodeId.isValid()) {
      m_nodeExplorer->reloadNode(p_parentId);
      setCurrentNode(newNodeId);
    }
  }
}

void NotebookExplorer2::onRenameRequested(const NodeIdentifier &p_nodeId,
                                          const QString &p_currentName) {
  Q_UNUSED(p_currentName);
  m_nodeExplorer->startInlineRename(p_nodeId);
}

void NotebookExplorer2::onDeleteRequested(const QList<NodeIdentifier> &p_nodeIds,
                                          bool p_permanent) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  QString title = p_permanent ? tr("Delete Permanently") : tr("Delete");
  QString message;

  if (p_permanent) {
    message = tr("Permanently delete %n node(s)? This cannot be undone.", "", p_nodeIds.size());
  } else {
    message = tr("Move %n node(s) to recycle bin?", "", p_nodeIds.size());
  }

  int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Question, title, message,
                                               QString(), window());
  if (ret != QMessageBox::Ok) {
    return;
  }

  // Use unified interface to perform the delete
  m_nodeExplorer->handleDeleteConfirmed(p_nodeIds, p_permanent);
}

void NotebookExplorer2::onRemoveFromNotebookRequested(const QList<NodeIdentifier> &p_nodeIds) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  QString message =
      tr("Remove %n node(s) from notebook index? Files will remain on disk.", "", p_nodeIds.size());

  int ret = MessageBoxHelper::questionOkCancel(
      MessageBoxHelper::Question, tr("Remove From Notebook"), message, QString(), window());
  if (ret != QMessageBox::Ok) {
    return;
  }

  // Use unified interface to perform the removal
  m_nodeExplorer->handleRemoveConfirmed(p_nodeIds);
}

void NotebookExplorer2::onImportFilesRequested(const NodeIdentifier &p_targetFolderId) {
  QStringList files =
      QFileDialog::getOpenFileNames(window(), tr("Import Files"), QString(), tr("All Files (*)"));
  if (files.isEmpty()) {
    return;
  }

  // Use NotebookService to perform the import, then reload
  auto &notebookService = *m_services.get<NotebookCoreService>();
  for (const QString &filePath : files) {
    QFileInfo fileInfo(filePath);
    QString destPath = p_targetFolderId.relativePath.isEmpty()
                           ? fileInfo.fileName()
                           : p_targetFolderId.relativePath + "/" + fileInfo.fileName();
    notebookService.importFile(p_targetFolderId.notebookId, filePath, destPath);
  }
  // Reload the folder to show imported files
  m_nodeExplorer->reloadNode(p_targetFolderId);
}

void NotebookExplorer2::onImportFolderRequested(const NodeIdentifier &p_targetFolderId) {
  ImportFolderDialog2 dialog(m_services, p_targetFolderId, window());
  if (dialog.exec() == QDialog::Accepted) {
    NodeIdentifier newNodeId = dialog.getNewNodeId();
    if (newNodeId.isValid()) {
      m_nodeExplorer->reloadNode(p_targetFolderId);
      setCurrentNode(newNodeId);
    }
  }
}

void NotebookExplorer2::onPropertiesRequested(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Get node info using unified interface
  NodeInfo nodeInfo = m_nodeExplorer->getNodeInfo(p_nodeId);

  if (!nodeInfo.isValid()) {
    return;
  }

  // Build absolute path
  QString absolutePath;
  auto *notebookService = m_services.get<NotebookCoreService>();
  QJsonObject config = notebookService->getNotebookConfig(p_nodeId.notebookId);
  QString rootPath = config.value(QStringLiteral("rootFolder")).toString();
  if (!rootPath.isEmpty()) {
    absolutePath =
        p_nodeId.relativePath.isEmpty() ? rootPath : rootPath + "/" + p_nodeId.relativePath;
  }

  QString info;
  info += tr("Name: %1\n").arg(nodeInfo.name);
  info += tr("Path: %1\n").arg(absolutePath);
  info += tr("Type: %1\n").arg(nodeInfo.isFolder ? tr("Folder") : tr("Note"));
  info += tr("Created: %1\n").arg(nodeInfo.createdTimeUtc.toLocalTime().toString());
  info += tr("Modified: %1\n").arg(nodeInfo.modifiedTimeUtc.toLocalTime().toString());

  if (nodeInfo.isFolder) {
    info += tr("Children: %1\n").arg(nodeInfo.childCount);
  }

  if (!nodeInfo.tags.isEmpty()) {
    info += tr("Tags: %1\n").arg(nodeInfo.tags.join(", "));
  }

  MessageBoxHelper::notify(MessageBoxHelper::Information, info, window());
}

void NotebookExplorer2::onErrorOccurred(const QString &p_title, const QString &p_message) {
  MessageBoxHelper::notify(MessageBoxHelper::Critical, p_title + ": " + p_message, window());
}

void NotebookExplorer2::onInfoMessage(const QString &p_title, const QString &p_message) {
  MessageBoxHelper::notify(MessageBoxHelper::Information, p_title + ": " + p_message, window());
}

void NotebookExplorer2::onNodeActivated(const NodeIdentifier &p_nodeId,
                                        const FileOpenSettings &p_settings) {
  auto *bufferSvc = m_services.get<BufferService>();
  if (!bufferSvc) {
    return;
  }

  bufferSvc->openBuffer(p_nodeId, p_settings);

  emit currentExploredFolderChanged(currentExploredFolderId());
}
