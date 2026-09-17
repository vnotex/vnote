#include "searchcontroller.h"

#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QModelIndex>
#include <QScopedValueRollback>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <limits>
#include <utility>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/error.h>
#include <core/fileopensettings.h>
#include <core/logging.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/searchservice.h>
#include <models/searchresultmodel.h>

#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

SearchController::SearchController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  if (auto *searchSvc = m_services.get<SearchService>()) {
    connect(searchSvc, &SearchService::searchFinished, this, &SearchController::onSearchFinished,
            Qt::QueuedConnection);
    connect(searchSvc, &SearchService::searchFailed, this, &SearchController::onSearchFailed,
            Qt::QueuedConnection);
    connect(searchSvc, &SearchService::searchCancelled, this, &SearchController::onSearchCancelled,
            Qt::QueuedConnection);
    connect(searchSvc, &SearchService::searchBatch, this, &SearchController::onSearchBatch);
    connect(searchSvc, &SearchService::searchProgress, this, [this](int p_token, int p_percent) {
      if (m_activeTokens.contains(p_token) && !m_cancelRequested) {
        emit progressUpdated(p_percent);
      }
    });
  }
  if (auto *bufferSvc = m_services.get<BufferService>()) {
    connect(bufferSvc->asQObject(),
            SIGNAL(searchReplacementFinished(int, NodeIdentifier, QString, int, bool, QString)),
            this,
            SLOT(onSearchReplacementFinished(int, NodeIdentifier, QString, int, bool, QString)));
  }
}

SearchController::~SearchController() {
  if (auto *searchSvc = m_services.get<SearchService>()) {
    for (int token : m_activeTokens) {
      searchSvc->cancel(token);
    }
  }
  if (m_replacement && m_replacement->activeToken > 0) {
    if (auto *bufferSvc = m_services.get<BufferService>()) {
      bufferSvc->cancelSearchReplacement(m_replacement->activeToken);
    }
  }
}

void SearchController::setModel(SearchResultModel *p_model) {
  if (m_model == p_model) {
    return;
  }
  if (m_model) {
    disconnect(m_model, nullptr, this, nullptr);
  }
  m_model = p_model;
  invalidateReplacementResults();
  m_selectedTargets.clear();
  if (m_model) {
    connect(m_model, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
      m_selectedTargets.clear();
      if (!m_updatingModel) {
        invalidateReplacementResults();
      }
    });
    const auto changed = [this]() {
      if (!m_updatingModel) {
        m_selectedTargets.clear();
        invalidateReplacementResults();
      }
    };
    connect(m_model, &QAbstractItemModel::rowsInserted, this, changed);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, changed);
    connect(m_model, &QAbstractItemModel::dataChanged, this, changed);
    connect(m_model, &QObject::destroyed, this, [this]() {
      m_model = nullptr;
      m_selectedTargets.clear();
      invalidateReplacementResults();
    });
  }
}

void SearchController::setCurrentNotebookId(const QString &p_notebookId) {
  if (m_currentNotebookId != p_notebookId) {
    m_currentNotebookId = p_notebookId;
    invalidateReplacementResults();
  }
}

void SearchController::setCurrentFolderId(const NodeIdentifier &p_folderId) {
  if (m_currentFolderId != p_folderId) {
    m_currentFolderId = p_folderId;
    invalidateReplacementResults();
  }
}

