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
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <controllers/notebooknodecontroller.h>
#include <controllers/opennotebookcontroller.h>
#include <controllers/recyclebincontroller.h>
#include <core/configmgr2.h>
#include <core/exception.h>
#include <core/fileopenparameters.h>
#include <core/global.h>
#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <core/services/notebookservice.h>
#include <core/sessionconfig.h>
#include <core/widgetconfig.h>
#include <gui/services/themeservice.h>
#include <models/notebooknodemodel.h>
#include <models/notebooknodeproxymodel.h>
#include <utils/fileutils.h>
#include <utils/widgetutils.h>
#include <views/notebooknodedelegate.h>
#include <views/notebooknodeview.h>
// TODO: Migrate dialogs to use ServiceLocator DI pattern
// #include <widgets/dialogs/importfolderdialog.h>
// #include <widgets/dialogs/importnotebookdialog.h>
// #include <widgets/dialogs/managenotebooksdialog.h>
// #include <widgets/dialogs/newfolderdialog.h>
// #include <widgets/dialogs/newnotebookdialog.h>
// #include <widgets/dialogs/newnotebookfromfolderdialog.h>
// #include <widgets/dialogs/newnotedialog.h>
#include <widgets/dialogs/newnotebookdialog2.h>
#include <widgets/dialogs/managenotebooksdialog2.h>
#include <widgets/dialogs/newfolderdialog2.h>
#include <widgets/dialogs/newnotedialog2.h>
#include <widgets/dialogs/selectdialog.h>
#include <widgets/mainwindow.h>
#include <widgets/messageboxhelper.h>
// TODO: Migrate NavigationModeMgr to use ServiceLocator DI pattern
// #include <widgets/navigationmodemgr.h>
#include <widgets/notebookselector2.h>
#include <widgets/titlebar.h>
#include <core/services/templateservice.h>
#include <snippet/snippetmgr.h>

using namespace vnotex;

NotebookExplorer2::NotebookExplorer2(ServiceLocator &p_services, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  setupUI();
}

NotebookExplorer2::~NotebookExplorer2() {}

