#include "searchservice.h"

#include <QDebug>
#include <QEventLoop>
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
                        std::atomic<int> *p_cancelFlag, QObject *p_parent = nullptr)
      : QObject(p_parent), m_coreService(p_coreService), m_mutex(p_mutex), m_cancelFlag(p_cancelFlag) {
  }

public slots:
  void doSearchFiles(const QString &p_notebookId, const QString &p_queryJson,
                     const QString &p_inputFilesJson) {
    qDebug() << "SearchWorker::doSearchFiles: [worker thread] notebookId:" << p_notebookId;
    emit progress(0);

    if (m_cancelFlag->load(std::memory_order_relaxed) != 0) {
      emit cancelled();
      return;
    }

    QJsonArray matches;
    Error error;
    {
      QMutexLocker locker(m_mutex);
      error = m_coreService->searchFiles(p_notebookId, p_queryJson, p_inputFilesJson, &matches);
    }

    if (error) {
      emit failed(error);
      return;
    }

    if (m_cancelFlag->load(std::memory_order_relaxed) != 0) {
      emit cancelled();
      return;
    }

    QJsonObject wrapped;
    wrapped.insert(QStringLiteral("matchCount"), matches.size());
    wrapped.insert(QStringLiteral("truncated"), false);
    wrapped.insert(QStringLiteral("matches"), QJsonValue(matches));

    SearchResult result = SearchResult::fromFileSearchJson(wrapped, p_notebookId);
    qDebug() << "SearchWorker::doSearchFiles: [worker thread] completed, matches:" << matches.size();
    emit progress(100);
    emit finished(result);
  }

  void doSearchContent(const QString &p_notebookId, const QString &p_queryJson,
                       const QString &p_inputFilesJson) {
    qDebug() << "SearchWorker::doSearchContent: [worker thread] notebookId:" << p_notebookId;
    emit progress(0);

    QJsonObject resultObj;
    Error error;
    {
      QMutexLocker locker(m_mutex);
      error = m_coreService->searchContentCancellable(p_notebookId, p_queryJson, p_inputFilesJson,
                                                      m_cancelFlag, &resultObj);
    }

    if (error) {
      if (error.code() == ErrorCode::Cancelled) {
        emit cancelled();
        return;
      }

      emit failed(error);
      return;
    }

    if (m_cancelFlag->load(std::memory_order_relaxed) != 0) {
      emit cancelled();
      return;
    }

    SearchResult result = SearchResult::fromContentSearchJson(resultObj, p_notebookId);
    qDebug() << "SearchWorker::doSearchContent: [worker thread] completed, matchCount:"
             << result.m_matchCount << "truncated:" << result.m_truncated;
    emit progress(100);
    emit finished(result);
  }

  void doSearchByTags(const QString &p_notebookId, const QString &p_queryJson,
                      const QString &p_inputFilesJson) {
    qDebug() << "SearchWorker::doSearchByTags: [worker thread] notebookId:" << p_notebookId;
    emit progress(0);

    if (m_cancelFlag->load(std::memory_order_relaxed) != 0) {
      emit cancelled();
      return;
    }

    QJsonArray matches;
    Error error;
    {
      QMutexLocker locker(m_mutex);
      error = m_coreService->searchByTags(p_notebookId, p_queryJson, p_inputFilesJson, &matches);
    }

    if (error) {
      emit failed(error);
      return;
    }

    if (m_cancelFlag->load(std::memory_order_relaxed) != 0) {
      emit cancelled();
      return;
    }

    QJsonObject wrapped;
    wrapped.insert(QStringLiteral("matchCount"), matches.size());
    wrapped.insert(QStringLiteral("truncated"), false);
    wrapped.insert(QStringLiteral("matches"), QJsonValue(matches));

    SearchResult result = SearchResult::fromFileSearchJson(wrapped, p_notebookId);
    qDebug() << "SearchWorker::doSearchByTags: [worker thread] completed, matches:" << matches.size();
    emit progress(100);
    emit finished(result);
  }

signals:
  void finished(const SearchResult &p_result);
  void failed(const Error &p_error);
  void cancelled();
  void progress(int p_percent);

private:
  SearchCoreService *m_coreService = nullptr;
  QMutex *m_mutex = nullptr;
  std::atomic<int> *m_cancelFlag = nullptr;
};

