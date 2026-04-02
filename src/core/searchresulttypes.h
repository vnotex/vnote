#ifndef SEARCHRESULTTYPES_H
#define SEARCHRESULTTYPES_H

#include <QString>
#include <QVector>

class QJsonObject;

namespace vnotex {

// A single line match within a file (from content search).
struct SearchLineMatch {
  int m_lineNumber = -1;  // 0-based line number
  int m_columnStart = 0;
  int m_columnEnd = 0;
  QString m_lineText;
};

// Type of a search file result.
enum class SearchResultType { File, Folder };

// A single file/folder result from search.
struct SearchFileResult {
  SearchResultType m_type = SearchResultType::File;
  QString m_path;          // Relative path within notebook
  QString m_absolutePath;  // Absolute filesystem path (file search only)
  QString m_id;            // Node ID (GUID)
  QString m_notebookId;    // Notebook GUID (set by caller)

  // Line-level matches (content search only; empty for file search).
  QVector<SearchLineMatch> m_lineMatches;
};

// Aggregate search result container.
struct SearchResult {
  QVector<SearchFileResult> m_fileResults;
  int m_matchCount = 0;
  bool m_truncated = false;

  // Build from vxcore content search JSON response.
  // @p_json: full vxcore response with matchCount, truncated, matches.
  // @p_notebookId: notebook GUID to stamp on each file result.
  static SearchResult fromContentSearchJson(const QJsonObject &p_json,
                                            const QString &p_notebookId);

  // Build from vxcore file/tag search JSON response.
  // @p_json: wrapped response with matchCount, truncated, matches.
  // @p_notebookId: notebook GUID to stamp on each file result.
  static SearchResult fromFileSearchJson(const QJsonObject &p_json,
                                         const QString &p_notebookId);
};

} // namespace vnotex

#endif // SEARCHRESULTTYPES_H
