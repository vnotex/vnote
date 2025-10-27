#ifndef VXNODE_H
#define VXNODE_H

#include "node.h"

namespace vnotex {
// Node of VXNotebookConfigMgr.
class VXNode : public Node {
public:
  // For content node.
  VXNode(const QString &p_name, const NodeParameters &p_paras, Notebook *p_notebook,
         Node *p_parent);

  // For container node.
  VXNode(const QString &p_name, Notebook *p_notebook, Node *p_parent);

  QString fetchAbsolutePath() const Q_DECL_OVERRIDE;

  QSharedPointer<File> getContentFile() Q_DECL_OVERRIDE;

  QStringList addAttachment(const QString &p_destFolderPath,
                            const QStringList &p_files) Q_DECL_OVERRIDE;

  QString newAttachmentFile(const QString &p_destFolderPath, const QString &p_name) Q_DECL_OVERRIDE;

  QString newAttachmentFolder(const QString &p_destFolderPath,
                              const QString &p_name) Q_DECL_OVERRIDE;

  QString renameAttachment(const QString &p_path, const QString &p_name) Q_DECL_OVERRIDE;

  void removeAttachment(const QStringList &p_paths) Q_DECL_OVERRIDE;
};
} // namespace vnotex

#endif // VXNODE_H