SearchService::SearchService(SearchCoreService *p_coreService, QObject *p_parent)
    : QObject(p_parent), m_coreService(p_coreService), m_thread(new QThread(this)), m_mutex(new QMutex) {
  Q_ASSERT(m_coreService);

  qRegisterMetaType<SearchResult>();
  qRegisterMetaType<Error>();

  m_worker = new SearchWorker(m_coreService, m_mutex, &m_cancelFlag);
  m_worker->moveToThread(m_thread);

  connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

  connect(m_worker, &SearchWorker::progress, this, &SearchService::searchProgress);

  connect(m_worker, &SearchWorker::finished, this, [this](const SearchResult &p_result) {
    m_searching.store(false, std::memory_order_relaxed);
    emit searchFinished(p_result);
  });

  connect(m_worker, &SearchWorker::failed, this, [this](const Error &p_error) {
    m_searching.store(false, std::memory_order_relaxed);
    emit searchFailed(p_error);
  });

  connect(m_worker, &SearchWorker::cancelled, this, [this]() {
    m_searching.store(false, std::memory_order_relaxed);
    emit searchCancelled();
  });

  m_thread->start();
}

SearchService::~SearchService() {
  if (m_searching.load(std::memory_order_relaxed)) {
    cancel();
    waitForCurrentSearch();
  }

  m_thread->quit();
  m_thread->wait();

  delete m_mutex;
  m_mutex = nullptr;
}

void SearchService::searchFiles(const QString &p_notebookId, const QString &p_queryJson,
                                const QString &p_inputFilesJson) {
  qDebug() << "SearchService::searchFiles: notebookId:" << p_notebookId;

  if (m_searching.load(std::memory_order_relaxed)) {
    qDebug() << "SearchService::searchFiles: cancelling previous search";
    cancel();
    waitForCurrentSearch();
  }

  m_cancelFlag.store(0, std::memory_order_relaxed);
  m_searching.store(true, std::memory_order_relaxed);
  emit searchStarted();

  const bool invoked = QMetaObject::invokeMethod(
      m_worker, "doSearchFiles", Qt::QueuedConnection, Q_ARG(QString, p_notebookId),
      Q_ARG(QString, p_queryJson), Q_ARG(QString, p_inputFilesJson));
  if (!invoked) {
    m_searching.store(false, std::memory_order_relaxed);
    emit searchFailed(Error::error(ErrorCode::InvalidArgument,
                                   QStringLiteral("Failed to invoke doSearchFiles")));
  }
}

void SearchService::searchContent(const QString &p_notebookId, const QString &p_queryJson,
                                  const QString &p_inputFilesJson) {
  qDebug() << "SearchService::searchContent: notebookId:" << p_notebookId;

  if (m_searching.load(std::memory_order_relaxed)) {
    qDebug() << "SearchService::searchContent: cancelling previous search";
    cancel();
    waitForCurrentSearch();
  }

  m_cancelFlag.store(0, std::memory_order_relaxed);
  m_searching.store(true, std::memory_order_relaxed);
  emit searchStarted();

  const bool invoked = QMetaObject::invokeMethod(
      m_worker, "doSearchContent", Qt::QueuedConnection, Q_ARG(QString, p_notebookId),
      Q_ARG(QString, p_queryJson), Q_ARG(QString, p_inputFilesJson));
  if (!invoked) {
    m_searching.store(false, std::memory_order_relaxed);
    emit searchFailed(Error::error(ErrorCode::InvalidArgument,
                                   QStringLiteral("Failed to invoke doSearchContent")));
  }
}

void SearchService::searchByTags(const QString &p_notebookId, const QString &p_queryJson,
                                 const QString &p_inputFilesJson) {
  qDebug() << "SearchService::searchByTags: notebookId:" << p_notebookId;

  if (m_searching.load(std::memory_order_relaxed)) {
    qDebug() << "SearchService::searchByTags: cancelling previous search";
    cancel();
    waitForCurrentSearch();
  }

  m_cancelFlag.store(0, std::memory_order_relaxed);
  m_searching.store(true, std::memory_order_relaxed);
  emit searchStarted();

  const bool invoked = QMetaObject::invokeMethod(
      m_worker, "doSearchByTags", Qt::QueuedConnection, Q_ARG(QString, p_notebookId),
      Q_ARG(QString, p_queryJson), Q_ARG(QString, p_inputFilesJson));
  if (!invoked) {
    m_searching.store(false, std::memory_order_relaxed);
    emit searchFailed(Error::error(ErrorCode::InvalidArgument,
                                   QStringLiteral("Failed to invoke doSearchByTags")));
  }
}

void SearchService::cancel() {
  qDebug() << "SearchService::cancel: setting cancel flag";
  m_cancelFlag.store(1, std::memory_order_relaxed);
}

bool SearchService::isSearching() const {
  return m_searching.load(std::memory_order_relaxed);
}

void SearchService::waitForCurrentSearch() {
  if (!m_searching.load(std::memory_order_relaxed)) {
    return;
  }

  qDebug() << "SearchService::waitForCurrentSearch: waiting for worker thread to finish";

  QEventLoop loop;
  connect(this, &SearchService::searchFinished, &loop, &QEventLoop::quit);
  connect(this, &SearchService::searchFailed, &loop, &QEventLoop::quit);
  connect(this, &SearchService::searchCancelled, &loop, &QEventLoop::quit);
  loop.exec();
}

#include "searchservice.moc"
