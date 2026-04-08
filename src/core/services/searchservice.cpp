#include "searchservice.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QMetaType>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <core/services/searchcoreservice.h>

using namespace vnotex;

class vnotex::SearchWorker : public QObject {
  Q_OBJECT

public:
  explicit SearchWorker(SearchCoreService *p_coreService, QMutex *p_mutex,
                        QObject *p_parent = nullptr)
      : QObject(p_parent), m_coreService(p_coreService), m_mutex(p_mutex) {}

public slots:
  void doSearchFiles(int p_token, const QString &p_notebookId, const QString &p_queryJson,
                     const QString &p_inputFilesJson, std::atomic<int> *p_cancelFlag) {
    qDebug() << "SearchWorker::doSearchFiles: [worker thread] notebookId:" << p_notebookId;
    emit progress(p_token, 0);

    if (p_cancelFlag->load(std::memory_order_relaxed) != 0) {
      emit cancelled(p_token);
      return;
    }

    QJsonArray matches;
    Error error;
    {
      QMutexLocker locker(m_mutex);
      error = m_coreService->searchFiles(p_notebookId, p_queryJson, p_inputFilesJson, &matches);
    }

    if (error) {
      emit failed(p_token, error);
      return;
    }

    if (p_cancelFlag->load(std::memory_order_relaxed) != 0) {
      emit cancelled(p_token);
      return;
    }

    QJsonObject wrapped;
    wrapped.insert(QStringLiteral("matchCount"), matches.size());
    wrapped.insert(QStringLiteral("truncated"), false);
    wrapped.insert(QStringLiteral("matches"), QJsonValue(matches));

    SearchResult result = SearchResult::fromFileSearchJson(wrapped, p_notebookId);
    qDebug() << "SearchWorker::doSearchFiles: [worker thread] completed, matches:"
             << matches.size();
    emit progress(p_token, 100);
    emit finished(p_token, result);
  }

  void doSearchContent(int p_token, const QString &p_notebookId, const QString &p_queryJson,
                       const QString &p_inputFilesJson, std::atomic<int> *p_cancelFlag) {
    qDebug() << "SearchWorker::doSearchContent: [worker thread] notebookId:" << p_notebookId;
    emit progress(p_token, 0);

    QJsonObject resultObj;
    Error error;
    {
      QMutexLocker locker(m_mutex);
      error = m_coreService->searchContentCancellable(p_notebookId, p_queryJson, p_inputFilesJson,
                                                      p_cancelFlag, &resultObj);
    }

    if (error) {
      if (error.code() == ErrorCode::Cancelled) {
        emit cancelled(p_token);
        return;
      }

      emit failed(p_token, error);
      return;
    }

    if (p_cancelFlag->load(std::memory_order_relaxed) != 0) {
      emit cancelled(p_token);
      return;
    }

    SearchResult result = SearchResult::fromContentSearchJson(resultObj, p_notebookId);
    qDebug() << "SearchWorker::doSearchContent: [worker thread] completed, matchCount:"
             << result.m_matchCount << "truncated:" << result.m_truncated;
    emit progress(p_token, 100);
    emit finished(p_token, result);
  }

  void doSearchByTags(int p_token, const QString &p_notebookId, const QString &p_queryJson,
                      const QString &p_inputFilesJson, std::atomic<int> *p_cancelFlag) {
    qDebug() << "SearchWorker::doSearchByTags: [worker thread] notebookId:" << p_notebookId;
    emit progress(p_token, 0);

    if (p_cancelFlag->load(std::memory_order_relaxed) != 0) {
      emit cancelled(p_token);
      return;
    }

    QJsonArray matches;
    Error error;
    {
      QMutexLocker locker(m_mutex);
      error = m_coreService->searchByTags(p_notebookId, p_queryJson, p_inputFilesJson, &matches);
    }

    if (error) {
      emit failed(p_token, error);
      return;
    }

    if (p_cancelFlag->load(std::memory_order_relaxed) != 0) {
      emit cancelled(p_token);
      return;
    }

    QJsonObject wrapped;
    wrapped.insert(QStringLiteral("matchCount"), matches.size());
    wrapped.insert(QStringLiteral("truncated"), false);
    wrapped.insert(QStringLiteral("matches"), QJsonValue(matches));

    SearchResult result = SearchResult::fromFileSearchJson(wrapped, p_notebookId);
    qDebug() << "SearchWorker::doSearchByTags: [worker thread] completed, matches:"
             << matches.size();
    emit progress(p_token, 100);
    emit finished(p_token, result);
  }

signals:
  void finished(int p_token, const SearchResult &p_result);
  void failed(int p_token, const Error &p_error);
  void cancelled(int p_token);
  void progress(int p_token, int p_percent);

private:
  SearchCoreService *m_coreService = nullptr;
  QMutex *m_mutex = nullptr;
};

