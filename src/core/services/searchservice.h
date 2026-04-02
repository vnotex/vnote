#ifndef SEARCHSERVICE_H
#define SEARCHSERVICE_H

#include <atomic>

#include <QObject>
#include <QString>

#include <core/error.h>
#include <core/searchresulttypes.h>

class QMutex;
class QThread;

namespace vnotex {

class SearchCoreService;
class SearchWorker;

// Async wrapper around SearchCoreService using QThread + worker object.
// Guarantees single-search-at-a-time and cooperative cancellation.
class SearchService : public QObject {
  Q_OBJECT

public:
  explicit SearchService(SearchCoreService *p_coreService, QObject *p_parent = nullptr);
  ~SearchService() override;

  void searchFiles(const QString &p_notebookId, const QString &p_queryJson,
                   const QString &p_inputFilesJson = QString());
  void searchContent(const QString &p_notebookId, const QString &p_queryJson,
                     const QString &p_inputFilesJson = QString());
  void searchByTags(const QString &p_notebookId, const QString &p_queryJson,
                    const QString &p_inputFilesJson = QString());

  void cancel();
  bool isSearching() const;

signals:
  void searchFinished(const SearchResult &p_result);
  void searchFailed(const Error &p_error);
  void searchCancelled();
  void searchProgress(int p_percent);
  void searchStarted();

private:
  void waitForCurrentSearch();

  SearchCoreService *m_coreService = nullptr;
  SearchWorker *m_worker = nullptr;
  QThread *m_thread = nullptr;
  QMutex *m_mutex = nullptr;
  std::atomic<int> m_cancelFlag{0};
  std::atomic<bool> m_searching{false};
};

} // namespace vnotex

#endif // SEARCHSERVICE_H
