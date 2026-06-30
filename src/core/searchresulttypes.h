#ifndef SEARCHRESULTTYPES_H
#define SEARCHRESULTTYPES_H

#include <QString>
#include <QMetaType>
#include <QVector>

class QJsonObject;

namespace vnotex {

// A single matched column range within a line.
struct SearchMatchSegment {
  int m_columnStart = 0;
  int m_columnEnd = 0;
};

// A single matched line within a file (from content search). vxcore emits one
// match per occurrence; occurrences on the same line are grouped here into one
// SearchLineMatch carrying one segment per occurrence, so a line with N matches
// renders as one row with N highlighted ranges (not N duplicated rows).
struct SearchLineMatch {
  int m_lineNumber = -1;  // 0-based line number
  QString m_lineText;
  QVector<SearchMatchSegment> m_segments;
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

  // Total match occurrences in this file (content search only). Distinct from
  // m_lineMatches.size(), which counts matched LINES after same-line grouping.
  int m_matchCount = 0;

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

Q_DECLARE_METATYPE(vnotex::SearchResult)
Q_DECLARE_METATYPE(vnotex::SearchMatchSegment)
Q_DECLARE_METATYPE(QVector<vnotex::SearchMatchSegment>)

#endif // SEARCHRESULTTYPES_H