SearchService::SearchService(SearchCoreService *p_coreService, QObject *p_parent)
    : QObject(p_parent), m_coreService(p_coreService), m_thread(new QThread(this)),
      m_mutex(new QMutex) {
  Q_ASSERT(m_coreService);

  qRegisterMetaType<SearchResult>();
  qRegisterMetaType<Error>();
  qRegisterMetaType<std::atomic<int> *>("std::atomic<int>*");

  m_worker = new SearchWorker(m_coreService, m_mutex);
  m_worker->moveToThread(m_thread);

  connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

  connect(m_worker, &SearchWorker::progress, this, &SearchService::searchProgress);

  const auto cleanupToken = [this](int p_token) {
    m_activeTokens.remove(p_token);
    m_cancelFlags.remove(p_token);
  };

  connect(m_worker, &SearchWorker::finished, this,
          [this, cleanupToken](int p_token, const SearchResult &p_result) {
            cleanupToken(p_token);
            emit searchFinished(p_token, p_result);
          });

  connect(m_worker, &SearchWorker::failed, this,
          [this, cleanupToken](int p_token, const Error &p_error) {
            cleanupToken(p_token);
            emit searchFailed(p_token, p_error);
          });

  connect(m_worker, &SearchWorker::cancelled, this, [this, cleanupToken](int p_token) {
    cleanupToken(p_token);
    emit searchCancelled(p_token);
  });

  m_thread->start();
}

SearchService::~SearchService() {
  cancel();
  m_thread->quit();
  m_thread->wait();

  delete m_mutex;
  m_mutex = nullptr;
}

int SearchService::searchFiles(const QString &p_notebookId, const QString &p_queryJson,
                               const QString &p_inputFilesJson) {
  qDebug() << "SearchService::searchFiles: notebookId:" << p_notebookId;

  const int token = m_nextToken++;
  auto cancelFlag = std::make_shared<std::atomic<int>>(0);
  m_cancelFlags.insert(token, cancelFlag);
  m_activeTokens.insert(token);
  emit searchStarted(token);

  const bool invoked = QMetaObject::invokeMethod(
      m_worker, "doSearchFiles", Qt::QueuedConnection, Q_ARG(int, token),
      Q_ARG(QString, p_notebookId), Q_ARG(QString, p_queryJson), Q_ARG(QString, p_inputFilesJson),
      Q_ARG(std::atomic<int> *, cancelFlag.get()));
  if (!invoked) {
    m_activeTokens.remove(token);
    m_cancelFlags.remove(token);
    emit searchFailed(token, Error::error(ErrorCode::InvalidArgument,
                                          QStringLiteral("Failed to invoke doSearchFiles")));
  }
  return token;
}

int SearchService::searchContent(const QString &p_notebookId, const QString &p_queryJson,
                                 const QString &p_inputFilesJson) {
  qDebug() << "SearchService::searchContent: notebookId:" << p_notebookId;

  const int token = m_nextToken++;
  auto cancelFlag = std::make_shared<std::atomic<int>>(0);
  m_cancelFlags.insert(token, cancelFlag);
  m_activeTokens.insert(token);
  emit searchStarted(token);

  const bool invoked = QMetaObject::invokeMethod(
      m_worker, "doSearchContent", Qt::QueuedConnection, Q_ARG(int, token),
      Q_ARG(QString, p_notebookId), Q_ARG(QString, p_queryJson), Q_ARG(QString, p_inputFilesJson),
      Q_ARG(std::atomic<int> *, cancelFlag.get()));
  if (!invoked) {
    m_activeTokens.remove(token);
    m_cancelFlags.remove(token);
    emit searchFailed(token, Error::error(ErrorCode::InvalidArgument,
                                          QStringLiteral("Failed to invoke doSearchContent")));
  }
  return token;
}

int SearchService::searchByTags(const QString &p_notebookId, const QString &p_queryJson,
                                const QString &p_inputFilesJson) {
  qDebug() << "SearchService::searchByTags: notebookId:" << p_notebookId;

  const int token = m_nextToken++;
  auto cancelFlag = std::make_shared<std::atomic<int>>(0);
  m_cancelFlags.insert(token, cancelFlag);
  m_activeTokens.insert(token);
  emit searchStarted(token);

  const bool invoked = QMetaObject::invokeMethod(
      m_worker, "doSearchByTags", Qt::QueuedConnection, Q_ARG(int, token),
      Q_ARG(QString, p_notebookId), Q_ARG(QString, p_queryJson), Q_ARG(QString, p_inputFilesJson),
      Q_ARG(std::atomic<int> *, cancelFlag.get()));
  if (!invoked) {
    m_activeTokens.remove(token);
    m_cancelFlags.remove(token);
    emit searchFailed(token, Error::error(ErrorCode::InvalidArgument,
                                          QStringLiteral("Failed to invoke doSearchByTags")));
  }
  return token;
}

void SearchService::cancel(int p_token) {
  auto it = m_cancelFlags.constFind(p_token);
  if (it != m_cancelFlags.constEnd()) {
    (*it)->store(1, std::memory_order_relaxed);
  }
}

void SearchService::cancel() {
  qDebug() << "SearchService::cancel: cancelling active searches";
  for (auto it = m_cancelFlags.cbegin(); it != m_cancelFlags.cend(); ++it) {
    it.value()->store(1, std::memory_order_relaxed);
  }
}

bool SearchService::isSearching() const { return !m_activeTokens.isEmpty(); }

bool SearchService::isSearching(int p_token) const { return m_activeTokens.contains(p_token); }

#include "searchservice.moc"
