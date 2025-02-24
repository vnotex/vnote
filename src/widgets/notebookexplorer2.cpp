#include "notebookexplorer2.h"

#include <QActionGroup>
#include <QDataStream>
#include <QDir>
#include <QFileDialog>
#include <QMenu>
#include <QProgressDialog>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <controllers/notebooknodecontroller.h>
#include <core/configmgr2.h>
#include <core/exception.h>
#include <core/fileopenparameters.h>
#include <core/global.h>
#include <core/notebook/notebook.h>
#include <core/servicelocator.h>
#include <core/sessionconfig.h>
#include <core/vnotex.h>
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
// #include <widgets/dialogs/selectdialog.h>
#include <widgets/mainwindow.h>
#include <widgets/messageboxhelper.h>
// TODO: Migrate NavigationModeMgr to use ServiceLocator DI pattern
// #include <widgets/navigationmodemgr.h>
#include <widgets/notebookselector2.h>
#include <widgets/titlebar.h>
// TODO: Migrate TemplateMgr and SnippetMgr to use ServiceLocator DI pattern
// #include <core/templatemgr.h>
// #include <snippet/snippetmgr.h>

using namespace vnotex;

NotebookExplorer2::NotebookExplorer2(ServiceLocator &p_services, QWidget *p_parent) : QFrame(p_parent), m_services(p_services) { setupUI(); }

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

  // Connect notebook selector - use activated for user interaction
  connect(m_notebookSelector, QOverload<int>::of(&QComboBox::activated), this,
          [this](int p_idx) {
            auto id = static_cast<ID>(m_notebookSelector->itemData(p_idx).toULongLong());
            emit notebookActivated(id);
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

  m_titleBar->addMenuAction(tr("Manage Notebooks"), m_titleBar,
                            [this]() { manageNotebooks(); });

  m_titleBar->addMenuAction(tr("Rebuild Database"), m_titleBar,
                            [this]() { rebuildDatabase(); });

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
                                               m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerExternalFilesVisible(p_checked);
                                               // TODO: Apply to views when external file support is added
                                             });
    showAct->setCheckable(true);
    showAct->setChecked(widgetConfig.isNodeExplorerExternalFilesVisible());

    auto importAct = m_titleBar->addMenuAction(tr("Import External Files when Activated"),
                                               m_titleBar,
                                               [this](bool p_checked) {
                                                 m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerAutoImportExternalFilesEnabled(p_checked);
                                               });
    importAct->setCheckable(true);
    importAct->setChecked(widgetConfig.getNodeExplorerAutoImportExternalFilesEnabled());
  }

  {
    m_titleBar->addMenuSeparator();
    auto act = m_titleBar->addMenuAction(
        tr("Close File Before Opening Externally"), m_titleBar, [this](bool p_checked) {
          m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerCloseBeforeOpenWithEnabled(p_checked);
        });
    act->setCheckable(true);
    act->setChecked(widgetConfig.getNodeExplorerCloseBeforeOpenWithEnabled());
  }

  setupExploreModeMenu();
}

void NotebookExplorer2::setupRecycleBinMenu() {
  m_titleBar->addMenuSeparator();

  m_titleBar->addMenuAction(tr("Open Recycle Bin"), this, [this]() {
    if (m_currentNotebook) {
      WidgetUtils::openUrlByDesktop(
          QUrl::fromLocalFile(m_currentNotebook->getRecycleBinFolderAbsolutePath()));
    }
  });

  m_titleBar->addMenuAction(tr("Empty Recycle Bin"), this, [this]() {
    if (!m_currentNotebook) {
      return;
    }
    int okRet = MessageBoxHelper::questionOkCancel(
        MessageBoxHelper::Warning,
        tr("Empty the recycle bin of notebook (%1)?").arg(m_currentNotebook->getName()),
        tr("CAUTION! All the files under the recycle bin folder will be deleted and "
           "unrecoverable!"),
        tr("Recycle bin folder: %1").arg(m_currentNotebook->getRecycleBinFolderAbsolutePath()),
        window());
    if (okRet == QMessageBox::Ok) {
      m_currentNotebook->emptyRecycleBin();
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
      // TODO: Apply view order to MVC views when sorting is implemented
    }
  });
}