void SearchController::search(const QString &p_keyword, int p_scope, int p_searchMode,
                              bool p_caseSensitive, bool p_useRegex, const QString &p_filePattern,
                              const FileSearchOptions &p_fileSearchOptions) {
  qCDebug(lcUi) << "SearchController::search: keyword:" << p_keyword << "scope:" << p_scope
                << "mode:" << p_searchMode << "caseSensitive:" << p_caseSensitive
                << "regex:" << p_useRegex << "filePattern:" << p_filePattern;

  ++m_searchGeneration;
  cancel();
  resetSearchState();
  invalidateReplacementResults();
  m_resultsInvalidated = false;
  publishSearchResult(SearchResult());

  m_activeSearchMode = p_searchMode;

  m_lastKeyword = p_keyword;
  m_lastFindOptions = FindNone;
  if (p_caseSensitive) {
    m_lastFindOptions |= CaseSensitive;
  }
  if (p_useRegex) {
    m_lastFindOptions |= RegularExpression;
  }

  m_queryJson = buildQueryJson(p_keyword, p_searchMode, p_caseSensitive, p_useRegex, p_filePattern,
                               p_fileSearchOptions);
  if (m_queryJson.isEmpty()) {
    emit searchFailed(tr("Failed to build search query."));
    return;
  }

  switch (p_scope) {
  case AllNotebooks: {
    auto *notebookSvc = m_services.get<NotebookCoreService>();
    if (!notebookSvc) {
      emit searchFailed(tr("Notebook service is not available."));
      return;
    }

    const QJsonArray notebooks = notebookSvc->listNotebooks();
    for (const QJsonValue &val : notebooks) {
      const QString notebookId = val.toObject().value(QLatin1String(vxcore::kJsonKeyId)).toString();
      if (!notebookId.isEmpty()) {
        SearchTarget target;
        target.notebookId = notebookId;
        m_pendingTargets.append(target);
      }
    }
    break;
  }

  case CurrentNotebook: {
    if (m_currentNotebookId.isEmpty()) {
      emit searchFailed(tr("No current notebook selected."));
      return;
    }

    SearchTarget target;
    target.notebookId = m_currentNotebookId;
    m_pendingTargets.append(target);
    break;
  }

  case CurrentFolder: {
    if (!m_currentFolderId.isValid()) {
      emit searchFailed(tr("No current folder selected."));
      return;
    }

    SearchTarget target;
    target.notebookId = m_currentFolderId.notebookId;
    if (!m_currentFolderId.relativePath.isEmpty()) {
      target.inputFilesJson = buildFoldersInputFilesJson(m_currentFolderId.relativePath);
    }
    m_pendingTargets.append(target);
    break;
  }

  case Buffers: {
    auto *bufferSvc = m_services.get<BufferService>();
    if (!bufferSvc) {
      emit searchFailed(tr("Buffer service is not available."));
      return;
    }

    const QJsonArray buffers = bufferSvc->listBuffers();
    QHash<QString, QSet<QString>> filesByNotebook;
    for (const QJsonValue &val : buffers) {
      const QJsonObject obj = val.toObject();
      const QString notebookId = obj.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
      QString filePath = obj.value(QStringLiteral("path")).toString();
      if (filePath.isEmpty()) {
        filePath = obj.value(QStringLiteral("filePath")).toString();
      }

      if (!notebookId.isEmpty() && !filePath.isEmpty()) {
        filesByNotebook[notebookId].insert(filePath);
      }
    }

    for (auto it = filesByNotebook.constBegin(); it != filesByNotebook.constEnd(); ++it) {
      SearchTarget target;
      target.notebookId = it.key();
      target.inputFilesJson = buildFilesInputFilesJson(it.value().values());
      m_pendingTargets.append(target);
    }
    break;
  }

  default:
    emit searchFailed(tr("Invalid search scope."));
    return;
  }

  if (m_pendingTargets.isEmpty()) {
    qCDebug(lcUi) << "SearchController::search: no targets found, emitting empty result";
    resetSearchState();
    emit searchFinished(0, false, 0);
    return;
  }

  qCDebug(lcUi) << "SearchController::search: pendingTargets:" << m_pendingTargets.size();
  const SearchSnapshot snapshot{m_pendingTargets, m_queryJson, m_lastKeyword, m_lastFindOptions,
                                m_activeSearchMode};
  beginSearch(snapshot);
}

