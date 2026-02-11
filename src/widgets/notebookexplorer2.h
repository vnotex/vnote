#ifndef NOTEBOOKEXPLORER2_H
#define NOTEBOOKEXPLORER2_H

#include <QFrame>
#include <QSharedPointer>

#include <core/global.h>

class QStackedWidget;
class QSplitter;
class QMenu;
class QActionGroup;

namespace vnotex {

class Node;
class Notebook;
class NotebookMgr;
class NotebookSelector;
class TitleBar;
class NotebookNodeModel;
class NotebookNodeProxyModel;
class NotebookNodeView;
class NotebookNodeDelegate;
class NotebookNodeController;
struct FileOpenParameters;
class Event;

// NotebookExplorer2 is a container widget that displays notebook nodes
// using proper MVC architecture.
// It supports two explore modes:
// - Combined: Single tree view showing all nodes
// - TwoColumns: Split view with folder tree on left, file list on right
class NotebookExplorer2 : public QFrame {
  Q_OBJECT

public:
  enum ExploreMode {
    Combined,   // Single tree with folders and files
    TwoColumns  // Left: folders only, Right: files in selected folder
  };

  explicit NotebookExplorer2(QWidget *p_parent = nullptr);
  ~NotebookExplorer2() override;

  // Notebook management
  void setCurrentNotebook(const QSharedPointer<Notebook> &p_notebook);
  QSharedPointer<Notebook> currentNotebook() const;

  // Navigation
  void setCurrentNode(Node *p_node);
  Node *currentNode() const;

  // Mode switching
  void setExploreMode(ExploreMode p_mode);
  ExploreMode exploreMode() const;

  // Initialize with NotebookMgr
  void setNotebookMgr(NotebookMgr *p_notebookMgr);

  // Get current explored folder node (for newNote/newFolder operations)
  Node *currentExploredFolderNode() const;

  // Get current explored node
  Node *currentExploredNode() const;

  // State saving/restoring for session
  QByteArray saveState() const;
  void restoreState(const QByteArray &p_data);

public slots:
  // Notebook operations - these launch dialogs
  void newNotebook();
  void newNotebookFromFolder();
  void importNotebook();
  void manageNotebooks();

  // Reload operations
  void loadNotebooks();
  void reloadNotebook(const Notebook *p_notebook);

  // Node creation - use current explored folder as parent
  void newFolder();
  void newNote();
  void newQuickNote();

  // Import operations - use current explored folder as parent
  void importFile();
  void importFolder();

  // Navigation
  void locateNode(Node *p_node);

signals:
  // Emitted when user selects a notebook from the selector
  void notebookActivated(ID p_notebookId);
  // Forwarded from controller - same signatures as NotebookExplorer
  void nodeActivated(Node *p_node, const QSharedPointer<FileOpenParameters> &p_paras);
  void fileActivated(const QString &p_path, const QSharedPointer<FileOpenParameters> &p_paras);
  void nodeAboutToMove(Node *p_node, const QSharedPointer<Event> &p_event);
  void nodeAboutToRemove(Node *p_node, const QSharedPointer<Event> &p_event);
  void nodeAboutToReload(Node *p_node, const QSharedPointer<Event> &p_event);
  void closeFileRequested(const QString &p_filePath, const QSharedPointer<Event> &p_event);

private slots:
  void onNotebookChanged(int p_index);
  void onNodeActivated(Node *p_node, const QSharedPointer<FileOpenParameters> &p_paras);
  void onContextMenuRequested(Node *p_node, const QPoint &p_globalPos);

  // TwoColumns mode: when folder selection changes, update file list
  void onFolderSelectionChanged(const QList<Node *> &p_nodes);

private:
  void setupUI();
  void setupTitleBar();
  void setupTitleBarMenu();
  void setupRecycleBinMenu();
  void setupExploreModeMenu();
  void setupViewMenu(QMenu *p_menu, bool p_isNotebookView);
  void setupCombinedMode();
  void setupTwoColumnsMode();
  void connectSignals();
  void updateExploreMode();
  void updateFocusProxy();

  // Notebook database rebuild
  void rebuildDatabase();

  // Helper: check notebook exists and get current explored folder
  Node *checkNotebookAndGetCurrentExploredFolderNode() const;

  // MVC Components - Combined mode
  NotebookNodeModel *m_combinedModel = nullptr;
  NotebookNodeProxyModel *m_combinedProxyModel = nullptr;
  NotebookNodeView *m_combinedView = nullptr;
  NotebookNodeDelegate *m_combinedDelegate = nullptr;
  NotebookNodeController *m_combinedController = nullptr;

  // MVC Components - TwoColumns mode (folder view)
  NotebookNodeModel *m_folderModel = nullptr;
  NotebookNodeProxyModel *m_folderProxyModel = nullptr;
  NotebookNodeView *m_folderView = nullptr;
  NotebookNodeDelegate *m_folderDelegate = nullptr;
  NotebookNodeController *m_folderController = nullptr;

  // MVC Components - TwoColumns mode (file view)
  NotebookNodeModel *m_fileModel = nullptr;
  NotebookNodeProxyModel *m_fileProxyModel = nullptr;
  NotebookNodeView *m_fileView = nullptr;
  NotebookNodeDelegate *m_fileDelegate = nullptr;
  NotebookNodeController *m_fileController = nullptr;

  // UI Components
  TitleBar *m_titleBar = nullptr;
  NotebookSelector *m_notebookSelector = nullptr;
  QStackedWidget *m_contentStack = nullptr;
  QSplitter *m_twoColumnsSplitter = nullptr;

  // State
  ExploreMode m_exploreMode = Combined;
  NotebookMgr *m_notebookMgr = nullptr;
  QSharedPointer<Notebook> m_currentNotebook;
};

} // namespace vnotex

#endif // NOTEBOOKEXPLORER2_H
