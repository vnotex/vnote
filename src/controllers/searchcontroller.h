#ifndef SEARCHCONTROLLER_H
#define SEARCHCONTROLLER_H

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <core/fileopensettings.h>
#include <core/nodeidentifier.h>
#include <core/searchresulttypes.h>

class QModelIndex;

namespace vnotex {

class Error;
class SearchResultModel;
class ServiceLocator;

class SearchController : public QObject {
  Q_OBJECT

public:
  enum SearchScope { Buffers = 0, CurrentFolder = 1, CurrentNotebook = 2, AllNotebooks = 3 };

  enum SearchMode { FileNameSearch = 0, ContentSearch = 1, TagSearch = 2 };

  explicit SearchController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  void setModel(SearchResultModel *p_model);

  void setCurrentNotebookId(const QString &p_notebookId);
  void setCurrentFolderId(const NodeIdentifier &p_folderId);

  void search(const QString &p_keyword, int p_scope, int p_searchMode, bool p_caseSensitive,
              bool p_useRegex, const QString &p_filePattern);
  void cancel();
  void activateResult(const QModelIndex &p_index);

signals:
  void nodeActivated(const NodeIdentifier &p_nodeId, const FileOpenSettings &p_settings);
  void searchStarted();
  void searchFinished(int p_totalMatches, bool p_truncated);
  void searchFailed(const QString &p_errorMessage);
  void searchCancelled();
  void progressUpdated(int p_percent);

private slots:
  void onSearchFinished(int p_token, const SearchResult &p_result);
  void onSearchFailed(int p_token, const Error &p_error);
  void onSearchCancelled(int p_token);

private:
  struct SearchTarget {
    QString notebookId;
    QString inputFilesJson;
  };

  QString buildQueryJson(const QString &p_keyword, int p_searchMode, bool p_caseSensitive,
                         bool p_useRegex, const QString &p_filePattern) const;
  QString buildFoldersInputFilesJson(const QString &p_folderPath) const;
  QString buildFilesInputFilesJson(const QStringList &p_files) const;
  void dispatchSearch(const SearchTarget &p_target);
  void startNextSearch();
  void resetSearchState();
  void mergeSearchResult(const SearchResult &p_result);

  ServiceLocator &m_services;
  SearchResultModel *m_model = nullptr;

  QString m_currentNotebookId;
  NodeIdentifier m_currentFolderId;

  QList<SearchTarget> m_pendingTargets;
  SearchResult m_accumulatedResult;
  QString m_queryJson;
  int m_activeSearchMode = ContentSearch;
  bool m_cancelRequested = false;
  QSet<int> m_activeTokens;

  QString m_lastKeyword;
  FindOptions m_lastFindOptions = FindNone;
};

} // namespace vnotex

#endif // SEARCHCONTROLLER_H
