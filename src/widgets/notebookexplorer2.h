#ifndef NOTEBOOKEXPLORER2_H
#define NOTEBOOKEXPLORER2_H

#include <QFrame>
#include <QSharedPointer>

#include <core/global.h>
#include <nodeinfo.h>
#include <core/noncopyable.h>

class QStackedWidget;
class QSplitter;
class QMenu;
class QActionGroup;

namespace vnotex {


class NotebookSelector2;
class TitleBar;
class NotebookNodeModel;
class NotebookNodeProxyModel;
class NotebookNodeView;
class NotebookNodeDelegate;
class NotebookNodeController;
class TwoColumnsNodeExplorer;
struct FileOpenParameters;
class Event;
class ServiceLocator;

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
    Combined,   // Single tree with folders and files
    TwoColumns  // Left: folders only, Right: files in selected folder
  };

  explicit NotebookExplorer2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~NotebookExplorer2() override;

  // Notebook management - using notebook ID instead of Notebook pointer
  void setCurrentNotebook(const QString &p_notebookId);
  QString currentNotebookId() const;

  // Navigation - using NodeIdentifier instead of Node*
  void setCurrentNode(const NodeIdentifier &p_nodeId);
  NodeIdentifier currentNodeId() const;

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

public slots:
  // Notebook operations - these launch dialogs
  void newNotebook();
  void newNotebookFromFolder();
  void importNotebook();
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

signals:
  // Emitted when user selects a notebook from the selector
  void notebookActivated(const QString &p_notebookGuid);

  // --- New architecture signals (NodeIdentifier-based) ---
  void nodeActivated(const NodeIdentifier &p_nodeId,
                     const QSharedPointer<FileOpenParameters> &p_paras);
  void fileActivated(const QString &p_path, const QSharedPointer<FileOpenParameters> &p_paras);
  void nodeAboutToMove(const NodeIdentifier &p_nodeId, const QSharedPointer<Event> &p_event);
  void nodeAboutToRemove(const NodeIdentifier &p_nodeId, const QSharedPointer<Event> &p_event);
  void nodeAboutToReload(const NodeIdentifier &p_nodeId, const QSharedPointer<Event> &p_event);
  void closeFileRequested(const QString &p_filePath, const QSharedPointer<Event> &p_event);

private slots:
  void onNodeActivated(const NodeIdentifier &p_nodeId,
                       const QSharedPointer<FileOpenParameters> &p_paras);
  void onContextMenuRequested(const NodeIdentifier &p_nodeId, const QPoint &p_globalPos);

  // TwoColumns mode: context menu handler
  void onTwoColumnsContextMenu(const NodeIdentifier &p_nodeId, const QPoint &p_globalPos,
                               bool p_isFromFileView);

  // GUI request handlers from controller signals
  void onNewNoteRequested(const NodeIdentifier &p_parentId);
  void onNewFolderRequested(const NodeIdentifier &p_parentId);
  void onRenameRequested(const NodeIdentifier &p_nodeId, const QString &p_currentName);
  void onDeleteRequested(const QList<NodeIdentifier> &p_nodeIds, bool p_permanent);
  void onRemoveFromNotebookRequested(const QList<NodeIdentifier> &p_nodeIds);
  void onPropertiesRequested(const NodeIdentifier &p_nodeId);
  void onErrorOccurred(const QString &p_title, const QString &p_message);
  void onInfoMessage(const QString &p_title, const QString &p_message);

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
  // Apply view order to all node views
  void setNodeViewOrder(ViewOrder p_order);


  // Services
  ServiceLocator &m_services;

  // MVC Components - Combined mode
  NotebookNodeModel *m_combinedModel = nullptr;
  NotebookNodeProxyModel *m_combinedProxyModel = nullptr;
  NotebookNodeView *m_combinedView = nullptr;
  NotebookNodeDelegate *m_combinedDelegate = nullptr;
  NotebookNodeController *m_combinedController = nullptr;

  // UI Components

  // UI Components
  TitleBar *m_titleBar = nullptr;
  NotebookSelector2 *m_notebookSelector = nullptr;
  QStackedWidget *m_contentStack = nullptr;

  // TwoColumns mode encapsulated widget
  TwoColumnsNodeExplorer *m_twoColumnsExplorer = nullptr;

  // State
  ExploreMode m_exploreMode = Combined;
  QString m_currentNotebookId;
};

} // namespace vnotex

#endif // NOTEBOOKEXPLORER2_H
