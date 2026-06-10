#include "notebookpathhelpers.h"

#include <QDir>
#include <QFileInfo>

#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/notebookcoreservice.h>

namespace vnotex {

QString resolveExpectFsChangePathForBuffer(ServiceLocator &p_services, const QString &p_bufferId,
                                           const QString &p_currentNotebookId) {
  auto *bufSvc = p_services.get<BufferService>();
  if (!bufSvc)
    return QString();
  // getBufferHandle does NOT fire FileBefore/AfterOpen hooks (unlike
  // BufferService::openBuffer); it just returns a Buffer2 view of an
  // already-open buffer. CORRECT API for resolving an active buffer.
  Buffer2 buf = bufSvc->getBufferHandle(p_bufferId);
  if (!buf.isValid())
    return QString();
  const NodeIdentifier &nid = buf.nodeId();
  if (nid.notebookId != p_currentNotebookId)
    return QString();
  auto *nbSvc = p_services.get<NotebookCoreService>();
  if (!nbSvc)
    return QString();
  QString parentRel = QFileInfo(nid.relativePath).path();
  if (parentRel == QStringLiteral("."))
    parentRel.clear();
  return nbSvc->buildAbsolutePath(nid.notebookId, parentRel);
}

bool isPathUnderNotebookRoot(ServiceLocator &p_services, const QString &p_notebookId,
                             const QString &p_path) {
  if (p_notebookId.isEmpty() || p_path.isEmpty())
    return false;
  auto *nbSvc = p_services.get<NotebookCoreService>();
  if (!nbSvc)
    return false;
  const QString rootAbs =
      QDir::cleanPath(QDir(nbSvc->buildAbsolutePath(p_notebookId, QString())).absolutePath());
  const QString pathAbs = QDir::cleanPath(QDir(p_path).absolutePath());
  // Append path separator to avoid prefix collisions.
  return pathAbs == rootAbs || pathAbs.startsWith(rootAbs + QLatin1Char('/'));
}

} // namespace vnotex
