#ifndef SEARCHSERVICE_H
#define SEARCHSERVICE_H

#include <atomic>
#include <memory>

#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>

#include <core/error.h>
#include <core/searchresulttypes.h>

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

signals:
  void searchFinished(int p_token, const SearchResult &p_result);
  void searchFailed(int p_token, const Error &p_error);
  void searchCancelled(int p_token);
  void searchProgress(int p_token, int p_percent);
  void searchStarted(int p_token);

private:
  SearchCoreService *m_coreService = nullptr;
  SearchWorker *m_worker = nullptr;
  QThread *m_thread = nullptr;
  QMutex *m_mutex = nullptr;
  QMap<int, std::shared_ptr<std::atomic<int>>> m_cancelFlags;
  QSet<int> m_activeTokens;
  int m_nextToken = 1;
};

} // namespace vnotex

#endif // SEARCHSERVICE_H
