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
// Load/save captures must outlive their operations, tolerate worker destruction,
// and must not access GUI objects or reenter buffer APIs. finalizeSave is instead
// owned, invoked and destroyed on the BufferService owner (GUI) thread.
// Return errors, never false success.
struct MemoryBufferSource {
  QString address; // Stable vx:// identity, not a path to open on disk.
  QString displayName;
  QString editorType;       // Explicit ViewWindowFactory creator key.
  QString resourceBasePath; // Optional absolute directory for relative reads.
  QByteArray content;       // Initial content when load is absent.
  bool readOnly = false;
  // Sync still pulls editor drafts, but only explicit Save may persist them.
  bool manualSaveOnly = false;

  // Empty output is valid. Failure must leave the existing document untouched.
  std::function<VxCoreError(QByteArray &)> load;
  // Bytes are borrowed for this call only. Without finalizeSave, success means
  // this snapshot is saved. With finalizeSave, success means only staged: GUI
  // finalization and core completion must both succeed to acknowledge durability.
  // Without a saver the document is editable scratch content, not durable.
  std::function<VxCoreError(QByteArrayView)> save;

  // Optional owner-thread commit of the exact snapshot staged by save. Legal only
  // with save and manualSaveOnly. Runs outside vxcore callbacks, synchronously and
  // non-modally; must not reenter save/close of its own buffer. Notifications that
  // affect another source buffer are allowed. Captures may be GUI-owned, must
  // guard their dependencies' lifetimes, and are never transferred to the worker.
  // Failure leaves the draft unsaved. Exceptions become VxCoreError failures.
  std::function<VxCoreError()> finalizeSave;
};

} // namespace vnotex

#endif // MEMORYBUFFERSOURCE_H