void NotebookExplorer2::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Title bar with actions and menu
  setupTitleBar();
  mainLayout->addWidget(m_titleBar);

  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();

  // Notebook selector
  m_notebookSelector = new NotebookSelector2(m_services, this);
  m_notebookSelector->setWhatsThis(tr("Select one of all the notebooks as current notebook.<br/>"
                                      "Move mouse on one item to check its details."));
  // TODO: Migrate NavigationModeMgr to use ServiceLocator DI pattern
  // NavigationModeMgr::getInst().registerNavigationTarget(m_notebookSelector);
  m_notebookSelector->setViewOrder(widgetConfig.getNotebookSelectorViewOrder());
  mainLayout->addWidget(m_notebookSelector);

  // Content stack for different modes
  m_contentStack = new QStackedWidget(this);
  mainLayout->addWidget(m_contentStack, 1);

  // Get initial explore mode from config
  int mode = m_services.get<ConfigMgr2>()->getWidgetConfig().getNodeExplorerExploreMode();
  // Map old NotebookNodeExplorer modes (0=Combined, 1=SeparateSingle, 2=SeparateDouble)
  // to new modes (0=Combined, 1=TwoColumns)
  m_exploreMode = (mode >= 1) ? TwoColumns : Combined;

  // Create only the initial mode's view (lazy initialization)
  if (m_exploreMode == Combined) {
    setupCombinedMode();
  } else {
    setupTwoColumnsMode();
  }

  // Connect notebook selector - activated fires on user interaction only
  // Note: We don't use currentIndexChanged because it fires during addItem() before item data is set
  connect(m_notebookSelector, QOverload<int>::of(&QComboBox::activated), this, [this](int p_idx) {
    QString guid = m_notebookSelector->itemData(p_idx, NotebookGuidRole).toString();
    setCurrentNotebookInternal(guid);
    emit notebookActivated(guid);
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

  // External files options - Note: NotebookExplorer2 doesn't have external file support yet
  // These are kept for future implementation
  {
    m_titleBar->addMenuSeparator();
    auto showAct = m_titleBar->addMenuAction(tr("Show External Files"), m_titleBar,
                                             [this](bool p_checked) {
                                               m_services.get<ConfigMgr2>()
                                                   ->getWidgetConfig()
                                                   .setNodeExplorerExternalFilesVisible(p_checked);
                                               // TODO: Apply to views when external file support is added
                                             });
    showAct->setCheckable(true);
    showAct->setChecked(widgetConfig.isNodeExplorerExternalFilesVisible());

    auto importAct =
        m_titleBar->addMenuAction(tr("Import External Files on Open"), m_titleBar,
                                  [this](bool p_checked) {
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
  // Map old NotebookNodeExplorer modes to new modes
  // Combined = 0, SeparateSingle = 1, SeparateDouble = 2
  // Our modes: Combined = 0, TwoColumns = 1
  ExploreMode mappedMode = Combined;
  if (mode >= 1) {
    mappedMode = TwoColumns;
  }

  for (const auto &action : ag->actions()) {
    if (action->data().toInt() == static_cast<int>(mappedMode)) {
      action->setChecked(true);
    }
  }

  connect(ag, &QActionGroup::triggered, this, [this](QAction *action) {
    int mode = action->data().toInt();
    // Store as compatible value for old config
    m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerExploreMode(mode == 0 ? 0 : 2);
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

  auto &notebookService = *m_services.get<NotebookService>();

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
      QString(),
      QString(),
      window());
  if (ret != QMessageBox::Ok) {
    return;
  }

  // Show progress dialog (indeterminate for sync operation).
  QProgressDialog progress(tr("Rebuilding database for \"%1\"...").arg(notebookName),
                           QString(),  // No cancel button for sync operation
                           0, 0,       // Indeterminate progress
                           window());
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);  // Show immediately
  progress.show();
  QCoreApplication::processEvents();  // Ensure dialog is displayed

  // Perform the rebuild (synchronous).
  notebookService.rebuildNotebookCache(m_currentNotebookId);

  progress.close();

  // Reload the current notebook to reflect changes.
  if (m_combinedModel) {
    m_combinedModel->setNotebookId(m_currentNotebookId);
  }
  if (m_folderModel) {
    m_folderModel->setNotebookId(m_currentNotebookId);
  }

  MessageBoxHelper::notify(MessageBoxHelper::Information,
                           tr("Database rebuilt successfully for \"%1\".").arg(notebookName),
                           window());
}

void NotebookExplorer2::setupCombinedMode() {
  // Create MVC components for combined mode
  m_combinedModel = new NotebookNodeModel(m_services, this);
  m_combinedProxyModel = new NotebookNodeProxyModel(this);
  m_combinedProxyModel->setSourceModel(m_combinedModel);
  m_combinedProxyModel->setFilterFlags(NotebookNodeProxyModel::ShowAll);

  // Apply initial view order from config
  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();
  m_combinedProxyModel->setViewOrder(static_cast<ViewOrder>(widgetConfig.getNodeExplorerViewOrder()));
  m_combinedProxyModel->sort(0); // Enable sorting

  m_combinedView = new NotebookNodeView(this);
  m_combinedView->setModel(m_combinedProxyModel);

  m_combinedDelegate = new NotebookNodeDelegate(m_services, this);
  m_combinedView->setItemDelegate(m_combinedDelegate);

  m_combinedController = new NotebookNodeController(m_services, this);
  m_combinedController->setModel(m_combinedModel);
  m_combinedController->setView(m_combinedView);
  m_combinedView->setController(m_combinedController);

  // Add to content stack
  m_contentStack->addWidget(m_combinedView);

  // Connect signals from view
  connect(m_combinedView, &NotebookNodeView::nodeActivated, this,
          &NotebookExplorer2::onNodeActivated);
  connect(m_combinedView, &NotebookNodeView::contextMenuRequested, this,
          &NotebookExplorer2::onContextMenuRequested);

  // Forward controller signals (already using NodeIdentifier)
  connect(m_combinedController, &NotebookNodeController::nodeActivated, this,
          &NotebookExplorer2::nodeActivated);
  connect(m_combinedController, &NotebookNodeController::fileActivated, this,
          &NotebookExplorer2::fileActivated);
  connect(m_combinedController, &NotebookNodeController::nodeAboutToMove, this,
          &NotebookExplorer2::nodeAboutToMove);
  connect(m_combinedController, &NotebookNodeController::nodeAboutToRemove, this,
          &NotebookExplorer2::nodeAboutToRemove);
  connect(m_combinedController, &NotebookNodeController::nodeAboutToReload, this,
          &NotebookExplorer2::nodeAboutToReload);
  connect(m_combinedController, &NotebookNodeController::closeFileRequested, this,
          &NotebookExplorer2::closeFileRequested);

  // Connect GUI request signals from controller
  connect(m_combinedController, &NotebookNodeController::newNoteRequested, this,
          &NotebookExplorer2::onNewNoteRequested);
  connect(m_combinedController, &NotebookNodeController::newFolderRequested, this,
          &NotebookExplorer2::onNewFolderRequested);
  connect(m_combinedController, &NotebookNodeController::renameRequested, this,
          &NotebookExplorer2::onRenameRequested);
  connect(m_combinedController, &NotebookNodeController::deleteRequested, this,
          &NotebookExplorer2::onDeleteRequested);
  connect(m_combinedController, &NotebookNodeController::removeFromNotebookRequested, this,
          &NotebookExplorer2::onRemoveFromNotebookRequested);
  connect(m_combinedController, &NotebookNodeController::propertiesRequested, this,
          &NotebookExplorer2::onPropertiesRequested);
  connect(m_combinedController, &NotebookNodeController::errorOccurred, this,
          &NotebookExplorer2::onErrorOccurred);
  connect(m_combinedController, &NotebookNodeController::infoMessage, this,
          &NotebookExplorer2::onInfoMessage);
}

void NotebookExplorer2::setupTwoColumnsMode() {
  // Create splitter for two columns
  m_twoColumnsSplitter = new QSplitter(Qt::Horizontal, this);

  // Folder view (left) - shows only folders
  m_folderModel = new NotebookNodeModel(m_services, this);
  m_folderProxyModel = new NotebookNodeProxyModel(this);
  m_folderProxyModel->setSourceModel(m_folderModel);
  m_folderProxyModel->setFilterFlags(NotebookNodeProxyModel::ShowFolders);

  // Apply initial view order from config
  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();
  ViewOrder viewOrder = static_cast<ViewOrder>(widgetConfig.getNodeExplorerViewOrder());
  m_folderProxyModel->setViewOrder(viewOrder);
  m_folderProxyModel->sort(0); // Enable sorting

  m_folderView = new NotebookNodeView(this);
  m_folderView->setModel(m_folderProxyModel);

  m_folderDelegate = new NotebookNodeDelegate(m_services, this);
  m_folderView->setItemDelegate(m_folderDelegate);

  m_folderController = new NotebookNodeController(m_services, this);
  m_folderController->setModel(m_folderModel);
  m_folderController->setView(m_folderView);
  m_folderView->setController(m_folderController);

  m_twoColumnsSplitter->addWidget(m_folderView);

  // File view (right) - shows files in selected folder
  m_fileModel = new NotebookNodeModel(m_services, this);
  m_fileProxyModel = new NotebookNodeProxyModel(this);
  m_fileProxyModel->setSourceModel(m_fileModel);
  m_fileProxyModel->setFilterFlags(NotebookNodeProxyModel::ShowNotes);

  m_fileProxyModel->setViewOrder(viewOrder);
  m_fileProxyModel->sort(0); // Enable sorting

  m_fileView = new NotebookNodeView(this);
  m_fileView->setModel(m_fileProxyModel);

  m_fileDelegate = new NotebookNodeDelegate(m_services, this);
  m_fileDelegate->setShowChildCount(false); // Files don't have children
  m_fileView->setItemDelegate(m_fileDelegate);

  m_fileController = new NotebookNodeController(m_services, this);
  m_fileController->setModel(m_fileModel);
  m_fileController->setView(m_fileView);
  m_fileView->setController(m_fileController);

  m_twoColumnsSplitter->addWidget(m_fileView);

  // Set initial splitter sizes
  m_twoColumnsSplitter->setSizes(QList<int>() << 200 << 300);

  // Add to content stack
  m_contentStack->addWidget(m_twoColumnsSplitter);

  // Connect folder selection to update file view
  connect(m_folderView, &NotebookNodeView::nodeSelectionChanged, this,
          &NotebookExplorer2::onFolderSelectionChanged);

  // Connect file view signals
  connect(m_fileView, &NotebookNodeView::nodeActivated, this, &NotebookExplorer2::onNodeActivated);
  connect(m_fileView, &NotebookNodeView::contextMenuRequested, this,
          &NotebookExplorer2::onContextMenuRequested);

  // Forward controller signals from both controllers (already using NodeIdentifier)
  auto connectControllerSignals = [this](NotebookNodeController *controller) {
    connect(controller, &NotebookNodeController::nodeActivated, this,
            &NotebookExplorer2::nodeActivated);
    connect(controller, &NotebookNodeController::fileActivated, this,
            &NotebookExplorer2::fileActivated);
    connect(controller, &NotebookNodeController::nodeAboutToMove, this,
            &NotebookExplorer2::nodeAboutToMove);
    connect(controller, &NotebookNodeController::nodeAboutToRemove, this,
            &NotebookExplorer2::nodeAboutToRemove);
    connect(controller, &NotebookNodeController::nodeAboutToReload, this,
            &NotebookExplorer2::nodeAboutToReload);
    connect(controller, &NotebookNodeController::closeFileRequested, this,
            &NotebookExplorer2::closeFileRequested);

    // Connect GUI request signals from controller
    connect(controller, &NotebookNodeController::newNoteRequested, this,
            &NotebookExplorer2::onNewNoteRequested);
    connect(controller, &NotebookNodeController::newFolderRequested, this,
            &NotebookExplorer2::onNewFolderRequested);
    connect(controller, &NotebookNodeController::renameRequested, this,
            &NotebookExplorer2::onRenameRequested);
    connect(controller, &NotebookNodeController::deleteRequested, this,
            &NotebookExplorer2::onDeleteRequested);
    connect(controller, &NotebookNodeController::removeFromNotebookRequested, this,
            &NotebookExplorer2::onRemoveFromNotebookRequested);
    connect(controller, &NotebookNodeController::propertiesRequested, this,
            &NotebookExplorer2::onPropertiesRequested);
    connect(controller, &NotebookNodeController::errorOccurred, this,
            &NotebookExplorer2::onErrorOccurred);
    connect(controller, &NotebookNodeController::infoMessage, this,
            &NotebookExplorer2::onInfoMessage);
  };

  connectControllerSignals(m_folderController);
  connectControllerSignals(m_fileController);
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

  // Update all models that exist (views are created on-demand)
  if (m_combinedModel) {
    m_combinedModel->setNotebookId(p_notebookId);
  }

  if (m_folderModel) {
    m_folderModel->setNotebookId(p_notebookId);
    // File model will be updated when folder is selected
  }
}

QString NotebookExplorer2::currentNotebookId() const {
  return m_currentNotebookId;
}

void NotebookExplorer2::setCurrentNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  if (m_exploreMode == Combined) {
    if (m_combinedView) {
      m_combinedView->expandToNode(p_nodeId);
      m_combinedView->selectNode(p_nodeId);
      m_combinedView->scrollToNode(p_nodeId);
    }
  } else {
    // TwoColumns mode
    if (!m_folderView || !m_fileView) {
      return;
    }

    // Get node info to check if it's a folder
    NodeInfo info;
    if (m_folderModel) {
      info = m_folderModel->nodeInfoFromIndex(m_folderModel->indexFromNodeId(p_nodeId));
    }

    if (info.isFolder) {
      m_folderView->expandToNode(p_nodeId);
      m_folderView->selectNode(p_nodeId);
    } else {
      // Select parent folder in folder view
      NodeIdentifier parentId = p_nodeId;
      parentId.relativePath = p_nodeId.parentPath();

      if (parentId.isValid()) {
        m_folderView->expandToNode(parentId);
        m_folderView->selectNode(parentId);
      }
      // Select file in file view
      m_fileView->selectNode(p_nodeId);
    }
  }
}

NodeIdentifier NotebookExplorer2::currentNodeId() const {
  if (m_exploreMode == Combined) {
    return m_combinedView ? m_combinedView->currentNodeId() : NodeIdentifier();
  } else {
    // In two columns mode, prefer file selection over folder
    if (m_fileView) {
      NodeIdentifier fileNodeId = m_fileView->currentNodeId();
      if (fileNodeId.isValid()) {
        return fileNodeId;
      }
    }
    return m_folderView ? m_folderView->currentNodeId() : NodeIdentifier();
  }
}

void NotebookExplorer2::setExploreMode(ExploreMode p_mode) {
  if (m_exploreMode == p_mode) {
    return;
  }

  m_exploreMode = p_mode;
  updateExploreMode();
}

NotebookExplorer2::ExploreMode NotebookExplorer2::exploreMode() const {
  return m_exploreMode;
}

void NotebookExplorer2::updateExploreMode() {
  if (m_exploreMode == Combined) {
    // Ensure combined mode is set up (lazy creation)
    if (!m_combinedView) {
      setupCombinedMode();
    }
    m_contentStack->setCurrentWidget(m_combinedView);
    // Sync notebook to the view if needed
    if (!m_currentNotebookId.isEmpty() && m_combinedModel) {
      m_combinedModel->setNotebookId(m_currentNotebookId);
    }
  } else {
    // Ensure two columns mode is set up (lazy creation)
    if (!m_twoColumnsSplitter) {
      setupTwoColumnsMode();
    }
    m_contentStack->setCurrentWidget(m_twoColumnsSplitter);
    // Sync notebook to the folder view if needed
    if (!m_currentNotebookId.isEmpty() && m_folderModel) {
      m_folderModel->setNotebookId(m_currentNotebookId);
    }
  }
  updateFocusProxy();
}

void NotebookExplorer2::updateFocusProxy() {
  if (m_exploreMode == Combined && m_combinedView) {
    setFocusProxy(m_combinedView);
  } else if (m_exploreMode == TwoColumns && m_folderView) {
    setFocusProxy(m_folderView);
  }
}

void NotebookExplorer2::onNodeActivated(const NodeIdentifier &p_nodeId,
                                        const QSharedPointer<FileOpenParameters> &p_paras) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Forward the signal (already using NodeIdentifier)
  emit nodeActivated(p_nodeId, p_paras);
}

void NotebookExplorer2::onContextMenuRequested(const NodeIdentifier &p_nodeId,
                                               const QPoint &p_globalPos) {
  NotebookNodeController *controller = nullptr;

  // Determine which controller to use based on which view triggered the event
  if (m_exploreMode == Combined) {
    controller = m_combinedController;
  } else {
    // Check which view has focus
    if (m_fileView && m_fileView->hasFocus()) {
      controller = m_fileController;
    } else {
      controller = m_folderController;
    }
  }

  if (!controller) {
    return;
  }

  QMenu *menu = controller->createContextMenu(p_nodeId, this);
  if (menu) {
    menu->exec(p_globalPos);
    delete menu;
  }
}

void NotebookExplorer2::onFolderSelectionChanged(const QList<NodeIdentifier> &p_nodeIds) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  // Get the first selected folder
  const NodeIdentifier &folderId = p_nodeIds.first();
  if (!folderId.isValid()) {
    return;
  }

  // Update file model to show contents of selected folder
  // Set the same notebook and let the model handle folder filtering
  if (m_fileModel && !m_currentNotebookId.isEmpty()) {
    m_fileModel->setNotebookId(m_currentNotebookId);
    // Reload the model to show children of selected folder
    m_fileModel->reloadNode(folderId);
  }
}

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
    auto &notebookService = *m_services.get<NotebookService>();
    QJsonObject resolved = notebookService.resolvePathToNotebook(scheme.m_folderPath);
    if (!resolved.isEmpty()) {
      notebookId = resolved[QStringLiteral("notebookId")].toString();
      folderPath = resolved[QStringLiteral("relativePath")].toString();
    } else {
      // Path not found in any notebook - use scheme.m_folderPath as-is if current notebook is valid.
      if (notebookId.isEmpty()) {
        MessageBoxHelper::notify(
            MessageBoxHelper::Information,
            tr("The quick note folder path (%1) is not within any open notebook.").arg(scheme.m_folderPath),
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
                             tr("The quick note should be created within a notebook."),
                             window());
    return;
  }

  // Generate filename using snippet expansion.
  QString expandedName = SnippetMgr::getInst().applySnippetBySymbol(scheme.m_noteName);
  QFileInfo finfo(expandedName);

  // Get notebook root path to generate unique filename.
  auto &notebookService = *m_services.get<NotebookService>();
  QJsonObject notebookConfig = notebookService.getNotebookConfig(notebookId);
  QString rootFolder = notebookConfig[QStringLiteral("rootFolder")].toString();
  QString parentAbsPath = folderPath.isEmpty()
                             ? rootFolder
                             : QDir(rootFolder).filePath(folderPath);

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
    MessageBoxHelper::notify(
        MessageBoxHelper::Information,
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

  // Open the file in edit mode.
  QString filePath = QDir(parentAbsPath).filePath(newFileName);
  auto paras = QSharedPointer<FileOpenParameters>::create();
  paras->m_mode = ViewWindowMode::Edit;
  paras->m_newFile = true;
  emit fileActivated(filePath, paras);
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
  if (m_exploreMode == Combined && m_combinedModel) {
    info = m_combinedModel->nodeInfoFromIndex(m_combinedModel->indexFromNodeId(nodeId));
  } else if (m_folderModel) {
    info = m_folderModel->nodeInfoFromIndex(m_folderModel->indexFromNodeId(nodeId));
  }

  if (info.isFolder) {
    return nodeId;
  }

  // Return parent folder for file nodes
  NodeIdentifier parentId;
  parentId.notebookId = nodeId.notebookId;
  parentId.relativePath = nodeId.parentPath();
  return parentId;
}

NodeIdentifier NotebookExplorer2::currentExploredNodeId() const {
  return currentNodeId();
}

QByteArray NotebookExplorer2::saveState() const {
  // Save current node path and view state
  QByteArray state;
  QDataStream stream(&state, QIODevice::WriteOnly);

  // Save explore mode
  stream << static_cast<int>(m_exploreMode);

  // Save splitter sizes if in TwoColumns mode
  if (m_twoColumnsSplitter) {
    stream << m_twoColumnsSplitter->saveState();
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
  if (!splitterState.isEmpty() && m_twoColumnsSplitter) {
    m_twoColumnsSplitter->restoreState(splitterState);
  }
}

void NotebookExplorer2::setNodeViewOrder(ViewOrder p_order) {
  // Apply to combined mode proxy model if it exists
  if (m_combinedProxyModel) {
    m_combinedProxyModel->setViewOrder(p_order);
    m_combinedProxyModel->sort(0); // Trigger re-sort
  }

  // Apply to two-columns mode proxy models if they exist
  if (m_folderProxyModel) {
    m_folderProxyModel->setViewOrder(p_order);
    m_folderProxyModel->sort(0); // Trigger re-sort
  }
  if (m_fileProxyModel) {
    m_fileProxyModel->setViewOrder(p_order);
    m_fileProxyModel->sort(0); // Trigger re-sort
  }
}

// --- GUI request handlers from controller signals ---

void NotebookExplorer2::onNewNoteRequested(const NodeIdentifier &p_parentId) {
  NewNoteDialog2 dialog(m_services, p_parentId, window());
  if (dialog.exec() == QDialog::Accepted) {
    NodeIdentifier newNodeId = dialog.getNewNodeId();
    if (newNodeId.isValid()) {
      // Reload parent node in the appropriate model
      if (m_exploreMode == Combined) {
        if (m_combinedModel) {
          m_combinedModel->reloadNode(p_parentId);
        }
      } else {
        // TwoColumns mode - reload file model (notes are files)
        if (m_fileModel) {
          m_fileModel->reloadNode(p_parentId);
        }
      }
      // Select the new note
      setCurrentNode(newNodeId);
    }
  }
}

void NotebookExplorer2::onNewFolderRequested(const NodeIdentifier &p_parentId) {
  NewFolderDialog2 dialog(m_services, p_parentId, window());
  if (dialog.exec() == QDialog::Accepted) {
    NodeIdentifier newNodeId = dialog.getNewNodeId();
    if (newNodeId.isValid()) {
      // Reload parent node in the appropriate model
      if (m_exploreMode == Combined) {
        if (m_combinedModel) {
          m_combinedModel->reloadNode(p_parentId);
        }
      } else {
        // TwoColumns mode - reload folder model
        if (m_folderModel) {
          m_folderModel->reloadNode(p_parentId);
        }
      }
      // Select the new folder
      setCurrentNode(newNodeId);
    }
  }
}

void NotebookExplorer2::onRenameRequested(const NodeIdentifier &p_nodeId,
                                          const QString &p_currentName) {
  bool ok;
  QString newName = QInputDialog::getText(window(), tr("Rename"), tr("New name:"),
                                          QLineEdit::Normal, p_currentName, &ok);
  if (!ok || newName.isEmpty() || newName == p_currentName) {
    return;
  }

  // Call back to the appropriate controller to perform the rename
  NotebookNodeController *controller = nullptr;
  if (m_exploreMode == Combined) {
    controller = m_combinedController;
  } else {
    // Determine which controller based on the node
    if (m_folderModel) {
      NodeInfo info = m_folderModel->nodeInfoFromIndex(m_folderModel->indexFromNodeId(p_nodeId));
      controller = info.isFolder ? m_folderController : m_fileController;
    }
  }

  if (controller) {
    controller->handleRenameResult(p_nodeId, newName);
  }
}

void NotebookExplorer2::onDeleteRequested(const QList<NodeIdentifier> &p_nodeIds, bool p_permanent) {
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

  int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Question, message, title, QString(),
                                               window());
  if (ret != QMessageBox::Ok) {
    return;
  }

  // Call back to the appropriate controller to perform the delete
  NotebookNodeController *controller = nullptr;
  if (m_exploreMode == Combined) {
    controller = m_combinedController;
  } else {
    // Use folder controller for delete operations (it can handle both)
    controller = m_folderController;
  }

  if (controller) {
    controller->handleDeleteConfirmed(p_nodeIds, p_permanent);
  }
}

void NotebookExplorer2::onRemoveFromNotebookRequested(const QList<NodeIdentifier> &p_nodeIds) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  QString message =
      tr("Remove %n node(s) from notebook? Files will remain on disk.", "", p_nodeIds.size());

  int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Question, message,
                                               tr("Remove From Notebook"), QString(), window());
  if (ret != QMessageBox::Ok) {
    return;
  }

  // Call back to the appropriate controller to perform the removal
  NotebookNodeController *controller = nullptr;
  if (m_exploreMode == Combined) {
    controller = m_combinedController;
  } else {
    controller = m_folderController;
  }

  if (controller) {
    controller->handleRemoveConfirmed(p_nodeIds);
  }
}

