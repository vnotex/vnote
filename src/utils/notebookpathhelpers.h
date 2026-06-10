#ifndef NOTEBOOKPATHHELPERS_H
#define NOTEBOOKPATHHELPERS_H

#include <QString>

namespace vnotex {

class ServiceLocator;

// Resolve a Buffer's parent directory absolute path for fs-watcher
// suppression. Returns empty if the buffer is invalid, the buffer
// belongs to a different notebook than @p_currentNotebookId, or
// required services are unavailable.
QString resolveExpectFsChangePathForBuffer(ServiceLocator &p_services, const QString &p_bufferId,
                                           const QString &p_currentNotebookId);

// Returns true iff @p_path is the notebook root or a descendant of it.
// Avoids prefix collisions ("/notebooks/foo" must not match "/notebooks/foobar").
bool isPathUnderNotebookRoot(ServiceLocator &p_services, const QString &p_notebookId,
                             const QString &p_path);

} // namespace vnotex

#endif // NOTEBOOKPATHHELPERS_H
