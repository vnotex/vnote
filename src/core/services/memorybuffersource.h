#ifndef MEMORYBUFFERSOURCE_H
#define MEMORYBUFFERSOURCE_H

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <functional>
#include <vxcore/vxcore_types.h>

namespace vnotex {

// An application-owned document, independent of a filesystem path. Opening
// transfers the callbacks into the buffer; duplicate opens retain the first source.
// Load runs on the caller thread. Save runs on a worker, serially per buffer.
// Captures must outlive their operations, tolerate worker destruction, and must
// not access GUI objects or reenter buffer APIs. Return errors, never false success.
struct MemoryBufferSource {
  QString address; // Stable vx:// identity, not a path to open on disk.
  QString displayName;
  QString editorType;       // Explicit ViewWindowFactory creator key.
  QString resourceBasePath; // Optional absolute directory for relative reads.
  QByteArray content;       // Initial content when load is absent.
  bool readOnly = false;

  // Empty output is valid. Failure must leave the existing document untouched.
  std::function<VxCoreError(QByteArray &)> load;
  // Bytes are borrowed for this call only. Success means this snapshot is saved.
  // Without a saver the document is editable scratch content, not durable.
  std::function<VxCoreError(QByteArrayView)> save;
};

} // namespace vnotex

#endif // MEMORYBUFFERSOURCE_H