void SearchController::cancel() {
  if (m_replacement && m_replacement->confirmed) {
    m_replacement->cancelled = true;
    if (auto *bufferSvc = m_services.get<BufferService>()) {
      if (m_replacement->activeToken > 0) {
        bufferSvc->cancelSearchReplacement(m_replacement->activeToken);
      }
    }
    QTimer::singleShot(0, this, &SearchController::startNextReplacement);
  }
  if (!m_searchRunning) {
    return;
  }
  m_cancelRequested = true;
  m_pendingTargets.clear();
  m_completedSearchSuccessful = false;
  updateReplacementAvailability();
  if (auto *searchSvc = m_services.get<SearchService>()) {
    // Keep ownership until the terminal callback, including a finish already queued at cancel.
    for (int token : m_activeTokens) {
      searchSvc->cancel(token);
    }
  }
}

void SearchController::activateResult(const QModelIndex &p_index) {
  if (!p_index.isValid() || !m_model) {
    return;
  }

  QVariant nodeIdVar = m_model->data(p_index, SearchResultModel::NodeIdRole);
  if (!nodeIdVar.isValid()) {
    return;
  }

  NodeIdentifier nodeId = nodeIdVar.value<NodeIdentifier>();
  if (!nodeId.isValid()) {
    return;
  }

  FileOpenSettings settings;
  int lineNumber = m_model->data(p_index, SearchResultModel::LineNumberRole).toInt();
  if (lineNumber > 0) {
    settings.m_lineNumber = lineNumber - 1;
  }

  if (m_activeSearchMode == ContentSearch && !m_lastKeyword.isEmpty()) {
    SearchHighlightContext ctx;
    ctx.m_patterns = QStringList{m_lastKeyword};
    ctx.m_options = m_lastFindOptions;
    ctx.m_currentMatchLine = lineNumber > 0 ? lineNumber - 1 : -1;
    ctx.m_isValid = true;
    settings.m_searchHighlight = ctx;
  }

  qCDebug(lcUi) << "SearchController::activateResult: notebookId:" << nodeId.notebookId
                << "path:" << nodeId.relativePath << "lineNumber:" << lineNumber;

  emit nodeActivated(nodeId, settings);
}

void SearchController::onSearchFinished(int p_token, const SearchResult &p_result) {
  if (!m_activeTokens.contains(p_token)) {
    return;
  }
  if (m_cancelRequested) {
    onSearchCancelled(p_token);
    return;
  }
  m_activeTokens.remove(p_token);
  mergeSearchResult(p_result);
  if (!m_pendingTargets.isEmpty()) {
    startNextSearch();
    return;
  }

  m_completedSearch = m_searchSnapshot;
  m_completedTruncated = m_accumulatedResult.m_truncated;
  m_completedProvenance = !m_accumulatedResult.m_fileResults.isEmpty();
  for (const auto &file : m_accumulatedResult.m_fileResults) {
    m_completedProvenance = m_completedProvenance && file.m_replacementSupported;
  }
  const auto result = m_accumulatedResult;
  const auto generation = m_searchGeneration;
  publishSearchResult(result);
  if (generation != m_searchGeneration) {
    return;
  }
  m_completedSearchSuccessful = !m_resultsInvalidated;
  resetSearchState();
  updateReplacementAvailability();
  emit searchFinished(result.m_matchCount, result.m_truncated, result.m_encryptedSkippedCount);
}

void SearchController::onSearchFailed(int p_token, const Error &p_error) {
  if (!m_activeTokens.contains(p_token)) {
    return;
  }
  if (m_cancelRequested) {
    onSearchCancelled(p_token);
    return;
  }
  QString message = p_error.message();
  if (message.isEmpty()) {
    message = p_error.what();
  }
  m_completedSearchSuccessful = false;
  resetSearchState();
  updateReplacementAvailability();
  emit searchFailed(message);
}

void SearchController::onSearchCancelled(int p_token) {
  if (!m_activeTokens.remove(p_token)) {
    return;
  }
  m_completedSearchSuccessful = false;
  resetSearchState();
  updateReplacementAvailability();
  emit searchCancelled();
}