void NotebookExplorer2::rebuildDatabase() {
  if (m_currentNotebook) {
    int okRet = MessageBoxHelper::questionOkCancel(
        MessageBoxHelper::Warning,
        tr("Rebuild the database of notebook (%1)?").arg(m_currentNotebook->getName()),
        tr("This operation will rebuild the notebook database from configuration files. It may "
           "take time."),
        tr("A notebook may use a database for cache, such as IDs of nodes and tags."),
        window());
    if (okRet != QMessageBox::Ok) {
      return;
    }

    QProgressDialog proDlg(tr("Rebuilding notebook database..."), QString(), 0, 0, this);
    proDlg.setWindowFlags(proDlg.windowFlags() & ~Qt::WindowCloseButtonHint);
    proDlg.setWindowModality(Qt::WindowModal);
    proDlg.setMinimumDuration(1000);
    proDlg.setValue(0);

    bool ret = m_currentNotebook->rebuildDatabase();

    proDlg.cancel();

    if (ret) {
      MessageBoxHelper::notify(MessageBoxHelper::Type::Information,
                               tr("Notebook database has been rebuilt."),
                               window());
    } else {
      MessageBoxHelper::notify(MessageBoxHelper::Type::Warning,
                               tr("Failed to rebuild notebook database."),
                               window());
    }
  }
}

void NotebookExplorer2::setupCombinedMode() {
  // Create MVC components for combined mode
  m_combinedModel = new NotebookNodeModel(this);
  m_combinedProxyModel = new NotebookNodeProxyModel(this);
  m_combinedProxyModel->setSourceModel(m_combinedModel);
  m_combinedProxyModel->setFilterFlags(NotebookNodeProxyModel::ShowAll);

  m_combinedView = new NotebookNodeView(this);
  m_combinedView->setModel(m_combinedProxyModel);

  m_combinedDelegate = new NotebookNodeDelegate(this);
  m_combinedView->setItemDelegate(m_combinedDelegate);

  m_combinedController = new NotebookNodeController(this);
  m_combinedController->setModel(m_combinedModel);
  m_combinedController->setView(m_combinedView);
  m_combinedView->setController(m_combinedController);

  // Add to content stack
  m_contentStack->addWidget(m_combinedView);

  // Connect signals
  connect(m_combinedView, &NotebookNodeView::nodeActivated, this,
          &NotebookExplorer2::onNodeActivated);
  connect(m_combinedView, &NotebookNodeView::contextMenuRequested, this,
          &NotebookExplorer2::onContextMenuRequested);

  // Forward controller signals
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
}

void NotebookExplorer2::setupTwoColumnsMode() {
  // Create splitter for two columns
  m_twoColumnsSplitter = new QSplitter(Qt::Horizontal, this);

  // Folder view (left) - shows only folders
  m_folderModel = new NotebookNodeModel(this);
  m_folderProxyModel = new NotebookNodeProxyModel(this);
  m_folderProxyModel->setSourceModel(m_folderModel);
  m_folderProxyModel->setFilterFlags(NotebookNodeProxyModel::ShowFolders);

  m_folderView = new NotebookNodeView(this);
  m_folderView->setModel(m_folderProxyModel);

  m_folderDelegate = new NotebookNodeDelegate(this);
  m_folderView->setItemDelegate(m_folderDelegate);

  m_folderController = new NotebookNodeController(this);
  m_folderController->setModel(m_folderModel);
  m_folderController->setView(m_folderView);
  m_folderView->setController(m_folderController);

  m_twoColumnsSplitter->addWidget(m_folderView);

  // File view (right) - shows files in selected folder
  m_fileModel = new NotebookNodeModel(this);
  m_fileProxyModel = new NotebookNodeProxyModel(this);
  m_fileProxyModel->setSourceModel(m_fileModel);
  m_fileProxyModel->setFilterFlags(NotebookNodeProxyModel::ShowNotes);

  m_fileView = new NotebookNodeView(this);
  m_fileView->setModel(m_fileProxyModel);

  m_fileDelegate = new NotebookNodeDelegate(this);
  m_fileDelegate->setShowChildCount(false); // Files don't have children
  m_fileView->setItemDelegate(m_fileDelegate);

  m_fileController = new NotebookNodeController(this);
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

  // Forward controller signals from both controllers
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
  };

  connectControllerSignals(m_folderController);
  connectControllerSignals(m_fileController);
}

void NotebookExplorer2::loadNotebooks() {
  if (!m_notebookSelector) {
    return;
  }

  m_notebookSelector->loadNotebooks();
}

