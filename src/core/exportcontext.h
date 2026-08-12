#ifndef EXPORTCONTEXT_H
#define EXPORTCONTEXT_H

#include <QString>

#include <core/nodeidentifier.h>
#include <export/exportdata.h>

namespace vnotex {

// Plain value type carrying context from MainWindow2 to ExportDialog2.
// Replaces legacy Notebook*/Node*/Buffer* constructor parameters.
struct ExportContext {
  NodeIdentifier currentNodeId;   // Currently selected/active node
  NodeIdentifier currentFolderId; // Currently selected/browsed folder
  QString notebookId;             // Current notebook
  QString bufferContent;          // Live content from ViewWindow2::getLatestContent()
  QString bufferName;             // Name of the open buffer (display/output naming)
  ExportSource presetSource = ExportSource::CurrentBuffer; // Pre-selected source

  QString bufferPath; // Absolute path of the open FILE-backed buffer. Empty when no editor is
                      // open or the active view is virtual. The only handle on an external file,
                      // whose currentNodeId is invalid by design.

  // Whether there is an open buffer that export can actually resolve to a file.
  bool hasFileBuffer() const { return !bufferPath.isEmpty(); }

  bool isValid() const { return currentNodeId.isValid(); }
};

} // namespace vnotex

#endif // EXPORTCONTEXT_H