void SearchController::onSearchBatch(int p_token, const SearchResult &p_result) {
  // Live incremental rendering: append each streamed content-search chunk to the model as it
  // arrives. The authoritative, deterministically-ordered result is applied once at completion
  // via setSearchResult() in onSearchFinished(), which supersedes these preview rows.
  if (m_cancelRequested || !m_activeTokens.contains(p_token)) {
    qCDebug(lcUi) << "SearchController::onSearchBatch: discarding batch for token:" << p_token;
    return;
  }

  if (p_result.m_fileResults.isEmpty()) {
    return;
  }

  qCDebug(lcUi) << "SearchController::onSearchBatch: token:" << p_token
                << "fileResults:" << p_result.m_fileResults.size()
                << "matchCount:" << p_result.m_matchCount;

  if (m_model) {
    QScopedValueRollback<bool> updating(m_updatingModel, true);
    m_model->appendSearchResult(p_result);
  }
}

void SearchController::setSelectedResults(const QModelIndexList &p_indexes) {
  m_selectedTargets =
      m_model ? m_model->replacementTargets(p_indexes) : QVector<SearchFileResult>();
  updateReplacementAvailability();
}

void SearchController::invalidateReplacementResults() {
  ++m_resultsGeneration;
  // Sticky across final publication and automatic refresh; only an explicit search resets it.
  m_resultsInvalidated = true;
  m_completedSearchSuccessful = false;
  updateReplacementAvailability();
}

QString SearchController::replacementEligibilityError() const {
  if (m_searchRunning || (m_replacement && m_replacement->confirmed)) {
    return tr("Wait for the current operation to finish before replacing.");
  }
  if (!m_model || !m_completedSearchSuccessful || m_resultsInvalidated ||
      m_completedSearch.mode != ContentSearch || m_model->rowCount() == 0) {
    return tr("Run a new completed content search before replacing.");
  }
  if (m_completedTruncated) {
    return tr(
        "Results are truncated. Narrow the search or increase the result limit before replacing.");
  }
  const auto *searchSvc = m_services.get<SearchService>();
  if (!m_completedProvenance || !searchSvc || !searchSvc->isReplacementSupported()) {
    return tr("Replacement is available only for Simple search results.");
  }
  if (!m_services.get<BufferService>()) {
    return tr("Buffer service is not available.");
  }
  return QString();
}

void SearchController::updateReplacementAvailability() {
  const QString reason = replacementEligibilityError();
  const bool available = !m_replacement && reason.isEmpty();
  emit replacementAvailabilityChanged(available && !m_selectedTargets.isEmpty(), available);
  const bool explain = !m_searchRunning && !m_replacement && m_completedSearchSuccessful &&
                       m_completedSearch.mode == ContentSearch && m_model &&
                       m_model->rowCount() > 0;
  emit replacementStatusChanged(explain ? reason : QString());
}

void SearchController::requestReplacement(const QString &p_replacement, bool p_all) {
  if (m_replacement) {
    return;
  }
  const QString error = replacementEligibilityError();
  if (!error.isEmpty()) {
    updateReplacementAvailability();
    emit replacementFinished(0, 0, false, {error});
    return;
  }
  auto targets = p_all ? m_model->allReplacementTargets() : m_selectedTargets;
  if (targets.isEmpty()) {
    return;
  }
  qint64 matches = 0;
  for (const auto &file : targets) {
    if (!file.m_replacementSupported) {
      emit replacementFinished(0, 0, false,
                               {tr("Replacement is available only for Simple search results.")});
      return;
    }
    for (const auto &line : file.m_lineMatches) {
      matches += line.m_segments.size();
    }
  }
  if (matches > (std::numeric_limits<int>::max)() ||
      targets.size() > (std::numeric_limits<int>::max)()) {
    emit replacementFinished(0, 0, false,
                             {tr("Too many matches. Narrow the search before replacing.")});
    return;
  }
  if (matches == 0) {
    return;
  }
  m_replacement = std::make_unique<ReplacementBatch>();
  m_replacement->targets = std::move(targets);
  m_replacement->replacement = p_replacement;
  m_replacement->search = m_completedSearch;
  m_replacement->searchGeneration = m_searchGeneration;
  m_replacement->resultsGeneration = m_resultsGeneration;
  const int files = static_cast<int>(m_replacement->targets.size());
  updateReplacementAvailability();
  emit replacementConfirmationRequested(static_cast<int>(matches), files);
}

