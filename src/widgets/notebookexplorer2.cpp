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
#include <core/hooknames.h>
#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
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
#include <views/twocolumnsnodeexplorer.h>
#include <views/notebooknodeview.h>
// TODO: Migrate dialogs to use ServiceLocator DI pattern
#include <widgets/dialogs/importfolderdialog2.h>
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

    // Fire hook before emitting Qt signal (WordPress-style plugin architecture)
    auto *hookMgr = m_services.get<HookManager>();
    if (hookMgr) {
      QVariantMap args;
      args[QStringLiteral("notebookId")] = guid;
      if (hookMgr->doAction(HookNames::NotebookBeforeOpen, args)) {
        return; // Cancelled by plugin
      }
    }
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

  // External files options
  {
    m_titleBar->addMenuSeparator();
    auto showAct = m_titleBar->addMenuAction(tr("Show External Files"), m_titleBar,
                                             [this](bool p_checked) {
                                               m_services.get<ConfigMgr2>()
                                                   ->getWidgetConfig()
                                                   .setNodeExplorerExternalFilesVisible(p_checked);
                                               // Apply to model(s)
                                               if (m_combinedModel) {
                                                 m_combinedModel->setExternalNodesVisible(p_checked);
                                               }
                                               if (m_twoColumnsExplorer) {
                                                 m_twoColumnsExplorer->setExternalNodesVisible(p_checked);
                                               }
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
  bool success = notebookService.rebuildNotebookCache(m_currentNotebookId);

  progress.close();

  // Reload the current notebook to reflect changes.
  if (m_combinedModel) {
    m_combinedModel->setNotebookId(m_currentNotebookId);
  }
  if (m_twoColumnsExplorer) {
    m_twoColumnsExplorer->setNotebookId(m_currentNotebookId);
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
  // Create MVC components for combined mode
  m_combinedModel = new NotebookNodeModel(m_services, this);
  m_combinedProxyModel = new NotebookNodeProxyModel(this);
  m_combinedProxyModel->setSourceModel(m_combinedModel);
  m_combinedProxyModel->setFilterFlags(NotebookNodeProxyModel::ShowAll);

  // Apply initial view order and external files visibility from config
  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();
  m_combinedProxyModel->setViewOrder(static_cast<ViewOrder>(widgetConfig.getNodeExplorerViewOrder()));
  m_combinedProxyModel->sort(0); // Enable sorting
  m_combinedModel->setExternalNodesVisible(widgetConfig.isNodeExplorerExternalFilesVisible());

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

  // Model error signals (for inline rename failures)
  connect(m_combinedModel, &NotebookNodeModel::errorOccurred, this,
          &NotebookExplorer2::onErrorOccurred);
}

void NotebookExplorer2::setupTwoColumnsMode() {
  m_twoColumnsExplorer = new TwoColumnsNodeExplorer(m_services, this);
  m_contentStack->addWidget(m_twoColumnsExplorer);

  // Connect context menu signal
  connect(m_twoColumnsExplorer,
          QOverload<const NodeIdentifier &, const QPoint &, bool>::of(
              &TwoColumnsNodeExplorer::contextMenuRequested),
          this, &NotebookExplorer2::onTwoColumnsContextMenu);

  // Forward controller signals (unified from both folder and file controllers)
  // Note: nodeActivated already routes through TwoColumnsNodeExplorer's controllers
  // which handle auto-import for external nodes
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::nodeActivated, this,
          &NotebookExplorer2::nodeActivated);
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::fileActivated, this,
          &NotebookExplorer2::fileActivated);
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::nodeAboutToMove, this,
          &NotebookExplorer2::nodeAboutToMove);
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::nodeAboutToRemove, this,
          &NotebookExplorer2::nodeAboutToRemove);
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::nodeAboutToReload, this,
          &NotebookExplorer2::nodeAboutToReload);
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::closeFileRequested, this,
          &NotebookExplorer2::closeFileRequested);

  // Connect GUI request signals
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::newNoteRequested, this,
          &NotebookExplorer2::onNewNoteRequested);
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::newFolderRequested, this,
          &NotebookExplorer2::onNewFolderRequested);
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::renameRequested, this,
          &NotebookExplorer2::onRenameRequested);
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::deleteRequested, this,
          &NotebookExplorer2::onDeleteRequested);
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::removeFromNotebookRequested, this,
          &NotebookExplorer2::onRemoveFromNotebookRequested);
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::propertiesRequested, this,
          &NotebookExplorer2::onPropertiesRequested);
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::errorOccurred, this,
          &NotebookExplorer2::onErrorOccurred);
  connect(m_twoColumnsExplorer, &TwoColumnsNodeExplorer::infoMessage, this,
          &NotebookExplorer2::onInfoMessage);
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

  if (m_twoColumnsExplorer) {
    m_twoColumnsExplorer->setNotebookId(p_notebookId);
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
    // TwoColumns mode - use unified interface
    if (m_twoColumnsExplorer) {
      m_twoColumnsExplorer->expandToNode(p_nodeId);
      m_twoColumnsExplorer->selectNode(p_nodeId);
      m_twoColumnsExplorer->scrollToNode(p_nodeId);
    }
  }
}

