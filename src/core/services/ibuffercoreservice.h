#ifndef IBUFFERCORESERVICE_H
#define IBUFFERCORESERVICE_H

#include <QByteArray>
#include <QString>
#include <exception>

namespace vnotex {

// Immutable admission context. Zero-token callers intentionally retain unowned
// installs; a service epoch still prevents a late job publishing into a new binding.
struct BufferSnapshotContext {
  quint64 writerToken = 0;
  quint64 bufferEpoch = 0;
  int expectedBaseRevision = 0;
  quint64 editorRevision = 0;
};

struct BufferSnapshotResult {
  bool installed = false;
  bool saved = false;
  int coreRevision = 0;
  QString error;
  int errorCode = 0; // Optional backend status; zero means no specific error supplied.
};

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

  // One serialized ordinary worker attempt. This default is the legitimate
  // unowned operation for core-only clients; no writer-registration knowledge
  // or observed-revision claim is invented here.
  virtual BufferSnapshotResult saveSnapshot(const QString &p_bufferId, const QByteArray &p_data,
                                            const BufferSnapshotContext &) {
    BufferSnapshotResult result;
    try {
      result.installed = setContentRaw(p_bufferId, p_data);
      if (result.installed)
        result.saved = saveBuffer(p_bufferId);
      if (!result.saved)
        result.error = result.installed ? QStringLiteral("saveBuffer failed")
                                        : QStringLiteral("setContentRaw failed");
    } catch (const std::exception &error) {
      result.error = QString::fromUtf8(error.what());
    } catch (...) {
      result.error = QStringLiteral("Snapshot persistence failed");
    }
    return result;
  }

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