void NotebookExplorer2::setCurrentNotebook(const QSharedPointer<Notebook> &p_notebook) {
  if (m_currentNotebook == p_notebook) {
    return;
  }

  m_currentNotebook = p_notebook;
  // Update all models that exist (views are created on-demand)
  if (m_combinedModel) {
    m_combinedModel->setNotebook(p_notebook);
  }

  if (m_folderModel) {
    m_folderModel->setNotebook(p_notebook);
    // File model will be updated when folder is selected
  }
}

QSharedPointer<Notebook> NotebookExplorer2::currentNotebook() const { return m_currentNotebook; }

void NotebookExplorer2::setCurrentNode(Node *p_node) {
  if (!p_node) {
    return;
  }

  if (m_exploreMode == Combined) {
    if (m_combinedView) {
      m_combinedView->expandToNode(p_node);
      m_combinedView->selectNode(p_node);
      m_combinedView->scrollToNode(p_node);
    }
  } else {
    // TwoColumns mode
    if (!m_folderView || !m_fileView) {
      return;
    }
    if (p_node->isContainer()) {
      m_folderView->expandToNode(p_node);
      m_folderView->selectNode(p_node);
    } else {
      // Select parent folder in folder view
      Node *parent = p_node->getParent();
      if (parent) {
        m_folderView->expandToNode(parent);
        m_folderView->selectNode(parent);
      }
      // Select file in file view
      m_fileView->selectNode(p_node);
    }
  }
}

Node *NotebookExplorer2::currentNode() const {
  if (m_exploreMode == Combined) {
    return m_combinedView ? m_combinedView->currentNode() : nullptr;
  } else {
    // In two columns mode, prefer file selection over folder
    if (m_fileView) {
      Node *fileNode = m_fileView->currentNode();
      if (fileNode) {
        return fileNode;
      }
    }
    return m_folderView ? m_folderView->currentNode() : nullptr;
  }
}

void NotebookExplorer2::setExploreMode(ExploreMode p_mode) {
  if (m_exploreMode == p_mode) {
    return;
  }

  m_exploreMode = p_mode;
  updateExploreMode();
}

NotebookExplorer2::ExploreMode NotebookExplorer2::exploreMode() const { return m_exploreMode; }

