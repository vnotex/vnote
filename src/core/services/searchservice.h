#ifndef SEARCHSERVICE_H
#define SEARCHSERVICE_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>

#include <core/error.h>
#include <core/searchresulttypes.h>

#include <vxcore/vxcore.h>

class QMutex;
class QThread;

namespace vnotex {

class SearchCoreService;
class SearchWorker;

// Async wrapper around SearchCoreService using QThread + worker object.
// Supports multiple queued searches with per-request cooperative cancellation.
class SearchService : public QObject {
  Q_OBJECT

public:
  explicit SearchService(SearchCoreService *p_coreService, QObject *p_parent = nullptr);
  ~SearchService() override;

  int searchFiles(const QString &p_notebookId, const QString &p_queryJson,
                  const QString &p_inputFilesJson = QString());
  int searchContent(const QString &p_notebookId, const QString &p_queryJson,
                    const QString &p_inputFilesJson = QString());
  int searchByTags(const QString &p_notebookId, const QString &p_queryJson,
                   const QString &p_inputFilesJson = QString());

  void cancel(int p_token);
  void cancel();
  bool isSearching() const;
  bool isSearching(int p_token) const;

  size_t testDrainThreadCount() const { return m_drainThreads.size(); }
  uint64_t testDrainItemsProcessed() const {
    return m_drainItemsProcessed.load(std::memory_order_relaxed);
  }

signals:
  void searchFinished(int p_token, const SearchResult &p_result);
  void searchFailed(int p_token, const Error &p_error);
  void searchCancelled(int p_token);
  void searchProgress(int p_token, int p_percent);
  void searchStarted(int p_token);

  // Emitted once per completed file-chunk during streaming content search, carrying that
  // chunk's slice of results for incremental rendering. Delivered on the GUI thread (queued).
  // Zero-match chunks are NOT emitted. The authoritative complete result still arrives via
  // searchFinished(); consumers may render batches live and let searchFinished() finalize.
  void searchBatch(int p_token, const SearchResult &p_result);

private:
  // Lazily start the worker QThread on first search. Deferred out of the
  // constructor because SearchService is constructed in main() BEFORE the
  // QApplication exists; starting the QThread there would run QThread::exec()
  // -> QEventLoop with no QCoreApplication, emitting
  // "QEventLoop: Cannot be used without QCoreApplication". Called on the GUI
  // thread from the search entry points only, so it needs no locking.
  void ensureWorkerThreadStarted();

  SearchCoreService *m_coreService = nullptr;
  SearchWorker *m_worker = nullptr;
  QThread *m_thread = nullptr;
  QMutex *m_mutex = nullptr;
  QMap<int, std::shared_ptr<std::atomic<int>>> m_cancelFlags;
  QSet<int> m_activeTokens;
  int m_nextToken = 1;

  std::vector<std::thread> m_drainThreads;
  std::atomic<bool> m_stopDrain{false};
  std::atomic<uint64_t> m_drainItemsProcessed{0};
};

} // namespace vnotex

#endif // SEARCHSERVICE_H
