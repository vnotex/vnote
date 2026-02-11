#ifndef NOTEBOOKNODECONTROLLER_H
#define NOTEBOOKNODECONTROLLER_H

#include <QList>
#include <QObject>
#include <QSharedPointer>

class QMenu;

namespace vnotex {

class Node;
class Notebook;
class NotebookNodeModel;
class NotebookNodeView;
struct FileOpenParameters;
class Event;

// Controller class that handles all user actions on notebook nodes.
// Acts as the bridge between View and Model, implementing business logic.
class NotebookNodeController : public QObject {
  Q_OBJECT

public:
  explicit NotebookNodeController(QObject *p_parent = nullptr);
  ~NotebookNodeController() override;

  // Set the model this controller operates on
  void setModel(NotebookNodeModel *p_model);
  NotebookNodeModel *model() const;

  // Set the view this controller manages
  void setView(NotebookNodeView *p_view);
  NotebookNodeView *view() const;

  // Context menu creation
  QMenu *createContextMenu(Node *p_node, QWidget *p_parent = nullptr);

  // Node operations
  void newNote(Node *p_parentNode);
  void newFolder(Node *p_parentNode);
  void openNode(Node *p_node);
  void openNodeWithDefaultApp(Node *p_node);
  void deleteNodes(const QList<Node *> &p_nodes);
  void removeNodesFromNotebook(const QList<Node *> &p_nodes);
  void copyNodes(const QList<Node *> &p_nodes);
  void cutNodes(const QList<Node *> &p_nodes);
  void pasteNodes(Node *p_targetFolder);
  void duplicateNode(Node *p_node);
  void renameNode(Node *p_node);
  void moveNodes(const QList<Node *> &p_nodes, Node *p_targetFolder);

  // Import/Export
  void importFiles(Node *p_targetFolder);
  void importFolder(Node *p_targetFolder);
  void exportNode(Node *p_node);

  // Properties and info
  void showNodeProperties(Node *p_node);
  void copyNodePath(Node *p_node);
  void locateNodeInFileManager(Node *p_node);

  // Sorting and reload
  void sortNodes(Node *p_parentNode);
  void reloadNode(Node *p_node);
  void reloadAll();

  // Pin/Tag operations
  void pinNodeToQuickAccess(Node *p_node);
  void manageNodeTags(Node *p_node);

  // Check if clipboard has nodes to paste
  bool canPaste() const;

signals:
  // Signals to notify external components (e.g., BufferMgr)
  void nodeActivated(Node *p_node, const QSharedPointer<FileOpenParameters> &p_paras);
  void fileActivated(const QString &p_path, const QSharedPointer<FileOpenParameters> &p_paras);
  void nodeAboutToMove(Node *p_node, const QSharedPointer<Event> &p_event);
  void nodeAboutToRemove(Node *p_node, const QSharedPointer<Event> &p_event);
  void nodeAboutToReload(Node *p_node, const QSharedPointer<Event> &p_event);
  void closeFileRequested(const QString &p_filePath, const QSharedPointer<Event> &p_event);

private:
  // Add actions to context menu based on node type
  void addNewActions(QMenu *p_menu, Node *p_node);
  void addOpenActions(QMenu *p_menu, Node *p_node);
  void addEditActions(QMenu *p_menu, Node *p_node);
  void addCopyMoveActions(QMenu *p_menu, Node *p_node);
  void addImportExportActions(QMenu *p_menu, Node *p_node);
  void addInfoActions(QMenu *p_menu, Node *p_node);
  void addMiscActions(QMenu *p_menu, Node *p_node);

  // Confirmation dialogs
  bool confirmDelete(const QList<Node *> &p_nodes, bool p_permanent);

  // Notify BufferMgr before node operations
  void notifyBeforeNodeOperation(Node *p_node, const QString &p_operation);

  // Get current notebook
  QSharedPointer<Notebook> currentNotebook() const;

  NotebookNodeModel *m_model = nullptr;
  NotebookNodeView *m_view = nullptr;

  // Clipboard state
  QList<Node *> m_clipboardNodes;
  bool m_isCut = false;
};

} // namespace vnotex

#endif // NOTEBOOKNODECONTROLLER_H
