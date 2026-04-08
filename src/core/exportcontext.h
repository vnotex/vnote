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

  bool isValid() const { return currentNodeId.isValid(); }
};

} // namespace vnotex

#endif // EXPORTCONTEXT_H