void NotebookExplorer2::updateExploreMode() {
  if (m_exploreMode == Combined) {
    // Ensure combined mode is set up (lazy creation)
    if (!m_combinedView) {
      setupCombinedMode();
    }
    m_contentStack->setCurrentWidget(m_combinedView);
    // Sync notebook to the view if needed
    if (m_currentNotebook && m_combinedModel) {
      m_combinedModel->setNotebook(m_currentNotebook);
    }
  } else {
    // Ensure two columns mode is set up (lazy creation)
    if (!m_twoColumnsSplitter) {
      setupTwoColumnsMode();
    }
    m_contentStack->setCurrentWidget(m_twoColumnsSplitter);
    // Sync notebook to the folder view if needed
    if (m_currentNotebook && m_folderModel) {
      m_folderModel->setNotebook(m_currentNotebook);
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


void NotebookExplorer2::onNodeActivated(Node *p_node,
                                        const QSharedPointer<FileOpenParameters> &p_paras) {
  emit nodeActivated(p_node, p_paras);
}

void NotebookExplorer2::onContextMenuRequested(Node *p_node, const QPoint &p_globalPos) {
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

  QMenu *menu = controller->createContextMenu(p_node, this);
  if (menu) {
    menu->exec(p_globalPos);
    delete menu;
  }
}

void NotebookExplorer2::onFolderSelectionChanged(const QList<Node *> &p_nodes) {
  if (p_nodes.isEmpty()) {
    return;
  }

  // Get the first selected folder
  Node *folder = p_nodes.first();
  if (!folder || !folder->isContainer()) {
    return;
  }

  // Update file model to show contents of selected folder
  // For now, we use the same notebook but could be optimized to show only
  // the selected folder's contents
  m_fileModel->setNotebook(m_currentNotebook);

  // Expand and select the folder's contents in the file view
  // The proxy model filters to show only notes, so we just need to
  // ensure the folder is loaded
  if (!folder->isLoaded()) {
    folder->load();
  }

  // Trigger a reload to show the folder's file children
  m_fileModel->reloadNode(folder);
}

// --- Public Slots Implementation ---

void NotebookExplorer2::newNotebook() {
  // Use window() to get top-level MainWindow2 for dialog centering.
  // DEPRECATED: VNoteX::getInst().getMainWindow() - use window() instead.
  NewNotebookDialog2 dialog(m_services, window());
  if (dialog.exec() == QDialog::Accepted) {
    // Reload notebooks to show the newly created one
    loadNotebooks();
  }
}

void NotebookExplorer2::newNotebookFromFolder() {
  // TODO: Migrate NewNotebookFromFolderDialog to use ServiceLocator DI pattern
  // DEPRECATED: VNoteX::getInst().getMainWindow() - use window() instead.
  MessageBoxHelper::notify(MessageBoxHelper::Information,
                           tr("New notebook from folder dialog is being migrated to use dependency injection."),
                           window());
}

void NotebookExplorer2::importNotebook() {
  // TODO: Migrate ImportNotebookDialog to use ServiceLocator DI pattern
  // DEPRECATED: VNoteX::getInst().getMainWindow() - use window() instead.
  MessageBoxHelper::notify(MessageBoxHelper::Information,
                           tr("Import notebook dialog is being migrated to use dependency injection."),
                           window());
}

void NotebookExplorer2::manageNotebooks() {
  // TODO: Migrate ManageNotebooksDialog to use ServiceLocator DI pattern
  // DEPRECATED: VNoteX::getInst().getMainWindow() - use window() instead.
  MessageBoxHelper::notify(MessageBoxHelper::Information,
                           tr("Manage notebooks dialog is being migrated to use dependency injection."),
                           window());
}

void NotebookExplorer2::reloadNotebook(const Notebook *p_notebook) {
  if (m_notebookSelector) {
    m_notebookSelector->reloadNotebook(p_notebook);
  }
}

void NotebookExplorer2::newFolder() {
  // TODO: Migrate NewFolderDialog to use ServiceLocator DI pattern
  // auto node = checkNotebookAndGetCurrentExploredFolderNode();
  // if (!node) {
  //   return;
  // }
  //
  // NewFolderDialog dialog(node, VNoteX::getInst().getMainWindow());
  // if (dialog.exec() == QDialog::Accepted) {
  //   setCurrentNode(dialog.getNewNode().data());
  // }
  MessageBoxHelper::notify(MessageBoxHelper::Information,
                           tr("New folder dialog is being migrated to use dependency injection."),
                           window());
}

void NotebookExplorer2::newNote() {
  // TODO: Migrate NewNoteDialog to use ServiceLocator DI pattern
  // auto node = checkNotebookAndGetCurrentExploredFolderNode();
  // if (!node) {
  //   return;
  // }
  //
  // NewNoteDialog dialog(node, VNoteX::getInst().getMainWindow());
  // if (dialog.exec() == QDialog::Accepted) {
  //   setCurrentNode(dialog.getNewNode().data());
  //   // Open it right now.
  //   auto paras = QSharedPointer<FileOpenParameters>::create();
  //   paras->m_mode = ViewWindowMode::Edit;
  //   paras->m_newFile = true;
  //   emit VNoteX::getInst().openNodeRequested(dialog.getNewNode().data(), paras);
  // }
  MessageBoxHelper::notify(MessageBoxHelper::Information,
                           tr("New note dialog is being migrated to use dependency injection."),
                           window());
}

void NotebookExplorer2::newQuickNote() {
  // TODO: Migrate newQuickNote to use ServiceLocator DI pattern
  // Requires: ConfigMgr2 (done), SelectDialog, SnippetMgr, TemplateMgr, NewNoteDialog
  // auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
  // const auto &schemes = sessionConfig.getQuickNoteSchemes();
  // if (schemes.isEmpty()) {
  //   MessageBoxHelper::notify(MessageBoxHelper::Information,
  //                            tr("Please set up quick note schemes in the Settings dialog first."),
  //                            VNoteX::getInst().getMainWindow());
  //   return;
  // }
  //
  // SelectDialog dialog(tr("New Quick Note"), VNoteX::getInst().getMainWindow());
  // for (int i = 0; i < schemes.size(); ++i) {
  //   dialog.addSelection(schemes[i].m_name, i);
  // }
  //
  // if (dialog.exec() != QDialog::Accepted) {
  //   return;
  // }
  //
  // int selection = dialog.getSelection();
  // const auto &scheme = schemes[selection];
  //
  // Notebook *notebook = m_currentNotebook.data();
  // Node *parentNode = currentExploredFolderNode();
  // if (!scheme.m_folderPath.isEmpty()) {
  //   auto node = VNoteX::getInst().getNotebookMgr().loadNodeByPath(scheme.m_folderPath);
  //   if (node) {
  //     notebook = node->getNotebook();
  //     parentNode = node.data();
  //   }
  // }
  //
  // if (!parentNode) {
  //   MessageBoxHelper::notify(MessageBoxHelper::Information,
  //                            tr("The quick note should be created within a notebook."),
  //                            VNoteX::getInst().getMainWindow());
  //   return;
  // }
  //
  // QFileInfo finfo(SnippetMgr::getInst().applySnippetBySymbol(scheme.m_noteName));
  // QString newName = FileUtils::generateFileNameWithSequence(
  //     parentNode->fetchAbsolutePath(), finfo.completeBaseName(), finfo.suffix());
  //
  // QString errMsg;
  // auto newNode =
  //     NewNoteDialog::newNote(notebook, parentNode, newName,
  //                            TemplateMgr::getInst().getTemplateContent(scheme.m_template), errMsg);
  // if (!newNode) {
  //   MessageBoxHelper::notify(
  //       MessageBoxHelper::Information,
  //       tr("Failed to create quick note from scheme (%1) (%2)").arg(scheme.m_name, errMsg),
  //       VNoteX::getInst().getMainWindow());
  //   return;
  // }
  //
  // if (notebook == m_currentNotebook.data()) {
  //   setCurrentNode(newNode.data());
  // }
  //
  // // Open it right now.
  // auto paras = QSharedPointer<FileOpenParameters>::create();
  // paras->m_mode = ViewWindowMode::Edit;
  // paras->m_newFile = true;
  // emit VNoteX::getInst().openNodeRequested(newNode.data(), paras);
  MessageBoxHelper::notify(MessageBoxHelper::Information,
                           tr("Quick note is being migrated to use dependency injection."),
                           window());
}

void NotebookExplorer2::importFile() {
  auto node = checkNotebookAndGetCurrentExploredFolderNode();
  if (!node) {
    return;
  }

  static QString lastFolderPath = QDir::homePath();
  QStringList files = QFileDialog::getOpenFileNames(window(),
                                                    tr("Select Files To Import"), lastFolderPath);
  if (files.isEmpty()) {
    return;
  }

  QString errMsg;
  for (const auto &file : files) {
    try {
      m_currentNotebook->copyAsNode(node, Node::Flag::Content, file);
    } catch (Exception &p_e) {
      errMsg += tr("Failed to add file (%1) as node (%2).\n").arg(file, p_e.what());
    }
  }

  if (!errMsg.isEmpty()) {
    MessageBoxHelper::notify(MessageBoxHelper::Critical, errMsg, window());
  }

  emit m_currentNotebook->nodeUpdated(node);
  setCurrentNode(node);
}

void NotebookExplorer2::importFolder() {
  // TODO: Migrate ImportFolderDialog to use ServiceLocator DI pattern
  // auto node = checkNotebookAndGetCurrentExploredFolderNode();
  // if (!node) {
  //   return;
  // }
  //
  // ImportFolderDialog dialog(node, VNoteX::getInst().getMainWindow());
  // if (dialog.exec() == QDialog::Accepted) {
  //   setCurrentNode(dialog.getNewNode().data());
  // }
  MessageBoxHelper::notify(MessageBoxHelper::Information,
                           tr("Import folder dialog is being migrated to use dependency injection."),
                           window());
}

void NotebookExplorer2::locateNode(Node *p_node) {
  Q_ASSERT(p_node);
  auto nb = p_node->getNotebook();
  if (nb != m_currentNotebook) {
    emit notebookActivated(nb->getId());
  }
  setCurrentNode(p_node);

  // Set focus to the appropriate view
  if (m_exploreMode == Combined) {
    if (m_combinedView) {
      m_combinedView->setFocus();
    }
  } else {
    if (p_node->isContainer()) {
      if (m_folderView) {
        m_folderView->setFocus();
      }
    } else {
      if (m_fileView) {
        m_fileView->setFocus();
      }
    }
  }
}

Node *NotebookExplorer2::currentExploredFolderNode() const {
  Node *node = currentNode();
  if (!node) {
    // Return notebook root if no selection
    if (m_currentNotebook) {
      return m_currentNotebook->getRootNode().data();
    }
    return nullptr;
  }

  if (node->isContainer()) {
    return node;
  }

  // Return parent folder for file nodes
  return node->getParent();
}

Node *NotebookExplorer2::currentExploredNode() const { return currentNode(); }

Node *NotebookExplorer2::checkNotebookAndGetCurrentExploredFolderNode() const {
  if (!m_currentNotebook) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Please first create a notebook to hold your data."),
                             window());
    return nullptr;
  }

  auto node = currentExploredFolderNode();
  Q_ASSERT(m_currentNotebook.data() == node->getNotebook());
  return node;
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
