#include "searchcontroller.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QModelIndex>
#include <QSet>
#include <QStringList>

#include <core/error.h>
#include <core/fileopensettings.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/searchservice.h>
#include <models/searchresultmodel.h>

using namespace vnotex;

SearchController::SearchController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  auto *searchSvc = m_services.get<SearchService>();
  if (!searchSvc) {
    return;
  }

  connect(searchSvc, &SearchService::searchFinished, this, &SearchController::onSearchFinished);
  connect(searchSvc, &SearchService::searchFailed, this, &SearchController::onSearchFailed);
  connect(searchSvc, &SearchService::searchCancelled, this, &SearchController::onSearchCancelled);
  connect(searchSvc, &SearchService::searchProgress, this, &SearchController::progressUpdated);
  connect(searchSvc, &SearchService::searchStarted, this, &SearchController::searchStarted);
}

void SearchController::setModel(SearchResultModel *p_model) {
  m_model = p_model;
}

void SearchController::setCurrentNotebookId(const QString &p_notebookId) {
  m_currentNotebookId = p_notebookId;
}

void SearchController::setCurrentFolderId(const NodeIdentifier &p_folderId) {
  m_currentFolderId = p_folderId;
}

void SearchController::search(const QString &p_keyword, int p_scope, int p_searchMode,
                              bool p_caseSensitive, bool p_useRegex,
                              const QString &p_filePattern) {
  resetSearchState();
  if (m_model) {
    m_model->clear();
  }

  m_activeSearchMode = p_searchMode;
  m_queryJson = buildQueryJson(p_keyword, p_searchMode, p_caseSensitive, p_useRegex, p_filePattern);
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
      const QString notebookId = val.toObject().value(QStringLiteral("id")).toString();
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
      const QString notebookId = obj.value(QStringLiteral("notebookId")).toString();
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
    if (m_model) {
      m_model->setSearchResult(m_accumulatedResult);
    }
    emit searchFinished(0, false);
    return;
  }

  startNextSearch();
}

void SearchController::cancel() {
  m_cancelRequested = true;
  m_pendingTargets.clear();

  auto *searchSvc = m_services.get<SearchService>();
  if (searchSvc) {
    searchSvc->cancel();
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
  if (lineNumber >= 0) {
    settings.m_lineNumber = lineNumber;
  }

  emit nodeActivated(nodeId, settings);
}

void SearchController::onSearchFinished(const SearchResult &p_result) {
  mergeSearchResult(p_result);

  if (m_cancelRequested) {
    return;
  }

  if (!m_pendingTargets.isEmpty()) {
    startNextSearch();
    return;
  }

  if (m_model) {
    m_model->setSearchResult(m_accumulatedResult);
  }

  emit searchFinished(m_accumulatedResult.m_matchCount, m_accumulatedResult.m_truncated);
  resetSearchState();
}

void SearchController::onSearchFailed(const Error &p_error) {
  QString message = p_error.message();
  if (message.isEmpty()) {
    message = p_error.what();
  }

  emit searchFailed(message);
  resetSearchState();
}

void SearchController::onSearchCancelled() {
  emit searchCancelled();
  resetSearchState();
}

QString SearchController::buildQueryJson(const QString &p_keyword, int p_searchMode,
                                         bool p_caseSensitive, bool p_useRegex,
                                         const QString &p_filePattern) const {
  QJsonObject queryObj;

  switch (p_searchMode) {
  case ContentSearch:
    queryObj.insert(QStringLiteral("pattern"), QJsonValue(p_keyword));
    queryObj.insert(QStringLiteral("caseSensitive"), QJsonValue(p_caseSensitive));
    queryObj.insert(QStringLiteral("wholeWord"), QJsonValue(false));
    queryObj.insert(QStringLiteral("regex"), QJsonValue(p_useRegex));
    queryObj.insert(QStringLiteral("maxResults"), QJsonValue(500));
    break;

  case FileNameSearch:
    queryObj.insert(QStringLiteral("pattern"), QJsonValue(p_keyword));
    queryObj.insert(QStringLiteral("includeFiles"), QJsonValue(true));
    queryObj.insert(QStringLiteral("includeFolders"), QJsonValue(true));
    queryObj.insert(QStringLiteral("maxResults"), QJsonValue(500));
    break;

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
    queryObj.insert(QStringLiteral("maxResults"), QJsonValue(500));
    break;
  }

  default:
    return QString();
  }

  if (!p_filePattern.isEmpty()) {
    QJsonObject scopeObj;
    QJsonArray pathPatterns;
    pathPatterns.append(p_filePattern);
    scopeObj.insert(QStringLiteral("pathPatterns"), pathPatterns);
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
  auto *searchSvc = m_services.get<SearchService>();
  if (!searchSvc) {
    emit searchFailed(tr("Search service is not available."));
    resetSearchState();
    return;
  }

  switch (m_activeSearchMode) {
  case FileNameSearch:
    searchSvc->searchFiles(p_target.notebookId, m_queryJson, p_target.inputFilesJson);
    break;

  case ContentSearch:
    searchSvc->searchContent(p_target.notebookId, m_queryJson, p_target.inputFilesJson);
    break;

  case TagSearch:
    searchSvc->searchByTags(p_target.notebookId, m_queryJson, p_target.inputFilesJson);
    break;

  default:
    emit searchFailed(tr("Invalid search mode."));
    resetSearchState();
    break;
  }
}

void SearchController::startNextSearch() {
  if (m_pendingTargets.isEmpty()) {
    return;
  }

  const SearchTarget target = m_pendingTargets.takeFirst();
  dispatchSearch(target);
}

void SearchController::resetSearchState() {
  m_pendingTargets.clear();
  m_accumulatedResult = SearchResult();
  m_queryJson.clear();
  m_cancelRequested = false;
}

void SearchController::mergeSearchResult(const SearchResult &p_result) {
  m_accumulatedResult.m_fileResults += p_result.m_fileResults;
  m_accumulatedResult.m_matchCount += p_result.m_matchCount;
  m_accumulatedResult.m_truncated = m_accumulatedResult.m_truncated || p_result.m_truncated;
}
