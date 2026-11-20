#include "searchresulttypes.h"

#include <QHash>

using namespace vnotex;

namespace {
QString fromUtf8(const VxCoreUtf8View &p_text) {
  return QString::fromUtf8(p_text.data, static_cast<qsizetype>(p_text.size));
}
} // namespace

SearchContentResult SearchContentResult::fromView(const VxCoreContentSearchView &p_view) {
  SearchContentResult result;
  result.m_truncated = p_view.truncated != 0;
  result.m_encryptedSkippedCount = static_cast<int>(p_view.encrypted_skipped_count);
  result.m_files.reserve(static_cast<qsizetype>(p_view.file_count));
  for (size_t i = 0; i < p_view.file_count; ++i) {
    const auto &file = p_view.files[i];
    SearchContentFile converted;
    converted.m_path = fromUtf8(file.path);
    converted.m_id = fromUtf8(file.id);
    converted.m_matches.reserve(static_cast<qsizetype>(file.match_count));
    for (size_t j = 0; j < file.match_count; ++j) {
      const auto &match = file.matches[j];
      converted.m_matches.append(
          {match.line_number, match.column_start, match.column_end, fromUtf8(match.line_text)});
    }
    result.m_files.append(std::move(converted));
  }
  return result;
}

SearchResult SearchResult::fromContentSearchResult(const SearchContentResult &p_content,
                                                   const QString &p_notebookId) {
  SearchResult result;
  result.m_matchCount = static_cast<int>(p_content.m_files.size());
  result.m_truncated = p_content.m_truncated;
  result.m_encryptedSkippedCount = p_content.m_encryptedSkippedCount;
  result.m_fileResults.reserve(p_content.m_files.size());

  for (const auto &file : p_content.m_files) {
    SearchFileResult fileResult;
    fileResult.m_path = file.m_path;
    fileResult.m_id = file.m_id;
    fileResult.m_notebookId = p_notebookId;
    fileResult.m_matchCount = static_cast<int>(file.m_matches.size());
    fileResult.m_lineMatches.reserve(file.m_matches.size());

    // Preserve first-seen line order even when same-line occurrences are not adjacent.
    QHash<int, int> lineNumberToIndex;
    for (const auto &match : file.m_matches) {
      const SearchMatchSegment segment{match.m_columnStart, match.m_columnEnd};
      auto it = lineNumberToIndex.find(match.m_lineNumber);
      if (it != lineNumberToIndex.end()) {
        fileResult.m_lineMatches[it.value()].m_segments.append(segment);
      } else {
        SearchLineMatch lineMatch;
        lineMatch.m_lineNumber = match.m_lineNumber;
        lineMatch.m_lineText = match.m_lineText;
        lineMatch.m_segments.append(segment);
        lineNumberToIndex.insert(match.m_lineNumber, fileResult.m_lineMatches.size());
        fileResult.m_lineMatches.append(std::move(lineMatch));
      }
    }
    result.m_fileResults.append(std::move(fileResult));
  }
  return result;
}

SearchResult SearchResult::fromFileSearchView(const VxCoreFileSearchView &p_view,
                                              const QString &p_notebookId) {
  SearchResult result;
  result.m_matchCount = static_cast<int>(p_view.file_count);
  result.m_truncated = p_view.truncated != 0;
  result.m_fileResults.reserve(static_cast<qsizetype>(p_view.file_count));
  for (size_t i = 0; i < p_view.file_count; ++i) {
    const auto &file = p_view.files[i];
    SearchFileResult fileResult;
    fileResult.m_type = file.is_folder ? SearchResultType::Folder : SearchResultType::File;
    fileResult.m_path = fromUtf8(file.path);
    fileResult.m_absolutePath = fromUtf8(file.absolute_path);
    fileResult.m_id = fromUtf8(file.id);
    fileResult.m_notebookId = p_notebookId;
    fileResult.m_createdUtc = file.created_utc;
    fileResult.m_modifiedUtc = file.modified_utc;
    fileResult.m_tags.reserve(static_cast<qsizetype>(file.tag_count));
    for (size_t j = 0; j < file.tag_count; ++j) {
      fileResult.m_tags.append(fromUtf8(file.tags[j]));
    }
    result.m_fileResults.append(std::move(fileResult));
  }
  return result;
}
