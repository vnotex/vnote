#ifndef NODE_H
#define NODE_H

#include <QDateTime>
#include <QDir>
#include <QEnableSharedFromThis>
#include <QSharedPointer>
#include <QVector>

#include "nodevisual.h"
#include <global.h>

namespace vnotex {
class Notebook;
class INotebookConfigMgr;
class INotebookBackend;
class File;
class ExternalNode;
class NodeParameters;

// Node of notebook.
class Node : public QEnableSharedFromThis<Node> {
public:
  enum Flag {
    None = 0,
    // A node with content.
    Content = 0x1,
    // A node with children.
    Container = 0x2,
    ReadOnly = 0x4,
    // Whether a node exists on disk.
    Exists = 0x10
  };
  Q_DECLARE_FLAGS(Flags, Flag)

  enum Use { Normal, Root };

  enum { InvalidId = 0 };

  // Constructor with all information loaded.
  Node(Flags p_flags, const QString &p_name, const NodeParameters &p_paras, Notebook *p_notebook,
       Node *p_parent);

  // Constructor not loaded.
  Node(Flags p_flags, const QString &p_name, Notebook *p_notebook, Node *p_parent);

  virtual ~Node();

  bool isLoaded() const;

  bool isRoot() const;

  const QString &getName() const;
  void setName(const QString &p_name);

  void updateName(const QString &p_name);

  // Fetch path of this node within notebook.
  // This may not be the same as the actual file path. It depends on the config mgr.
  virtual QString fetchPath() const;

  // Fetch absolute file path if available.
  virtual QString fetchAbsolutePath() const = 0;

  bool isContainer() const;

  bool hasContent() const;

  // Whether the node exists on disk (without real check).
  bool exists() const;

  bool checkExists();

  void setExists(bool p_exists);

  Node::Flags getFlags() const;

  Node::Use getUse() const;
  void setUse(Node::Use p_use);

  ID getId() const;
  void updateId(ID p_id);

  ID getSignature() const;

  const QDateTime &getCreatedTimeUtc() const;

  const QDateTime &getModifiedTimeUtc() const;
  void setModifiedTimeUtc();

  const QVector<QSharedPointer<Node>> &getChildrenRef() const;
  QVector<QSharedPointer<Node>> getChildren() const;
  int getChildrenCount() const;

  QSharedPointer<Node> findChild(const QString &p_name, bool p_caseSensitive = true) const;

  bool containsChild(const QString &p_name, bool p_caseSensitive = true) const;

  bool containsChild(const QSharedPointer<Node> &p_node) const;

  // Case sensitive.
  bool containsContainerChild(const QString &p_name) const;

  // Case sensitive.
  bool containsContentChild(const QString &p_name) const;

  bool isLegalNameForNewChild(const QString &p_name) const;

  void addChild(const QSharedPointer<Node> &p_node);

  void insertChild(int p_idx, const QSharedPointer<Node> &p_node);

  void removeChild(const QSharedPointer<Node> &p_node);

  QVector<QSharedPointer<ExternalNode>> fetchExternalChildren() const;

  void setParent(Node *p_parent);
  Node *getParent() const;

  Notebook *getNotebook() const;

  virtual void load();
  virtual void save();

  const QStringList &getTags() const;
  void updateTags(const QStringList &p_tags);

  const QString &getAttachmentFolder() const;
  void setAttachmentFolder(const QString &p_attachmentFolder);

  QString fetchAttachmentFolderPath();

  // 视觉效果相关方法
  const NodeVisual &getVisual() const;
  void setVisual(const NodeVisual &p_visual);

  // 视觉效果便捷访问方法
  const QString &getBackgroundColor() const;
  void setBackgroundColor(const QString &p_backgroundColor);

  const QString &getBorderColor() const;
  void setBorderColor(const QString &p_borderColor);

  const QString &getNameColor() const;
  void setNameColor(const QString &p_nameColor);

  // 获取有效颜色（直接返回设置的颜色）
  QString getEffectiveBackgroundColor() const;
  QString getEffectiveBorderColor() const;

  void updateNodeVisual(const NodeVisual &p_visual);

  virtual QStringList addAttachment(const QString &p_destFolderPath,
                                    const QStringList &p_files) = 0;

  virtual QString newAttachmentFile(const QString &p_destFolderPath, const QString &p_name) = 0;

  virtual QString newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name) = 0;

  virtual QString renameAttachment(const QString &p_path, const QString &p_name) = 0;

  virtual void removeAttachment(const QStringList &p_paths) = 0;

  QDir toDir() const;

  bool isReadOnly() const;
  void setReadOnly(bool p_readOnly);

  // Get File if this node has content.
  virtual QSharedPointer<File> getContentFile() = 0;

  void loadCompleteInfo(const NodeParameters &p_paras,
                        const QVector<QSharedPointer<Node>> &p_children);

  INotebookConfigMgr *getConfigMgr() const;

  INotebookBackend *getBackend() const;

  bool canRename(const QString &p_newName) const;

  void sortChildren(const QVector<int> &p_beforeIdx, const QVector<int> &p_afterIdx);

  // Get content files recursively.
  QList<QSharedPointer<File>> collectFiles();

  static bool isAncestor(const Node *p_ancestor, const Node *p_child);

  static ID generateSignature();

protected:
  Notebook *m_notebook = nullptr;

  bool m_loaded = false;

private:
  void checkSignature();

  Flags m_flags = Flag::None;

  Use m_use = Use::Normal;

  ID m_id = InvalidId;

  // A long random number created when the node is created.
  // Use to avoid conflicts of m_id.
  ID m_signature = InvalidId;

  QString m_name;

  QDateTime m_createdTimeUtc;

  QDateTime m_modifiedTimeUtc;

  QStringList m_tags;

  QString m_attachmentFolder;

  NodeVisual m_visual;

  Node *m_parent = nullptr;

  QVector<QSharedPointer<Node>> m_children;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Node::Flags)
} // namespace vnotex

#endif // NODE_H
