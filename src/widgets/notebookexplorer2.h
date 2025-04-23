#ifndef NOTEBOOKEXPLORER2_H
#define NOTEBOOKEXPLORER2_H

#include <QFrame>
#include <QHash>
#include <QVBoxLayout>

#include <core/global.h>
#include <core/noncopyable.h>
#include <nodeinfo.h>
#include <views/inodeexplorer.h>

class QFileSystemWatcher;
class QSplitter;
class QMenu;
class QActionGroup;
class QTimer;

namespace vnotex {

class NotebookSelector2;
class TitleBar;
class INodeExplorer;
class TwoColumnsNodeExplorer;
class ServiceLocator;
struct FileOpenSettings;

// NotebookExplorer2 is a container widget that displays notebook nodes
// using proper MVC architecture.
// It supports two explore modes:
// - Combined: Single tree view showing all nodes
// - TwoColumns: Split view with folder tree on left, file list on right
//
// NOTE: This class uses NodeIdentifier/NodeInfo for all node references.
// Legacy Node* pointers are being phased out.
class NotebookExplorer2 : public QFrame, private Noncopyable {
  Q_OBJECT

public:
  enum ExploreMode {
    Combined,  // Single tree with folders and files
    TwoColumns // Left: folders only, Right: files in selected folder
  };

  explicit NotebookExplorer2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~NotebookExplorer2() override;

  // Notebook management - using notebook ID instead of Notebook pointer
  void setCurrentNotebook(const QString &p_notebookId);
  QString currentNotebookId() const;

  // Navigation - using NodeIdentifier instead of Node*
  void setCurrentNode(const NodeIdentifier &p_nodeId);
  NodeIdentifier currentNodeId() const;

  NotebookSelector2 *getNotebookSelector() const;
  INodeExplorer *getNodeExplorer() const;

  // Mode switching
  void setExploreMode(ExploreMode p_mode);
  ExploreMode exploreMode() const;

  // Get current explored folder node (for newNote/newFolder operations)
  NodeIdentifier currentExploredFolderId() const;

  // Get current explored node
  NodeIdentifier currentExploredNodeId() const;

  // State saving/restoring for session
  QByteArray saveState() const;
  void restoreState(const QByteArray &p_data);
  void applyRestoredSessionState();

public slots:
  // Notebook operations - these launch dialogs
  void newNotebook();
  void newNotebookFromFolder();
  void importNotebook();
  void openVNote3Notebook();
  void manageNotebooks();

  // Reload operations
  void loadNotebooks();

  // Node creation - use current explored folder as parent
  void newFolder();
  void newNote();
  void newQuickNote();

  // Import operations - use current explored folder as parent
  void importFile();
  void importFolder();

  // Locate a node in the explorer (switch notebook if needed, expand, select, scroll).
  void locateNode(const NodeIdentifier &p_nodeId);

private slots:
  // Node activation — opens the file via BufferService
  void onNodeActivated(const NodeIdentifier &p_nodeId, const FileOpenSettings &p_settings);

  // GUI request handlers from controller signals
  void onNewNoteRequested(const NodeIdentifier &p_parentId);
  void onNewFolderRequested(const NodeIdentifier &p_parentId);
  void onRenameRequested(const NodeIdentifier &p_nodeId, const QString &p_currentName);
  void onDeleteRequested(const QList<NodeIdentifier> &p_nodeIds, bool p_permanent);
  void onRemoveFromNotebookRequested(const QList<NodeIdentifier> &p_nodeIds);
  void onPropertiesRequested(const NodeIdentifier &p_nodeId);
  void onMarkRequested(const NodeIdentifier &p_nodeId);
  void onIgnoreRequested(const NodeIdentifier &p_nodeId);
  void onManageTagsRequested(const NodeIdentifier &p_nodeId);
  void onErrorOccurred(const QString &p_title, const QString &p_message);
  void onInfoMessage(const QString &p_title, const QString &p_message);

signals:
  void currentNotebookChanged(const QString &p_notebookId);
  void currentExploredFolderChanged(const NodeIdentifier &p_folderId);
  void exportNodeRequested(const NodeIdentifier &p_nodeId);

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

  // Import file/folder helpers (called from toolbar actions)
  void onImportFilesRequested(const NodeIdentifier &p_targetFolderId);
  void onImportFolderRequested(const NodeIdentifier &p_targetFolderId);
  void setCurrentNotebookInternal(const QString &p_notebookId);
  void updateRecycleBinMenuState();
  void cacheCurrentExplorerState();
  void applyCachedExplorerState(const QString &p_notebookId);
  // Apply view order to all node views
  void setNodeViewOrder(ViewOrder p_order);

  // File system watcher methods
  void setupFileWatcher();
  void teardownFileWatcher();
  void onFileSystemChanged(const QString &p_path);
  void onFsReloadTimeout();
  void addWatchPath(const QString &p_path);
  void removeWatchPath(const QString &p_path);
  void syncWatchedPaths();

  // Services
  ServiceLocator &m_services;

  // Node explorer - polymorphic, recreated when mode changes
  INodeExplorer *m_nodeExplorer = nullptr;

  // UI Components
  TitleBar *m_titleBar = nullptr;
  NotebookSelector2 *m_notebookSelector = nullptr;
  QVBoxLayout *m_mainLayout = nullptr;

  // State
  ExploreMode m_exploreMode = Combined;
  QString m_currentNotebookId;
  QHash<QString, NodeExplorerState> m_notebookStateCache;
  bool m_hasRestoredSessionStatePending = false;
  QAction *m_openRecycleBinAction = nullptr;
  QAction *m_emptyRecycleBinAction = nullptr;

  // File system watcher for detecting external changes
  QFileSystemWatcher *m_fsWatcher = nullptr;
  QTimer *m_fsReloadTimer = nullptr;
  QString m_lastChangedDir;
};

} // namespace vnotex

#endif // NOTEBOOKEXPLORER2_H
