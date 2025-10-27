#ifndef INOTEBOOKCONFIGMGR_H
#define INOTEBOOKCONFIGMGR_H

#include <QObject>
#include <QSharedPointer>

#include "notebook/node.h"
#include "notebook/nodevisual.h"

namespace vnotex {
class NotebookConfig;
class INotebookBackend;
class NotebookParameters;
class Notebook;
class NodeParameters;

// Abstract class for notebook config manager, which is responsible for config
// files access and note nodes access.
class INotebookConfigMgr : public QObject {
  Q_OBJECT
public:
  INotebookConfigMgr(const QSharedPointer<INotebookBackend> &p_backend,
                     QObject *p_parent = nullptr);

  virtual ~INotebookConfigMgr();

  virtual QString getName() const = 0;

  virtual QString getDisplayName() const = 0;

  virtual QString getDescription() const = 0;

  // Create an empty skeleton for an empty notebook.
  virtual void createEmptySkeleton(const NotebookParameters &p_paras) = 0;

  const QSharedPointer<INotebookBackend> &getBackend() const;

  virtual QSharedPointer<Node> loadRootNode() = 0;

  virtual void loadNode(Node *p_node) = 0;
  virtual void saveNode(const Node *p_node) = 0;

  virtual void renameNode(Node *p_node, const QString &p_name) = 0;

  virtual QSharedPointer<Node> newNode(Node *p_parent, Node::Flags p_flags, const QString &p_name,
                                       const QString &p_content) = 0;

  virtual QSharedPointer<Node> addAsNode(Node *p_parent, Node::Flags p_flags, const QString &p_name,
                                         const NodeParameters &p_paras) = 0;

  virtual QSharedPointer<Node> copyAsNode(Node *p_parent, Node::Flags p_flags,
                                          const QString &p_path) = 0;

  Notebook *getNotebook() const;
  void setNotebook(Notebook *p_notebook);

  virtual QSharedPointer<Node> loadNodeByPath(const QSharedPointer<Node> &p_root,
                                              const QString &p_relativePath) = 0;

  virtual QSharedPointer<Node> copyNodeAsChildOf(const QSharedPointer<Node> &p_src, Node *p_dest,
                                                 bool p_move) = 0;

  virtual void removeNode(const QSharedPointer<Node> &p_node, bool p_force, bool p_configOnly) = 0;

  virtual void removeNodeToFolder(const QSharedPointer<Node> &p_node,
                                  const QString &p_destFolder) = 0;

  // Whether @p_name is a built-in file under @p_node.
  virtual bool isBuiltInFile(const Node *p_node, const QString &p_name) const = 0;

  virtual bool isBuiltInFolder(const Node *p_node, const QString &p_name) const = 0;

  virtual QString fetchNodeAttachmentFolderPath(Node *p_node) = 0;

  virtual QVector<QSharedPointer<ExternalNode>> fetchExternalChildren(Node *p_node) const = 0;

  virtual bool checkNodeExists(Node *p_node) = 0;

  virtual QStringList scanAndImportExternalFiles(Node *p_node) = 0;

  virtual void updateNodeVisual(Node *p_node, const NodeVisual &p_visual) = 0;

  // Version of the config processing code.
  virtual int getCodeVersion() const = 0;

  virtual QString getConfigFolderPath() const = 0;

private:
  QSharedPointer<INotebookBackend> m_backend;

  Notebook *m_notebook = nullptr;
};
} // namespace vnotex

#endif // INOTEBOOKCONFIGMGR_H
