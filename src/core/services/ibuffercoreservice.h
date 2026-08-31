#ifndef IBUFFERCORESERVICE_H
#define IBUFFERCORESERVICE_H

#include <QByteArray>
#include <QString>

namespace vnotex {

// Minimal abstract interface for the slice of BufferCoreService that
// BufferSaveQueue exercises on its worker thread. Extracted so unit tests can
// inject a fake without spinning up a real vxcore context.
//
// Production: BufferCoreService implements this interface, and BufferService
// (which derives from it) is what BufferSaveQueue is actually constructed with.
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

  // Whether the buffer is read-only.
  //
  // BufferSaveQueue::enqueue() consults this BEFORE acquiring any mutex so
  // a read-only buffer can never reach setContentRaw/saveBuffer on disk.
  //
  // This is a per-buffer FACT, resolved once when the buffer enters
  // BufferService (the per-open FileOpenSettings::m_readOnly override ORed
  // with the owning notebook's read-only flag) — implementers must not
  // re-derive it per call.
  //
  // Default returns false (writable), which keeps pre-existing fake
  // implementations source-compatible. Production behaviour comes from
  // BufferService's override; a fake that needs to simulate a read-only
  // buffer overrides this and returns true.
  virtual bool isBufferReadOnly(const QString &p_bufferId) const {
    (void)p_bufferId;
    return false;
  }
};

} // namespace vnotex

#endif // IBUFFERCORESERVICE_H