void NotebookExplorer2::onImportFilesRequested(const NodeIdentifier &p_targetFolderId) {
  QStringList files = QFileDialog::getOpenFileNames(window(), tr("Import Files"), QString(),
                                                    tr("All Files (*)"));
  if (files.isEmpty()) {
    return;
  }

  // Call back to the appropriate controller
  NotebookNodeController *controller = nullptr;
  if (m_exploreMode == Combined) {
    controller = m_combinedController;
  } else {
    controller = m_folderController;
  }

  if (controller) {
    controller->handleImportFiles(p_targetFolderId, files);
  }
}

void NotebookExplorer2::onImportFolderRequested(const NodeIdentifier &p_targetFolderId) {
  QString folder = QFileDialog::getExistingDirectory(window(), tr("Import Folder"), QString());
  if (folder.isEmpty()) {
    return;
  }

  // Call back to the appropriate controller
  NotebookNodeController *controller = nullptr;
  if (m_exploreMode == Combined) {
    controller = m_combinedController;
  } else {
    controller = m_folderController;
  }

  if (controller) {
    controller->handleImportFolder(p_targetFolderId, folder);
  }
}

void NotebookExplorer2::onPropertiesRequested(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Get node info to display properties
  NodeInfo nodeInfo;
  if (m_exploreMode == Combined && m_combinedModel) {
    nodeInfo = m_combinedModel->nodeInfoFromIndex(m_combinedModel->indexFromNodeId(p_nodeId));
  } else if (m_folderModel) {
    nodeInfo = m_folderModel->nodeInfoFromIndex(m_folderModel->indexFromNodeId(p_nodeId));
    if (!nodeInfo.isValid() && m_fileModel) {
      nodeInfo = m_fileModel->nodeInfoFromIndex(m_fileModel->indexFromNodeId(p_nodeId));
    }
  }

  if (!nodeInfo.isValid()) {
    return;
  }

  // Build absolute path
  QString absolutePath;
  auto *notebookService = m_services.get<NotebookService>();
  QJsonObject config = notebookService->getNotebookConfig(p_nodeId.notebookId);
  QString rootPath = config.value(QStringLiteral("root_folder")).toString();
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
