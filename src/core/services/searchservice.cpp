#include "searchservice.h"

#include <algorithm>
#include <limits>
#include <mutex>

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QMetaObject>
#include <QMetaType>
#include <QMutex>
#include <QMutexLocker>
#include <QStringView>
#include <QThread>
#include <QVector>

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

    SearchResult result;
    Error error;
    {
      QMutexLocker locker(m_mutex);
      error = m_coreService->searchFiles(p_notebookId, p_queryJson, p_inputFilesJson, &result);
    }

    if (error) {
      emit failed(p_token, error);
      return;
    }

    if (p_cancelFlag->load(std::memory_order_relaxed) != 0) {
      emit cancelled(p_token);
      return;
    }

    // Preserve the existing async UI policy; the core service retains the native fact.
    result.m_truncated = false;
    qDebug() << "SearchWorker::doSearchFiles: [worker thread] completed, matches:"
             << result.m_matchCount;
    emit progress(p_token, 100);
    emit finished(p_token, result);
  }

  void doSearchContent(int p_token, const QString &p_notebookId, const QString &p_queryJson,
                       const QString &p_inputFilesJson, std::atomic<int> *p_cancelFlag) {
    qDebug() << "SearchWorker::doSearchContent: [worker thread streaming] notebookId:"
             << p_notebookId;
    emit progress(p_token, 0);

    if (p_cancelFlag->load(std::memory_order_relaxed) != 0) {
      emit cancelled(p_token);
      return;
    }

    // The streaming primitive owns NO truncation (it emits every occurrence). The consumer
    // (this worker) applies the SAME deterministic raw-occurrence maxResults cap as the
    // blob API, before grouping same-line occurrences. Query parsing remains JSON.
    const int maxResults = parseMaxResults(p_queryJson);

    // Batches arrive concurrently and out of order. Retain owning UNGROUPED values by
    // batch index. Progress counts completed callbacks, not the greatest index seen.
    std::mutex accMutex;
    QMap<int, SearchContentResult> batchesByIndex;
    std::atomic<int> completed{0};
    int encryptedSkippedCount = 0;

    const auto onBatch = [&](int p_batchIndex, int p_totalBatches,
                             const SearchContentResult &p_batch) {
      {
        std::lock_guard<std::mutex> guard(accMutex);
        batchesByIndex.insert(p_batchIndex, p_batch);
        encryptedSkippedCount += p_batch.m_encryptedSkippedCount;
      }

      const int done = completed.fetch_add(1, std::memory_order_relaxed) + 1;

      // Deliver this chunk incrementally as a preview. Emitting from a drain thread is safe:
      // the connection to SearchService (GUI thread) is queued, so this only posts an event.
      // Skip zero-match chunks (consumer would ignore them) but still count progress. Preview
      // rows are superseded at completion by the authoritative, capped setSearchResult().
      if (!p_batch.m_files.isEmpty()) {
        SearchResult batchResult = SearchResult::fromContentSearchResult(p_batch, p_notebookId);
        if (batchResult.m_matchCount > 0) {
          emit batch(p_token, batchResult);
        }
      }

      if (p_totalBatches > 0) {
        emit progress(p_token, std::min(100, done * 100 / p_totalBatches));
      }
    };

    Error error;
    {
      QMutexLocker locker(m_mutex);
      error = m_coreService->searchContentStreaming(p_notebookId, p_queryJson, p_inputFilesJson, 0,
                                                    p_cancelFlag, onBatch);
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

    // Reassemble by input-file (batch-index) order, then cap raw occurrences. Preview
    // grouping never mutates the retained batches or the authoritative final payload.
    SearchContentResult content;
    qsizetype fileCount = 0;
    for (auto it = batchesByIndex.cbegin(); it != batchesByIndex.cend(); ++it) {
      fileCount += it.value().m_files.size();
    }
    content.m_files.reserve(fileCount);
    for (auto it = batchesByIndex.begin(); it != batchesByIndex.end(); ++it) {
      for (auto &file : it.value().m_files) {
        content.m_files.append(std::move(file));
      }
    }
    content.m_encryptedSkippedCount = encryptedSkippedCount;
    content.m_truncated = applyMaxResultsTruncation(content.m_files, maxResults);
    SearchResult result = SearchResult::fromContentSearchResult(content, p_notebookId);
    // Only a successful, uncancelled content search grants replacement provenance.
    for (SearchFileResult &file : result.m_fileResults) {
      file.m_replacementSupported = true;
    }

    qDebug() << "SearchWorker::doSearchContent: [worker thread streaming] completed, matchCount:"
             << result.m_matchCount << "truncated:" << result.m_truncated
             << "batches:" << batchesByIndex.size();
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

    SearchResult result;
    Error error;
    {
      QMutexLocker locker(m_mutex);
      error = m_coreService->searchByTags(p_notebookId, p_queryJson, p_inputFilesJson, &result);
    }

    if (error) {
      emit failed(p_token, error);
      return;
    }

    if (p_cancelFlag->load(std::memory_order_relaxed) != 0) {
      emit cancelled(p_token);
      return;
    }

    // Preserve the existing async UI policy; the core service retains the native fact.
    result.m_truncated = false;
    qDebug() << "SearchWorker::doSearchByTags: [worker thread] completed, matches:"
             << result.m_matchCount;
    emit progress(p_token, 100);
    emit finished(p_token, result);
  }

signals:
  void finished(int p_token, const SearchResult &p_result);
  void failed(int p_token, const Error &p_error);
  void cancelled(int p_token);
  void progress(int p_token, int p_percent);
  void batch(int p_token, const SearchResult &p_result);

private:
  // Parse the "maxResults" cap out of a content-search query the same way vxcore does
  // (SearchContentQuery::FromJson). CRITICAL: vxcore's SearchContentQuery::max_results DEFAULTS
  // to 100 and is only overridden when the "maxResults" key is present; an absent key therefore
  // means "cap at 100", NOT "uncapped". An explicit non-positive value means "no cap". Mirroring
  // this exactly keeps the streaming finished() payload byte-identical to the blob baseline even
  // when the caller omits the field.
  static constexpr int kDefaultContentSearchMaxResults = 100;
  static int parseMaxResults(const QString &p_queryJson) {
    const QJsonDocument doc = QJsonDocument::fromJson(p_queryJson.toUtf8());
    if (!doc.isObject()) {
      return kDefaultContentSearchMaxResults;
    }
    // QJsonValue::toInt(default) returns the default for an absent (Undefined) key and for a
    // non-numeric value, but the real integer (including 0 / negatives) when present.
    return doc.object().value(QStringLiteral("maxResults")).toInt(kDefaultContentSearchMaxResults);
  }

  // Match SimpleSearchBackend's cap, including truncated=true at the exact limit.
  // A non-positive cap is unbounded. Never cap grouped line rows.
  static bool applyMaxResultsTruncation(QVector<SearchContentFile> &p_files, int p_maxResults) {
    if (p_maxResults <= 0) {
      return false;
    }
    int total = 0;
    for (qsizetype i = 0; i < p_files.size(); ++i) {
      auto &occurrences = p_files[i].m_matches;
      const int remaining = p_maxResults - total;
      if (occurrences.size() >= remaining) {
        occurrences.resize(remaining);
        p_files.resize(i + 1);
        return true;
      }
      total += static_cast<int>(occurrences.size());
    }
    return false;
  }

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

  // Parent the worker to this service. If no search runs this session the
  // worker QThread never starts, so the finished->deleteLater below never
  // fires; QObject child deletion then reclaims the worker. On the first
  // search, ensureWorkerThreadStarted() unparents it and moves it onto
  // m_thread (deferred out of this constructor because it runs before
  // QApplication exists), after which finished->deleteLater owns it.
  m_worker = new SearchWorker(m_coreService, m_mutex, this);

  connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

  connect(m_worker, &SearchWorker::progress, this, &SearchService::searchProgress);
  connect(m_worker, &SearchWorker::batch, this, &SearchService::searchBatch);

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

  // NOTE: m_thread is started lazily on the first search (see
  // ensureWorkerThreadStarted()), NOT here. This constructor runs in main()
  // BEFORE the QApplication is created, so starting the QThread now would run
  // QThread::exec() -> QEventLoop while QCoreApplication::instance() is still
  // null, emitting "QEventLoop: Cannot be used without QCoreApplication". The
  // std::thread drain pool below runs no event loop, so it stays here.
  unsigned n = std::thread::hardware_concurrency();
  if (n == 0) {
    n = 2;
  }
  n = std::min(n, 8u);

  VxCoreContextHandle ctx = m_coreService->context();
  for (unsigned i = 0; i < n; ++i) {
    m_drainThreads.emplace_back([this, ctx]() {
      while (!m_stopDrain.load(std::memory_order_relaxed)) {
        if (vxcore_work_queue_process_next(ctx, "vxcore.search", 100) == 1) {
          m_drainItemsProcessed.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
}

SearchService::~SearchService() {
  cancel();
  m_stopDrain.store(true, std::memory_order_relaxed);
  m_thread->quit();
  m_thread->wait();

  for (auto &t : m_drainThreads) {
    if (t.joinable()) {
      t.join();
    }
  }

  delete m_mutex;
  m_mutex = nullptr;
}

void SearchService::ensureWorkerThreadStarted() {
  // Idempotent and GUI-thread-only, so no lock is needed. Unparent before
  // moveToThread (Qt forbids moving a parented object across threads); after
  // this the finished->deleteLater connection owns the worker.
  if (m_thread->isRunning()) {
    return;
  }
  m_worker->setParent(nullptr);
  m_worker->moveToThread(m_thread);
  m_thread->start();
}

int SearchService::searchFiles(const QString &p_notebookId, const QString &p_queryJson,
                               const QString &p_inputFilesJson) {
  qDebug() << "SearchService::searchFiles: notebookId:" << p_notebookId;

  const int token = m_nextToken++;
  auto cancelFlag = std::make_shared<std::atomic<int>>(0);
  m_cancelFlags.insert(token, cancelFlag);
  m_activeTokens.insert(token);
  emit searchStarted(token);

  ensureWorkerThreadStarted();
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

  ensureWorkerThreadStarted();
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

  ensureWorkerThreadStarted();
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

bool SearchService::buildReplacement(const QString &p_source,
                                     const QVector<SearchLineMatch> &p_matches,
                                     const QString &p_replacement, QString *p_outText,
                                     int *p_outCount, QString *p_outError) {
  // Keep implicit-shared snapshots before initializing outputs, including in-place callers.
  const QString source = p_source;
  const QString replacement = p_replacement;
  if (p_outText) {
    p_outText->clear();
  }
  if (p_outCount) {
    *p_outCount = 0;
  }
  if (p_outError) {
    p_outError->clear();
  }
  const auto invalidRange = [p_outError]() {
    if (p_outError) {
      *p_outError = tr("The search result contains an invalid match range.");
    }
    return false;
  };
  const auto staleLine = [p_outError]() {
    *p_outError = tr("Search results are out of date. Save affected notes and search again.");
    return false;
  };
  if (!p_outText || !p_outCount || !p_outError || p_outText == p_outError) {
    return invalidRange();
  }
  if (p_matches.isEmpty()) {
    *p_outText = source;
    return true;
  }

  using StringSize = decltype(source.size());
  struct Range {
    StringSize m_start;
    StringSize m_end;
    bool m_changed = false;
  };
  QVector<const SearchLineMatch *> lines;
  lines.reserve(p_matches.size());
  qint64 rangeCount = 0;
  for (const SearchLineMatch &line : p_matches) {
    if (line.m_lineNumber <= 0 ||
        line.m_segments.size() > (std::numeric_limits<int>::max)() - rangeCount) {
      return invalidRange();
    }
    rangeCount += line.m_segments.size();
    lines.append(&line);
  }
  std::sort(lines.begin(), lines.end(),
            [](const SearchLineMatch *p_left, const SearchLineMatch *p_right) {
              return p_left->m_lineNumber < p_right->m_lineNumber;
            });

  QVector<Range> ranges;
  ranges.reserve(static_cast<int>(rangeCount));
  const QStringView sourceView(source);
  StringSize lineStart = 0;
  StringSize lineEnd = 0;
  int lineNumber = 1;
  const SearchLineMatch *previousLine = nullptr;
  for (const SearchLineMatch *line : lines) {
    if (previousLine && previousLine->m_lineNumber == line->m_lineNumber) {
      if (previousLine->m_lineText != line->m_lineText) {
        return staleLine();
      }
    } else {
      while (lineNumber < line->m_lineNumber) {
        const StringSize newline = source.indexOf(QLatin1Char('\n'), lineStart);
        if (newline < 0) {
          return staleLine();
        }
        lineStart = newline + 1;
        ++lineNumber;
      }
      lineEnd = source.indexOf(QLatin1Char('\n'), lineStart);
      if (lineEnd < 0) {
        lineEnd = source.size();
      }
      if (lineEnd > lineStart && source.at(lineEnd - 1) == QLatin1Char('\r')) {
        --lineEnd;
      }
      if (sourceView.mid(lineStart, lineEnd - lineStart) != QStringView(line->m_lineText)) {
        return staleLine();
      }
    }
    previousLine = line;

    const auto splitsSurrogate = [&](StringSize p_column) {
      const StringSize offset = lineStart + p_column;
      return offset > lineStart && offset < lineEnd && source.at(offset - 1).isHighSurrogate() &&
             source.at(offset).isLowSurrogate();
    };
    for (const SearchMatchSegment &segment : line->m_segments) {
      if (segment.m_columnStart < 0 || segment.m_columnEnd < segment.m_columnStart ||
          segment.m_columnEnd > lineEnd - lineStart || splitsSurrogate(segment.m_columnStart) ||
          splitsSurrogate(segment.m_columnEnd)) {
        return invalidRange();
      }
      ranges.append({lineStart + segment.m_columnStart, lineStart + segment.m_columnEnd, false});
    }
  }

  std::sort(ranges.begin(), ranges.end(), [](const Range &p_left, const Range &p_right) {
    return p_left.m_start < p_right.m_start ||
           (p_left.m_start == p_right.m_start && p_left.m_end < p_right.m_end);
  });
  ranges.erase(std::unique(ranges.begin(), ranges.end(),
                           [](const Range &p_left, const Range &p_right) {
                             return p_left.m_start == p_right.m_start &&
                                    p_left.m_end == p_right.m_end;
                           }),
               ranges.end());

  // Check the sum in forward output order, bounded by both QString's signed size type and
  // its UTF-16 byte storage. No subtraction or multiplication can wrap before allocation.
  const qint64 maxOutputSize =
      qMin<qint64>((std::numeric_limits<StringSize>::max)() - 1,
                   (std::numeric_limits<qptrdiff>::max)() / sizeof(QChar) - 1);
  qint64 outputSize = 0;
  const auto addOutputSize = [&](qint64 p_size) {
    if (p_size > maxOutputSize - outputSize) {
      return false;
    }
    outputSize += p_size;
    return true;
  };
  const QStringView replacementView(replacement);
  StringSize cursor = 0;
  int changedCount = 0;
  for (Range &range : ranges) {
    // Identical ranges are already deduplicated. An insertion at a range boundary is valid,
    // but an insertion strictly inside another range overlaps and must also be rejected.
    if (range.m_start < cursor) {
      return invalidRange();
    }
    if (!addOutputSize(range.m_start - cursor) || !addOutputSize(replacement.size())) {
      return invalidRange();
    }
    range.m_changed = sourceView.mid(range.m_start, range.m_end - range.m_start) != replacementView;
    if (range.m_changed) {
      ++changedCount;
    }
    cursor = range.m_end;
  }
  if (!addOutputSize(source.size() - cursor)) {
    return invalidRange();
  }
  if (changedCount == 0) {
    *p_outText = source;
    return true;
  }

  QString output;
  output.reserve(static_cast<StringSize>(outputSize));
  cursor = 0;
  for (const Range &range : ranges) {
    if (!range.m_changed) {
      continue;
    }
    output.append(source.constData() + cursor, range.m_start - cursor);
    output.append(replacement);
    cursor = range.m_end;
  }
  output.append(source.constData() + cursor, source.size() - cursor);
  *p_outText = std::move(output);
  *p_outCount = changedCount;
  return true;
}

#include "searchservice.moc"
