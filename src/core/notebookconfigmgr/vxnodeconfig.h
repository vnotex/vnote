#ifndef VXNODECONFIG_H
#define VXNODECONFIG_H

#include <QDateTime>
#include <QJsonObject>
#include <QVector>

#include <core/global.h>
#include <notebook/node.h>
#include <notebook/nodeparameters.h>

namespace vnotex {
// Config structures for VXNotebookConfigMgr.
namespace vx_node_config {
// Config of a file child.
struct NodeFileConfig {
  QJsonObject toJson() const;

  void fromJson(const QJsonObject &p_jobj);

  NodeParameters toNodeParameters() const;

  QString m_name;

  ID m_id = Node::InvalidId;

  ID m_signature = Node::InvalidId;

  QDateTime m_createdTimeUtc;

  QDateTime m_modifiedTimeUtc;

  QString m_attachmentFolder;

  QStringList m_tags;

  // Visual settings
  QString m_backgroundColor;
  QString m_borderColor;
  QString m_nameColor;
};

// Config of a folder child.
struct NodeFolderConfig {
  QJsonObject toJson() const;

  void fromJson(const QJsonObject &p_jobj);

  NodeParameters toNodeParameters() const;

  QString m_name;

  // Visual settings
  QString m_backgroundColor;
  QString m_borderColor;
  QString m_nameColor;
};

// Config of a folder node.
struct NodeConfig {
  NodeConfig();

  NodeConfig(int p_version, ID p_id, ID p_signature, const QDateTime &p_createdTimeUtc,
             const QDateTime &p_modifiedTimeUtc);

  QJsonObject toJson() const;

  void fromJson(const QJsonObject &p_jobj);

  NodeParameters toNodeParameters() const;

  int m_version = 0;

  ID m_id = Node::InvalidId;

  ID m_signature = Node::InvalidId;

  QDateTime m_createdTimeUtc;

  QDateTime m_modifiedTimeUtc;

  QVector<NodeFileConfig> m_files;

  QVector<NodeFolderConfig> m_folders;

  // Visual settings for the container node itself
  QString m_backgroundColor;
  QString m_borderColor;
  QString m_nameColor;

  static const QString c_version;

  static const QString c_id;

  static const QString c_signature;

  static const QString c_createdTimeUtc;

  static const QString c_files;

  static const QString c_folders;

  static const QString c_name;

  static const QString c_modifiedTimeUtc;

  static const QString c_attachmentFolder;

  static const QString c_tags;

  static const QString c_backgroundColor;

  static const QString c_borderColor;

  static const QString c_nameColor;
};
} // namespace vx_node_config
} // namespace vnotex

#endif // VXNODECONFIG_H
