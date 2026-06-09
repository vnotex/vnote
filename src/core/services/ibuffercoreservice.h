#ifndef IBUFFERCORESERVICE_H
#define IBUFFERCORESERVICE_H

#include <QByteArray>
#include <QString>

namespace vnotex {

// Minimal abstract interface for the slice of BufferCoreService that
// BufferSaveQueue exercises on its worker thread. Extracted so unit tests can
// inject a fake without spinning up a real vxcore context.
//
// Production: BufferCoreService implements this interface.
// Tests: a header-only fake implements it directly.
class IBufferCoreService {
public:
  virtual ~IBufferCoreService() = default;

  // Replace the in-memory content of the buffer with @p_data.
  // Returns true on success.
  virtual bool setContentRaw(const QString &p_bufferId, const QByteArray &p_data) = 0;

  // Persist the buffer's in-memory content to disk.
  // Returns true on success.
  virtual bool saveBuffer(const QString &p_bufferId) = 0;

  // Check whether the buffer's owning notebook is read-only.
  //
  // BufferSaveQueue::enqueue() consults this BEFORE acquiring any mutex so
  // a read-only notebook can never reach setContentRaw/saveBuffer on disk.
  //
  // Default returns false (writable). The default keeps pre-existing fake
  // implementations source-compatible — they continue to behave as before.
  // Production code (BufferCoreService) overrides to query vxcore; new
  // fakes that need to simulate a read-only notebook override and return true.
  virtual bool isBufferReadOnly(const QString &p_bufferId) const {
    (void)p_bufferId;
    return false;
  }
};

} // namespace vnotex

#endif // IBUFFERCORESERVICE_H