NodeIdentifier NotebookExplorer2::currentNodeId() const {
  if (m_exploreMode == Combined) {
    return m_combinedView ? m_combinedView->currentNodeId() : NodeIdentifier();
  } else {
    // In two columns mode, prefer file selection over folder
    return m_twoColumnsExplorer ? m_twoColumnsExplorer->currentNodeId() : NodeIdentifier();
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
    if (!m_twoColumnsExplorer) {
      setupTwoColumnsMode();
    }
    m_contentStack->setCurrentWidget(m_twoColumnsExplorer);
    // Sync notebook to the explorer if needed
    if (!m_currentNotebookId.isEmpty() && m_twoColumnsExplorer) {
      m_twoColumnsExplorer->setNotebookId(m_currentNotebookId);
    }
  }
  updateFocusProxy();
}

void NotebookExplorer2::updateFocusProxy() {
  if (m_exploreMode == Combined && m_combinedView) {
    setFocusProxy(m_combinedView);
  } else if (m_exploreMode == TwoColumns && m_twoColumnsExplorer) {
    setFocusProxy(m_twoColumnsExplorer);
  }
}

void NotebookExplorer2::onNodeActivated(const NodeIdentifier &p_nodeId,
                                        const QSharedPointer<FileOpenParameters> &p_paras) {
  Q_UNUSED(p_paras);
  if (!p_nodeId.isValid()) {
    return;
  }

  // Route through controller to handle auto-import for external nodes
  if (m_combinedController) {
    m_combinedController->openNode(p_nodeId);
  }
}

void NotebookExplorer2::onContextMenuRequested(const NodeIdentifier &p_nodeId,
                                               const QPoint &p_globalPos) {
  // Use unified interface - this is called from combined mode only
  // (TwoColumns mode uses onTwoColumnsContextMenu with 3-param signal)
  if (m_exploreMode == Combined && m_combinedController) {
    QMenu *menu = m_combinedController->createContextMenu(p_nodeId, this);
    if (menu) {
      menu->exec(p_globalPos);
      delete menu;
    }
  }
}

