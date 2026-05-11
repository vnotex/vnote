#ifndef IMAGEHOSTWORKER_H
#define IMAGEHOSTWORKER_H

#include <atomic>
#include <functional>

#include <QObject>
#include <QQueue>

#include <imagehost/imagehosttypes.h>

namespace vnotex {

class IImageHostProvider;

// Background worker for image host operations.
// Designed to live on a dedicated QThread (moved via moveToThread).
// Uses a re-entrancy guard (m_busy + m_queue) to prevent nested execution
// when provider sync methods call processEvents() internally.
class ImageHostWorker : public QObject
{
  Q_OBJECT

public:
  using ProviderFactory = std::function<IImageHostProvider *(const QString &)>;

  explicit ImageHostWorker(ProviderFactory p_factory = nullptr,
                           QObject *p_parent = nullptr);

  ~ImageHostWorker() override = default;

  // Set by the owning service before thread quit to break busy-wait loops.
  std::atomic<int> m_abortFlag{0};

public slots:
  void doUpload(ImageHostWorkItem p_item);
  void doRemove(ImageHostWorkItem p_item);
  void doTestConfig(ImageHostWorkItem p_item);

signals:
  void uploadCompleted(int p_token, const ImageHostAsyncResult &p_result);
  void removeCompleted(int p_token, const ImageHostAsyncResult &p_result);
  void testConfigCompleted(int p_token, bool p_success, const QString &p_msg);
  void operationFailed(int p_token, const QString &p_errorMsg);

private:
  void enqueueOrExecute(const ImageHostWorkItem &p_item);
  void executeItem(const ImageHostWorkItem &p_item);
  void executeUpload(const ImageHostWorkItem &p_item);
  void executeRemove(const ImageHostWorkItem &p_item);
  void executeTestConfig(const ImageHostWorkItem &p_item);
  void drainQueue();

  // Creates a temporary provider for a single operation.
  IImageHostProvider *createProvider(const QString &p_typeId);

  ProviderFactory m_providerFactory;

  bool m_busy = false;
  QQueue<ImageHostWorkItem> m_queue;
};

} // namespace vnotex

#endif // IMAGEHOSTWORKER_H
