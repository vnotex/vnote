#ifndef SEARCHRESULTTYPES_H
#define SEARCHRESULTTYPES_H

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

#include <vxcore/vxcore_results.h>

namespace vnotex {

// Owning occurrence-level values. Keep these ungrouped until the worker has applied
// its deterministic raw-occurrence cap. No C view escapes conversion.
struct SearchOccurrence {
  int m_lineNumber = -1;
  int m_columnStart = 0;
  int m_columnEnd = 0;
  QString m_lineText;
};

struct SearchContentFile {
  QString m_path;
  QString m_id;
  QVector<SearchOccurrence> m_matches;
};

struct SearchContentResult {
  QVector<SearchContentFile> m_files;
  bool m_truncated = false;
  int m_encryptedSkippedCount = 0;

  static SearchContentResult fromView(const VxCoreContentSearchView &p_view);
};

// A single matched column range within a line. Simple-backed replacement addresses use
// 0-based UTF-16 columns with an end-exclusive endpoint.
struct SearchMatchSegment {
  int m_columnStart = 0;
  int m_columnEnd = 0;
};

// A single matched line within a file (from content search). vxcore emits one
// match per occurrence; occurrences on the same line are grouped here into one
// SearchLineMatch carrying one segment per occurrence, so a line with N matches
// renders as one row with N highlighted ranges (not N duplicated rows).
struct SearchLineMatch {
  int m_lineNumber = -1; // 1-based line number
  // Simple search snapshots omit LF and a trailing CR; neither is part of a match range.
  QString m_lineText;
  QVector<SearchMatchSegment> m_segments;
};

// Type of a search file result.
enum class SearchResultType { File, Folder };

// A single file/folder result from search.
struct SearchFileResult {
  SearchResultType m_type = SearchResultType::File;
  QString m_path;         // Relative path within notebook
  QString m_absolutePath; // Absolute filesystem path (file search only)
  QString m_id;           // Node ID (GUID)
  QString m_notebookId;   // Notebook GUID (set by caller)
  QStringList m_tags;
  qint64 m_createdUtc = 0;
  qint64 m_modifiedUtc = 0;

  // Total match occurrences in this file (content search only). Distinct from
  // m_lineMatches.size(), which counts matched LINES after same-line grouping.
  int m_matchCount = 0;

  // Line-level matches (content search only; empty for file search).
  QVector<SearchLineMatch> m_lineMatches;

  // Qt-side provenance, set only on successful completed Simple-backed content searches.
  // Typed conversion, streaming previews, and file/tag results never grant this capability.
  bool m_replacementSupported = false;
};

// Aggregate search result container.
struct SearchResult {
  QVector<SearchFileResult> m_fileResults;
  int m_matchCount = 0;
  bool m_truncated = false;
  int m_encryptedSkippedCount = 0;

  // Group already-capped occurrences, retaining first-seen line order.
  static SearchResult fromContentSearchResult(const SearchContentResult &p_content,
                                              const QString &p_notebookId);

  // Preserve all native file/tag fields and the native truncation fact.
  static SearchResult fromFileSearchView(const VxCoreFileSearchView &p_view,
                                         const QString &p_notebookId);
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::SearchResult)
Q_DECLARE_METATYPE(vnotex::SearchMatchSegment)
Q_DECLARE_METATYPE(QVector<vnotex::SearchMatchSegment>)

#endif // SEARCHRESULTTYPES_H