void SearchController::confirmReplacement(bool p_confirmed) {
  if (!m_replacement || m_replacement->confirmed) {
    return;
  }
  if (!p_confirmed) {
    m_replacement.reset();
    updateReplacementAvailability();
    return;
  }
  QString error = replacementEligibilityError();
  if (m_replacement->searchGeneration != m_searchGeneration ||
      m_replacement->resultsGeneration != m_resultsGeneration) {
    error = tr("Search results changed. Search again before replacing.");
  }
  for (const auto &file : m_replacement->targets) {
    if (!file.m_replacementSupported) {
      error = tr("Replacement is available only for Simple search results.");
      break;
    }
  }
  if (!error.isEmpty()) {
    m_replacement.reset();
    updateReplacementAvailability();
    emit replacementFinished(0, 0, false, {error});
    return;
  }
  m_replacement->confirmed = true;
  updateReplacementAvailability();
  emit replacementStarted(static_cast<int>(m_replacement->targets.size()));
  // Also gives a synchronous Cancel from the view's started handler a pre-dispatch boundary.
  QTimer::singleShot(0, this, &SearchController::startNextReplacement);
}

void SearchController::startNextReplacement() {
  if (!m_replacement || !m_replacement->confirmed || m_replacement->activeToken > 0) {
    return;
  }
  if (m_replacement->cancelled || m_replacement->completedFiles == m_replacement->targets.size()) {
    finishReplacement();
    return;
  }
  const auto &target = m_replacement->targets.at(m_replacement->completedFiles);
  auto *bufferSvc = m_services.get<BufferService>();
  if (bufferSvc) {
    // BufferService guarantees that no terminal callback precedes this method's return.
    m_replacement->activeToken =
        bufferSvc->replaceSearchMatches(target, m_replacement->replacement);
  }
  if (m_replacement->activeToken <= 0) {
    m_replacement->errors.append(target.m_notebookId + QLatin1Char('/') + target.m_path +
                                 QStringLiteral(": ") + tr("Buffer service is not available."));
    ++m_replacement->completedFiles;
    emit replacementProgress(m_replacement->completedFiles,
                             static_cast<int>(m_replacement->targets.size()));
    QTimer::singleShot(0, this, &SearchController::startNextReplacement);
  }
}

void SearchController::onSearchReplacementFinished(int p_token, const NodeIdentifier &p_nodeId,
                                                   const QString &p_bufferId, int p_replacedMatches,
                                                   bool p_saved, const QString &p_error) {
  if (!m_replacement || !m_replacement->confirmed || p_token <= 0 ||
      p_token != m_replacement->activeToken) {
    return;
  }
  m_replacement->activeToken = 0;
  const auto &target = m_replacement->targets.at(m_replacement->completedFiles);
  if (p_saved && p_replacedMatches > 0) {
    m_replacement->replacedMatches += p_replacedMatches;
    ++m_replacement->savedFiles;
  }
  if (!p_error.isEmpty()) {
    m_replacement->errors.append(target.m_notebookId + QLatin1Char('/') + target.m_path +
                                 QStringLiteral(": ") + p_error);
  }
  ++m_replacement->completedFiles;
  if (!p_saved && p_replacedMatches > 0 && !p_bufferId.isEmpty()) {
    FileOpenSettings settings;
    settings.m_mode = ViewWindowMode::Edit;
    settings.m_forceMode = true;
    emit nodeActivated(p_nodeId, settings);
  }
  emit replacementProgress(m_replacement->completedFiles,
                           static_cast<int>(m_replacement->targets.size()));
  // Never dispatch the next file inside completion observers; they may request cancellation.
  QTimer::singleShot(0, this, &SearchController::startNextReplacement);
}

