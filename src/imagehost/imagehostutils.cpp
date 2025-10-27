#include "imagehostutils.h"

#include <buffer/buffer.h>
#include <notebook/node.h>
#include <notebook/notebook.h>
#include <notebookbackend/inotebookbackend.h>
#include <utils/pathutils.h>

using namespace vnotex;

QString ImageHostUtils::generateRelativePath(const Buffer *p_buffer) {
  QString relativePath;

  // To avoid leaking any private information, for external files, we won't add path to it.
  if (auto node = p_buffer->getNode()) {
    auto notebook = node->getNotebook();
    auto name = notebook->getName();
    if (name.isEmpty() || !PathUtils::isLegalFileName(name)) {
      name = QStringLiteral("vx_notebooks");
    }

    relativePath = name;
    relativePath += "/" + notebook->getBackend()->getRelativePath(p_buffer->getPath());
    relativePath = relativePath.toLower();
  }

  return relativePath;
}
