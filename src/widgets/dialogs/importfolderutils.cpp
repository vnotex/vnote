#include "importfolderutils.h"

#include <QCoreApplication>
#include <core/exception.h>
#include <notebook/nodeparameters.h>
#include <notebook/notebook.h>
#include <utils/utils.h>

using namespace vnotex;

void ImportFolderUtils::importFolderContents(Notebook *p_notebook, Node *p_node,
                                             const QStringList &p_suffixes, QString &p_errMsg) {
  auto rootDir = p_node->toDir();
  auto children =
      rootDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
  for (const auto &child : children) {
    if (child.isDir()) {
      if (p_notebook->isBuiltInFolder(p_node, child.fileName())) {
        continue;
      }

      QSharedPointer<Node> node;
      try {
        node = p_notebook->addAsNode(p_node, Node::Flag::Container, child.fileName(),
                                     NodeParameters());
      } catch (Exception &p_e) {
        Utils::appendMsg(p_errMsg,
                         ImportFolderUtilsTranslate::tr("Failed to add folder (%1) as node (%2).")
                             .arg(child.fileName(), p_e.what()));
        continue;
      }

      importFolderContents(p_notebook, node.data(), p_suffixes, p_errMsg);
    } else if (!p_notebook->isBuiltInFile(p_node, child.fileName())) {
      if (p_suffixes.contains(child.suffix())) {
        try {
          p_notebook->addAsNode(p_node, Node::Flag::Content, child.fileName(), NodeParameters());
        } catch (Exception &p_e) {
          Utils::appendMsg(p_errMsg,
                           ImportFolderUtilsTranslate::tr("Failed to add file (%1) as node (%2).")
                               .arg(child.filePath(), p_e.what()));
        }
      }
    }
  }
}