void SearchController::finishReplacement() {
  auto batch = std::move(m_replacement);
  m_completedSearchSuccessful = false;
  ++m_resultsGeneration;
  if (batch->searchGeneration == m_searchGeneration) {
    publishSearchResult(SearchResult());
  }
  updateReplacementAvailability();
  emit replacementFinished(batch->replacedMatches, batch->savedFiles, batch->cancelled,
                           batch->errors);
  const auto generation = batch->searchGeneration;
  QTimer::singleShot(0, this, [this, generation, snapshot = std::move(batch->search)]() {
    if (generation != m_searchGeneration) {
      return;
    }
    beginSearch(snapshot);
  });
}

void SearchController::publishSearchResult(const SearchResult &p_result) {
  m_selectedTargets.clear();
  if (m_model) {
    QScopedValueRollback<bool> updating(m_updatingModel, true);
    m_model->setSearchResult(p_result);
  }
}

void SearchController::beginSearch(const SearchSnapshot &p_search) {
  resetSearchState();
  m_searchSnapshot = p_search;
  m_pendingTargets = p_search.targets;
  m_queryJson = p_search.queryJson;
  m_activeSearchMode = p_search.mode;
  m_lastKeyword = p_search.keyword;
  m_lastFindOptions = p_search.findOptions;
  m_searchRunning = true;
  updateReplacementAvailability();
  startNextSearch();
}

QString SearchController::buildQueryJson(const QString &p_keyword, int p_searchMode,
                                         bool p_caseSensitive, bool p_useRegex,
                                         const QString &p_filePattern,
                                         const FileSearchOptions &p_fileSearchOptions) const {
  QJsonObject queryObj;

  const int maxResults = m_services.get<ConfigMgr2>()->getCoreConfig().getSearchMaxResults();

  switch (p_searchMode) {
  case ContentSearch:
    queryObj.insert(QStringLiteral("pattern"), QJsonValue(p_keyword));
    queryObj.insert(QStringLiteral("caseSensitive"), QJsonValue(p_caseSensitive));
    queryObj.insert(QStringLiteral("wholeWord"), QJsonValue(false));
    queryObj.insert(QStringLiteral("regex"), QJsonValue(p_useRegex));
    queryObj.insert(QStringLiteral("maxResults"), QJsonValue(maxResults));
    break;

  case FileNameSearch: {
    queryObj.insert(QStringLiteral("pattern"), QJsonValue(p_keyword));
    queryObj.insert(QStringLiteral("includeFiles"), QJsonValue(true));
    queryObj.insert(QStringLiteral("includeFolders"),
                    QJsonValue(p_fileSearchOptions.includeFolders));
    QString matchTarget;
    switch (p_fileSearchOptions.matchTarget) {
    case MatchName:
      matchTarget = QStringLiteral("name");
      break;
    case MatchPath:
      matchTarget = QStringLiteral("path");
      break;
    case MatchNameAndPath:
      matchTarget = QStringLiteral("nameAndPath");
      break;
    }
    queryObj.insert(QLatin1String(vxcore::kJsonKeyMatchTarget), QJsonValue(matchTarget));
    queryObj.insert(QStringLiteral("maxResults"), QJsonValue(maxResults));
    break;
  }

  case TagSearch: {
    QJsonArray tags;
    const QStringList tagList = p_keyword.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &tag : tagList) {
      const QString trimmed = tag.trimmed();
      if (!trimmed.isEmpty()) {
        tags.append(trimmed);
      }
    }

    if (tags.isEmpty() && !p_keyword.trimmed().isEmpty()) {
      tags.append(p_keyword.trimmed());
    }

    queryObj.insert(QStringLiteral("tags"), tags);
    queryObj.insert(QStringLiteral("operator"), QJsonValue(QStringLiteral("AND")));
    queryObj.insert(QStringLiteral("maxResults"), QJsonValue(maxResults));
    break;
  }

  default:
    return QString();
  }

  if (!p_filePattern.isEmpty()) {
    QJsonObject scopeObj;
    QJsonArray filePatterns;
    filePatterns.append(p_filePattern);
    scopeObj.insert(QStringLiteral("filePatterns"), filePatterns);
    queryObj.insert(QStringLiteral("scope"), scopeObj);
  }

  return QString::fromUtf8(QJsonDocument(queryObj).toJson(QJsonDocument::Compact));
}