void NotebookExplorer2::onTwoColumnsContextMenu(const NodeIdentifier &p_nodeId,
                                                 const QPoint &p_globalPos,
                                                 bool p_isFromFileView) {
  if (!m_twoColumnsExplorer) {
    return;
  }

  // Use unified createContextMenu method
  QMenu *menu = m_twoColumnsExplorer->createContextMenu(p_nodeId, p_isFromFileView, this);
  if (menu) {
    menu->exec(p_globalPos);
    delete menu;
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
  } else if (m_twoColumnsExplorer) {
    info = m_twoColumnsExplorer->getNodeInfo(nodeId);
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
  if (m_twoColumnsExplorer) {
    stream << m_twoColumnsExplorer->saveSplitterState();
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
  if (!splitterState.isEmpty() && m_twoColumnsExplorer) {
    m_twoColumnsExplorer->restoreSplitterState(splitterState);
  }
}

void NotebookExplorer2::setNodeViewOrder(ViewOrder p_order) {
  // Apply to combined mode proxy model if it exists
  if (m_combinedProxyModel) {
    m_combinedProxyModel->setViewOrder(p_order);
    m_combinedProxyModel->sort(0); // Trigger re-sort
  }

  // Apply to two-columns mode proxy models if they exist
  // Apply to two-columns mode explorer if it exists
  if (m_twoColumnsExplorer) {
    m_twoColumnsExplorer->setViewOrder(p_order);
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
        if (m_twoColumnsExplorer) {
          m_twoColumnsExplorer->reloadNode(p_parentId, false);
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
        if (m_twoColumnsExplorer) {
          m_twoColumnsExplorer->reloadNode(p_parentId, true);
        }
      }
      // Select the new folder
      setCurrentNode(newNodeId);
    }
  }
}

void NotebookExplorer2::onRenameRequested(const NodeIdentifier &p_nodeId,
                                          const QString &p_currentName) {
  Q_UNUSED(p_currentName);

  // Trigger inline editing instead of dialog
  if (m_exploreMode == Combined) {
    if (m_combinedView && m_combinedModel && m_combinedProxyModel) {
      QModelIndex sourceIdx = m_combinedModel->indexFromNodeId(p_nodeId);
      if (sourceIdx.isValid()) {
        QModelIndex viewIdx = m_combinedProxyModel->mapFromSource(sourceIdx);
        if (viewIdx.isValid()) {
          m_combinedView->setCurrentIndex(viewIdx);
          m_combinedView->edit(viewIdx);
        }
      }
    }
  } else {
    if (m_twoColumnsExplorer) {
      m_twoColumnsExplorer->startInlineRename(p_nodeId);
    }
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

  int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Question, title, message,
                                               QString(), window());
  if (ret != QMessageBox::Ok) {
    return;
  }

  // Use unified interface to perform the delete
  if (m_exploreMode == Combined) {
    if (m_combinedController) {
      m_combinedController->handleDeleteConfirmed(p_nodeIds, p_permanent);
    }
  } else {
    if (m_twoColumnsExplorer) {
      m_twoColumnsExplorer->handleDeleteConfirmed(p_nodeIds, p_permanent);
    }
  }
}

void NotebookExplorer2::onRemoveFromNotebookRequested(const QList<NodeIdentifier> &p_nodeIds) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  QString message =
      tr("Remove %n node(s) from notebook index? Files will remain on disk.", "", p_nodeIds.size());

  int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Question,
                                               tr("Remove From Notebook"), message, QString(), window());
  if (ret != QMessageBox::Ok) {
    return;
  }

  // Use unified interface to perform the removal
  if (m_exploreMode == Combined) {
    if (m_combinedController) {
      m_combinedController->handleRemoveConfirmed(p_nodeIds);
    }
  } else {
    if (m_twoColumnsExplorer) {
      m_twoColumnsExplorer->handleRemoveConfirmed(p_nodeIds);
    }
  }
}

void NotebookExplorer2::onImportFilesRequested(const NodeIdentifier &p_targetFolderId) {
  QStringList files = QFileDialog::getOpenFileNames(window(), tr("Import Files"), QString(),
                                                    tr("All Files (*)"));
  if (files.isEmpty()) {
    return;
  }

  // Use controller to perform the import
  if (m_exploreMode == Combined && m_combinedController) {
    m_combinedController->handleImportFiles(p_targetFolderId, files);
  } else if (m_twoColumnsExplorer) {
    // For two-column mode, we still need a controller to handle import
    // Since TwoColumnsNodeExplorer doesn't expose handleImportFiles,
    // we need to handle this differently - use NotebookService directly
    auto &notebookService = *m_services.get<NotebookService>();
    for (const QString &filePath : files) {
      QFileInfo fileInfo(filePath);
      QString destPath = p_targetFolderId.relativePath.isEmpty()
                             ? fileInfo.fileName()
                             : p_targetFolderId.relativePath + "/" + fileInfo.fileName();
      notebookService.importFile(p_targetFolderId.notebookId, filePath, destPath);
    }
    // Reload the folder to show imported files
    m_twoColumnsExplorer->reloadNode(p_targetFolderId);
  }
}

void NotebookExplorer2::onImportFolderRequested(const NodeIdentifier &p_targetFolderId) {
  ImportFolderDialog2 dialog(m_services, p_targetFolderId, window());
  if (dialog.exec() == QDialog::Accepted) {
    NodeIdentifier newNodeId = dialog.getNewNodeId();
    if (newNodeId.isValid()) {
      // Reload the parent folder to show the new imported folder
      if (m_exploreMode == Combined && m_combinedModel) {
        m_combinedModel->reloadNode(p_targetFolderId);
      } else if (m_twoColumnsExplorer) {
        m_twoColumnsExplorer->reloadNode(p_targetFolderId, true);
      }
      setCurrentNode(newNodeId);
    }
  }
}

void NotebookExplorer2::onPropertiesRequested(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Get node info using unified interface
  NodeInfo nodeInfo;
  if (m_exploreMode == Combined && m_combinedModel) {
    nodeInfo = m_combinedModel->nodeInfoFromIndex(m_combinedModel->indexFromNodeId(p_nodeId));
  } else if (m_twoColumnsExplorer) {
    nodeInfo = m_twoColumnsExplorer->getNodeInfo(p_nodeId);
  }


  if (!nodeInfo.isValid()) {
    return;
  }

  // Build absolute path
  QString absolutePath;
  auto *notebookService = m_services.get<NotebookService>();
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
