#ifndef SEARCHCONTROLLER_H
#define SEARCHCONTROLLER_H

#include <QList>
#include <QModelIndex>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

#include <core/fileopensettings.h>
#include <core/nodeidentifier.h>
#include <core/searchresulttypes.h>

namespace vnotex {

class Error;
class SearchResultModel;
class ServiceLocator;

class SearchController : public QObject {
  Q_OBJECT

public:
  enum SearchScope { Buffers = 0, CurrentFolder = 1, CurrentNotebook = 2, AllNotebooks = 3 };

  enum SearchMode { FileNameSearch = 0, ContentSearch = 1, TagSearch = 2 };
  enum FileSearchMatchTarget { MatchName = 0, MatchPath = 1, MatchNameAndPath = 2 };

  struct FileSearchOptions {
    bool includeFolders = true;
    FileSearchMatchTarget matchTarget = MatchNameAndPath;
  };

  explicit SearchController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  ~SearchController() override;

  void setModel(SearchResultModel *p_model);

  void setCurrentNotebookId(const QString &p_notebookId);
  void setCurrentFolderId(const NodeIdentifier &p_folderId);

  void search(const QString &p_keyword, int p_scope, int p_searchMode, bool p_caseSensitive,
              bool p_useRegex, const QString &p_filePattern,
              const FileSearchOptions &p_fileSearchOptions);
  void cancel();
  void activateResult(const QModelIndex &p_index);

  void setSelectedResults(const QModelIndexList &p_indexes);
  void invalidateReplacementResults();
  void requestReplacement(const QString &p_replacement, bool p_all);
  void confirmReplacement(bool p_confirmed);

signals:
  void nodeActivated(const NodeIdentifier &p_nodeId, const FileOpenSettings &p_settings);
  void searchStarted();
  void searchFinished(int p_totalMatches, bool p_truncated, int p_encryptedSkippedCount);
  void searchFailed(const QString &p_errorMessage);
  void searchCancelled();
  void progressUpdated(int p_percent);
  void replacementAvailabilityChanged(bool p_selected, bool p_all);
  void replacementStatusChanged(const QString &p_reason);
  void replacementConfirmationRequested(int p_matches, int p_files);
  void replacementStarted(int p_files);
  void replacementProgress(int p_completedFiles, int p_totalFiles);
  void replacementFinished(int p_replacedMatches, int p_savedFiles, bool p_cancelled,
                           const QStringList &p_errors);

private slots:
  void onSearchFinished(int p_token, const SearchResult &p_result);
  void onSearchFailed(int p_token, const Error &p_error);
  void onSearchCancelled(int p_token);
  void onSearchBatch(int p_token, const SearchResult &p_result);
  void onSearchReplacementFinished(int p_token, const NodeIdentifier &p_nodeId,
                                   const QString &p_bufferId, int p_replacedMatches, bool p_saved,
                                   const QString &p_error);

private:
  struct SearchTarget {
    QString notebookId;
    QString inputFilesJson;
  };

  struct SearchSnapshot {
    QList<SearchTarget> targets;
    QString queryJson;
    QString keyword;
    FindOptions findOptions = FindNone;
    int mode = ContentSearch;
  };

  struct ReplacementBatch {
    QVector<SearchFileResult> targets;
    QString replacement;
    SearchSnapshot search;
    quint64 searchGeneration = 0;
    quint64 resultsGeneration = 0;
    bool confirmed = false;
    bool cancelled = false;
    int activeToken = 0;
    int completedFiles = 0;
    int savedFiles = 0;
    int replacedMatches = 0;
    QStringList errors;
  };

  QString replacementEligibilityError() const;
  void updateReplacementAvailability();
  void startNextReplacement();
  void finishReplacement();
  void publishSearchResult(const SearchResult &p_result);
  void beginSearch(const SearchSnapshot &p_search);

  QString buildQueryJson(const QString &p_keyword, int p_searchMode, bool p_caseSensitive,
                         bool p_useRegex, const QString &p_filePattern,
                         const FileSearchOptions &p_fileSearchOptions) const;
  QString buildFoldersInputFilesJson(const QString &p_folderPath) const;
  QString buildFilesInputFilesJson(const QStringList &p_files) const;
  void dispatchSearch(const SearchTarget &p_target);
  void startNextSearch();
  void resetSearchState();
  void mergeSearchResult(const SearchResult &p_result);

  ServiceLocator &m_services;
  QPointer<SearchResultModel> m_model;

  QString m_currentNotebookId;
  NodeIdentifier m_currentFolderId;

  QList<SearchTarget> m_pendingTargets;
  SearchResult m_accumulatedResult;
  QString m_queryJson;
  int m_activeSearchMode = ContentSearch;
  bool m_cancelRequested = false;
  QSet<int> m_activeTokens;
  bool m_searchRunning = false;
  bool m_searchStartedEmitted = false;

  // Completed addresses and refresh scope outlive the transient draining search state.
  SearchSnapshot m_searchSnapshot;
  SearchSnapshot m_completedSearch;
  bool m_completedSearchSuccessful = false;
  bool m_completedProvenance = false;
  bool m_completedTruncated = false;
  bool m_resultsInvalidated = true;
  bool m_updatingModel = false;
  quint64 m_searchGeneration = 0;
  quint64 m_resultsGeneration = 0;
  QVector<SearchFileResult> m_selectedTargets;
  std::unique_ptr<ReplacementBatch> m_replacement;

  QString m_lastKeyword;
  FindOptions m_lastFindOptions = FindNone;
};

} // namespace vnotex

#endif // SEARCHCONTROLLER_H