QString SearchController::buildFoldersInputFilesJson(const QString &p_folderPath) const {
  QJsonObject inputObj;
  QJsonArray folders;
  folders.append(p_folderPath);
  inputObj.insert(QStringLiteral("folders"), folders);
  return QString::fromUtf8(QJsonDocument(inputObj).toJson(QJsonDocument::Compact));
}

QString SearchController::buildFilesInputFilesJson(const QStringList &p_files) const {
  QJsonObject inputObj;
  QJsonArray files;
  for (const QString &file : p_files) {
    files.append(file);
  }
  inputObj.insert(QStringLiteral("files"), files);
  return QString::fromUtf8(QJsonDocument(inputObj).toJson(QJsonDocument::Compact));
}

void SearchController::dispatchSearch(const SearchTarget &p_target) {
  qCDebug(lcUi) << "SearchController::dispatchSearch: notebookId:" << p_target.notebookId
                << "mode:" << m_activeSearchMode
                << "hasInputFiles:" << !p_target.inputFilesJson.isEmpty();

  auto *searchSvc = m_services.get<SearchService>();
  if (!searchSvc) {
    qWarning() << "SearchController::dispatchSearch: SearchService not available";
    resetSearchState();
    emit searchFailed(tr("Search service is not available."));
    return;
  }

  int token = 0;
  switch (m_activeSearchMode) {
  case FileNameSearch:
    token = searchSvc->searchFiles(p_target.notebookId, m_queryJson, p_target.inputFilesJson);
    break;

  case ContentSearch:
    token = searchSvc->searchContent(p_target.notebookId, m_queryJson, p_target.inputFilesJson);
    break;

  case TagSearch:
    token = searchSvc->searchByTags(p_target.notebookId, m_queryJson, p_target.inputFilesJson);
    break;

  default:
    resetSearchState();
    emit searchFailed(tr("Invalid search mode."));
    return;
  }

  if (token > 0) {
    m_activeTokens.insert(token);
    if (!m_searchStartedEmitted) {
      m_searchStartedEmitted = true;
      emit searchStarted();
    }
  } else {
    resetSearchState();
    emit searchFailed(tr("Failed to start search."));
  }
}

void SearchController::startNextSearch() {
  if (m_pendingTargets.isEmpty()) {
    return;
  }

  qCDebug(lcUi) << "SearchController::startNextSearch: remaining:" << m_pendingTargets.size();

  const SearchTarget target = m_pendingTargets.takeFirst();
  dispatchSearch(target);
}

void SearchController::resetSearchState() {
  m_pendingTargets.clear();
  m_activeTokens.clear();
  m_accumulatedResult = SearchResult();
  m_queryJson.clear();
  m_cancelRequested = false;
  m_searchRunning = false;
  m_searchStartedEmitted = false;
  m_searchSnapshot = SearchSnapshot();
}

void SearchController::mergeSearchResult(const SearchResult &p_result) {
  m_accumulatedResult.m_fileResults += p_result.m_fileResults;
  m_accumulatedResult.m_matchCount += p_result.m_matchCount;
  m_accumulatedResult.m_truncated = m_accumulatedResult.m_truncated || p_result.m_truncated;
  m_accumulatedResult.m_encryptedSkippedCount += p_result.m_encryptedSkippedCount;

  qCDebug(lcUi) << "SearchController::mergeSearchResult: accumulated matchCount:"
                << m_accumulatedResult.m_matchCount
                << "fileResults:" << m_accumulatedResult.m_fileResults.size()
                << "truncated:" << m_accumulatedResult.m_truncated;
}
