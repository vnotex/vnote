#ifndef VXNOTEBOOKCONFIGMGR_H
#define VXNOTEBOOKCONFIGMGR_H

#include "bundlenotebookconfigmgr.h"

#include <QDateTime>
#include <QRegExp>
#include <QVector>

#include <core/global.h>

class QJsonObject;

namespace vnotex {
namespace vx_node_config {
struct NodeFileConfig;
struct NodeFolderConfig;
struct NodeConfig;
} // namespace vx_node_config

class NotebookDatabaseAccess;

// Config manager for VNoteX's bundle notebook.
class VXNotebookConfigMgr : public BundleNotebookConfigMgr {
  Q_OBJECT
public:
  VXNotebookConfigMgr(const QSharedPointer<INotebookBackend> &p_backend,
                      QObject *p_parent = nullptr);

  QString getName() const Q_DECL_OVERRIDE;

  QString getDisplayName() const Q_DECL_OVERRIDE;

  QString getDescription() const Q_DECL_OVERRIDE;

  void createEmptySkeleton(const NotebookParameters &p_paras) Q_DECL_OVERRIDE;

  QSharedPointer<Node> loadRootNode() Q_DECL_OVERRIDE;

  void loadNode(Node *p_node) Q_DECL_OVERRIDE;
  void saveNode(const Node *p_node) Q_DECL_OVERRIDE;

  void renameNode(Node *p_node, const QString &p_name) Q_DECL_OVERRIDE;

  QSharedPointer<Node> newNode(Node *p_parent, Node::Flags p_flags, const QString &p_name,
                               const QString &p_content) Q_DECL_OVERRIDE;

  QSharedPointer<Node> addAsNode(Node *p_parent, Node::Flags p_flags, const QString &p_name,
                                 const NodeParameters &p_paras) Q_DECL_OVERRIDE;

  QSharedPointer<Node> copyAsNode(Node *p_parent, Node::Flags p_flags,
                                  const QString &p_path) Q_DECL_OVERRIDE;

  QSharedPointer<Node> loadNodeByPath(const QSharedPointer<Node> &p_root,
                                      const QString &p_relativePath) Q_DECL_OVERRIDE;

  QSharedPointer<Node> copyNodeAsChildOf(const QSharedPointer<Node> &p_src, Node *p_dest,
                                         bool p_move) Q_DECL_OVERRIDE;

  void removeNode(const QSharedPointer<Node> &p_node, bool p_force = false,
                  bool p_configOnly = false) Q_DECL_OVERRIDE;

  void removeNodeToFolder(const QSharedPointer<Node> &p_node,
                          const QString &p_destFolder) Q_DECL_OVERRIDE;

  bool isBuiltInFile(const Node *p_node, const QString &p_name) const Q_DECL_OVERRIDE;

  bool isBuiltInFolder(const Node *p_node, const QString &p_name) const Q_DECL_OVERRIDE;

  QString fetchNodeImageFolderPath(Node *p_node);

  QString fetchNodeAttachmentFolderPath(Node *p_node) Q_DECL_OVERRIDE;

  QVector<QSharedPointer<ExternalNode>> fetchExternalChildren(Node *p_node) const Q_DECL_OVERRIDE;

  bool checkNodeExists(Node *p_node) Q_DECL_OVERRIDE;

  QStringList scanAndImportExternalFiles(Node *p_node) Q_DECL_OVERRIDE;

  void updateNodeVisual(Node *p_node, const NodeVisual &p_visual) Q_DECL_OVERRIDE;

private:
  void createEmptyRootNode();

  QSharedPointer<vx_node_config::NodeConfig> readNodeConfig(const QString &p_path) const;
  void writeNodeConfig(const QString &p_path, const vx_node_config::NodeConfig &p_config) const;

  void writeNodeConfig(const Node *p_node);

  QSharedPointer<Node> nodeConfigToNode(const vx_node_config::NodeConfig &p_config,
                                        const QString &p_name, Node *p_parent = nullptr);

  void loadFolderNode(Node *p_node, const vx_node_config::NodeConfig &p_config);

  QSharedPointer<vx_node_config::NodeConfig> nodeToNodeConfig(const Node *p_node) const;

  QSharedPointer<Node> newFileNode(Node *p_parent, const QString &p_name, const QString &p_content,
                                   bool p_create, const NodeParameters &p_paras);

  QSharedPointer<Node> newFolderNode(Node *p_parent, const QString &p_name, bool p_create,
                                     const NodeParameters &p_paras);

  QString getNodeConfigFilePath(const Node *p_node) const;

  void addChildNode(Node *p_parent, const QSharedPointer<Node> &p_child) const;

  QSharedPointer<Node> copyNodeAsChildOf(const QSharedPointer<Node> &p_src, Node *p_dest,
                                         bool p_move, bool p_updateDatabase);

  QSharedPointer<Node> copyFileNodeAsChildOf(const QSharedPointer<Node> &p_src, Node *p_dest,
                                             bool p_move, bool p_updateDatabase);

  QSharedPointer<Node> copyFolderNodeAsChildOf(const QSharedPointer<Node> &p_src, Node *p_dest,
                                               bool p_move, bool p_updateDatabase);

  QSharedPointer<Node> copyFileAsChildOf(const QString &p_srcPath, Node *p_dest);

  QSharedPointer<Node> copyFolderAsChildOf(const QString &p_srcPath, Node *p_dest);

  void removeFilesOfNode(Node *p_node, bool p_force);

  void markNodeReadOnly(Node *p_node) const;

  void removeLegacyRecycleBinNode(const QSharedPointer<Node> &p_root);

  // Generate node attachment folder.
  // @p_folderName: suggested folder name if not empty, may be renamed due to conflicts.
  // Return the attachment folder path.
  QString fetchNodeAttachmentFolder(const QString &p_nodePath, QString &p_folderName);

  void inheritNodeFlags(const Node *p_node, Node *p_child) const;

  bool isExcludedFromExternalNode(const QString &p_name) const;

  void removeNode(const QSharedPointer<Node> &p_node, bool p_force, bool p_configOnly,
                  bool p_updateDatabase);

  NotebookDatabaseAccess *getDatabaseAccess() const;

  void updateNodeInDatabase(Node *p_node);

  void ensureNodeInDatabase(Node *p_node);

  void addNodeToDatabase(Node *p_node);

  bool nodeExistsInDatabase(const Node *p_node);

  void removeNodeFromDatabase(const Node *p_node);

  bool sameNotebook(const Node *p_node) const;

  void removeFolderNodeToFolder(const QSharedPointer<Node> &p_node, const QString &p_destFolder);

  void removeFileNodeToFolder(const QSharedPointer<Node> &p_node, const QString &p_destFolder);

  void copyFilesOfFileNode(const QSharedPointer<Node> &p_node, const QString &p_destFolder,
                           QString &p_destFilePath, QString &p_attachmentFolder);

  static bool isLikelyImageFolder(const QString &p_dirPath);

  static bool s_initialized;

  static QVector<QRegExp> s_externalNodeExcludePatterns;

  // Name of the node's config file.
  static const QString c_nodeConfigName;
};
} // namespace vnotex

#endif // VXNOTEBOOKCONFIGMGR_H
