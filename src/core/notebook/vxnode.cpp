#include "vxnode.h"

#include <QDir>

#include "notebook.h"
#include "vxnodefile.h"
#include <notebookbackend/inotebookbackend.h>
#include <notebookconfigmgr/inotebookconfigmgr.h>
#include <utils/pathutils.h>

using namespace vnotex;

VXNode::VXNode(const QString &p_name, const NodeParameters &p_paras, Notebook *p_notebook,
               Node *p_parent)
    : Node(Node::Flag::Content, p_name, p_paras, p_notebook, p_parent) {}

VXNode::VXNode(const QString &p_name, Notebook *p_notebook, Node *p_parent)
    : Node(Node::Flag::Container, p_name, p_notebook, p_parent) {}

QString VXNode::fetchAbsolutePath() const {
  return PathUtils::concatenateFilePath(m_notebook->getRootFolderAbsolutePath(), fetchPath());
}

QSharedPointer<File> VXNode::getContentFile() {
  Q_ASSERT(hasContent());
  // We should not keep the shared ptr of VXNodeFile, or there is a cyclic ref.
  return QSharedPointer<VXNodeFile>::create(sharedFromThis().dynamicCast<VXNode>());
}

QStringList VXNode::addAttachment(const QString &p_destFolderPath, const QStringList &p_files) {
  Q_ASSERT(PathUtils::pathContains(fetchAttachmentFolderPath(), p_destFolderPath));

  auto backend = getBackend();
  QStringList addedFiles;
  for (const auto &file : p_files) {
    if (PathUtils::isDir(file)) {
      qWarning() << "skip adding folder as attachment" << file;
      continue;
    }

    auto destFilePath = backend->renameIfExistsCaseInsensitive(
        PathUtils::concatenateFilePath(p_destFolderPath, PathUtils::fileName(file)));
    backend->copyFile(file, destFilePath);
    addedFiles << destFilePath;
  }

  return addedFiles;
}

QString VXNode::newAttachmentFile(const QString &p_destFolderPath, const QString &p_name) {
  Q_ASSERT(PathUtils::pathContains(fetchAttachmentFolderPath(), p_destFolderPath));

  auto backend = getBackend();
  auto destFilePath = backend->renameIfExistsCaseInsensitive(
      PathUtils::concatenateFilePath(p_destFolderPath, p_name));
  backend->writeFile(destFilePath, QByteArray());
  return destFilePath;
}

QString VXNode::newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name) {
  Q_ASSERT(PathUtils::pathContains(fetchAttachmentFolderPath(), p_destFolderPath));

  auto backend = getBackend();
  auto destFilePath = backend->renameIfExistsCaseInsensitive(
      PathUtils::concatenateFilePath(p_destFolderPath, p_name));
  backend->makePath(destFilePath);
  return destFilePath;
}

QString VXNode::renameAttachment(const QString &p_path, const QString &p_name) {
  Q_ASSERT(PathUtils::pathContains(fetchAttachmentFolderPath(), p_path));
  getBackend()->renameFile(p_path, p_name);
  return p_name;
}

void VXNode::removeAttachment(const QStringList &p_paths) {
  auto attaFolderPath = fetchAttachmentFolderPath();
  // Just move it to recycle bin but not added as a child node of recycle bin.
  for (const auto &pa : p_paths) {
    Q_ASSERT(PathUtils::pathContains(attaFolderPath, pa));
    if (QFileInfo(pa).isDir()) {
      m_notebook->moveDirToRecycleBin(pa);
    } else {
      m_notebook->moveFileToRecycleBin(pa);
    }
  }
}
