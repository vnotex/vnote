#include "searchresultmodel.h"

#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <core/nodeidentifier.h>

using namespace vnotex;

SearchResultModel::SearchResultModel(QObject *p_parent) : QAbstractItemModel(p_parent) {}

SearchResultModel::~SearchResultModel() {}

QModelIndex SearchResultModel::index(int p_row, int p_column, const QModelIndex &p_parent) const {
  if (p_row < 0 || p_column != 0) {
    return QModelIndex();
  }

  if (!p_parent.isValid()) {
    if (p_row >= m_result.m_fileResults.size()) {
      return QModelIndex();
    }

    return createIndex(p_row, p_column, static_cast<quintptr>(0));
  }

  if (p_parent.column() != 0 || p_parent.internalId() != 0) {
    return QModelIndex();
  }

  if (p_parent.row() < 0 || p_parent.row() >= m_result.m_fileResults.size()) {
    return QModelIndex();
  }

  const auto &fileResult = m_result.m_fileResults[p_parent.row()];
  if (p_row >= fileResult.m_lineMatches.size()) {
    return QModelIndex();
  }

  return createIndex(p_row, p_column, static_cast<quintptr>(p_parent.row() + 1));
}

QModelIndex SearchResultModel::parent(const QModelIndex &p_child) const {
  if (!p_child.isValid()) {
    return QModelIndex();
  }

  quintptr id = p_child.internalId();
  if (id == 0) {
    return QModelIndex();
  }

  int fileRow = static_cast<int>(id) - 1;
  if (fileRow < 0 || fileRow >= m_result.m_fileResults.size()) {
    return QModelIndex();
  }

  return createIndex(fileRow, 0, static_cast<quintptr>(0));
}

int SearchResultModel::rowCount(const QModelIndex &p_parent) const {
  if (!p_parent.isValid()) {
    return m_result.m_fileResults.size();
  }

  if (p_parent.column() != 0) {
    return 0;
  }

  if (p_parent.internalId() != 0) {
    return 0;
  }

  int fileRow = p_parent.row();
  if (fileRow < 0 || fileRow >= m_result.m_fileResults.size()) {
    return 0;
  }

  return m_result.m_fileResults[fileRow].m_lineMatches.size();
}

int SearchResultModel::columnCount(const QModelIndex &p_parent) const {
  Q_UNUSED(p_parent);
  return 1;
}

QVariant SearchResultModel::data(const QModelIndex &p_index, int p_role) const {
  if (!p_index.isValid() || p_index.column() != 0) {
    return QVariant();
  }

  if (p_index.internalId() == 0) {
    int fileRow = p_index.row();
    if (fileRow < 0 || fileRow >= m_result.m_fileResults.size()) {
      return QVariant();
    }

    const auto &fileResult = m_result.m_fileResults[fileRow];
    switch (p_role) {
    case Qt::DisplayRole:
      return QFileInfo(fileResult.m_path).fileName();
    case Qt::ToolTipRole:
      return fileResult.m_path;
    case NodeIdRole: {
      NodeIdentifier nodeId;
      nodeId.notebookId = fileResult.m_notebookId;
      nodeId.relativePath = fileResult.m_path;
      return QVariant::fromValue(nodeId);
    }
    case LineNumberRole:
      return -1;
    case ColumnStartRole:
      return 0;
    case ColumnEndRole:
      return 0;
    case SegmentsRole:
      return QVariant::fromValue(QVector<SearchMatchSegment>());
    case IsFileResultRole:
      return true;
    case AbsolutePathRole:
      return fileResult.m_absolutePath;
    case MatchCountRole:
      return fileResult.m_matchCount;
    default:
      return QVariant();
    }
  }

  int fileRow = static_cast<int>(p_index.internalId()) - 1;
  if (fileRow < 0 || fileRow >= m_result.m_fileResults.size()) {
    return QVariant();
  }

  const auto &fileResult = m_result.m_fileResults[fileRow];
  int lineRow = p_index.row();
  if (lineRow < 0 || lineRow >= fileResult.m_lineMatches.size()) {
    return QVariant();
  }

  const auto &lineMatch = fileResult.m_lineMatches[lineRow];
  switch (p_role) {
  case Qt::DisplayRole:
    return lineMatch.m_lineText;
  case NodeIdRole: {
    NodeIdentifier nodeId;
    nodeId.notebookId = fileResult.m_notebookId;
    nodeId.relativePath = fileResult.m_path;
    return QVariant::fromValue(nodeId);
  }
  case LineNumberRole:
    return lineMatch.m_lineNumber;
  case ColumnStartRole:
    // First segment drives single-range consumers (navigation, united-entry).
    return lineMatch.m_segments.isEmpty() ? 0 : lineMatch.m_segments.first().m_columnStart;
  case ColumnEndRole:
    return lineMatch.m_segments.isEmpty() ? 0 : lineMatch.m_segments.first().m_columnEnd;
  case SegmentsRole:
    return QVariant::fromValue(lineMatch.m_segments);
  case IsFileResultRole:
    return false;
  case AbsolutePathRole:
    return fileResult.m_absolutePath;
  case MatchCountRole:
    return 0;
  default:
    return QVariant();
  }
}

void SearchResultModel::setSearchResult(const SearchResult &p_result) {
  qDebug() << "SearchResultModel::setSearchResult: fileResults:" << p_result.m_fileResults.size()
           << "matchCount:" << p_result.m_matchCount << "truncated:" << p_result.m_truncated;
  beginResetModel();
  m_result = p_result;
  endResetModel();
}

void SearchResultModel::appendSearchResult(const SearchResult &p_result) {
  // Fold aggregate counters in regardless (a zero-row chunk may still carry a truncated flag).
  m_result.m_matchCount += p_result.m_matchCount;
  m_result.m_truncated = m_result.m_truncated || p_result.m_truncated;

  if (p_result.m_fileResults.isEmpty()) {
    return;
  }

  const int first = m_result.m_fileResults.size();
  const int last = first + p_result.m_fileResults.size() - 1;
  qDebug() << "SearchResultModel::appendSearchResult: appending fileResults:"
           << p_result.m_fileResults.size() << "at rows" << first << "-" << last;
  beginInsertRows(QModelIndex(), first, last);
  m_result.m_fileResults.append(p_result.m_fileResults);
  endInsertRows();
}

void SearchResultModel::clear() {
  qDebug() << "SearchResultModel::clear";
  beginResetModel();
  m_result = SearchResult();
  endResetModel();
}

int SearchResultModel::totalMatchCount() const { return m_result.m_matchCount; }

bool SearchResultModel::isTruncated() const { return m_result.m_truncated; }

QVector<SearchFileResult>
SearchResultModel::replacementTargets(const QModelIndexList &p_indexes) const {
  return collectReplacementTargets(p_indexes, false);
}

QVector<SearchFileResult> SearchResultModel::allReplacementTargets() const {
  return collectReplacementTargets({}, true);
}

QVector<SearchFileResult>
SearchResultModel::collectReplacementTargets(const QModelIndexList &p_indexes, bool p_all) const {
  // A line row of -1 denotes its parent file, which subsumes all of that file's children.
  QHash<int, QSet<int>> selectedRows;
  for (const auto &idx : p_indexes) {
    if (!idx.isValid() || idx.model() != this || idx.column() != 0 || idx.row() < 0) {
      continue;
    }
    if (idx.internalId() == 0) {
      if (idx.row() < m_result.m_fileResults.size()) {
        selectedRows[idx.row()].insert(-1);
      }
    } else if (idx.internalId() <= static_cast<quintptr>(m_result.m_fileResults.size())) {
      const int fileRow = static_cast<int>(idx.internalId() - 1);
      if (idx.row() < m_result.m_fileResults[fileRow].m_lineMatches.size()) {
        selectedRows[fileRow].insert(idx.row());
      }
    }
  }
  if (!p_all && selectedRows.isEmpty()) {
    return {};
  }

  struct LineRows {
    int m_row = -1;
    QSet<QPair<int, int>> m_segments;
  };
  struct FileRows {
    int m_row = -1;
    QHash<int, LineRows> m_lines;
  };
  QHash<QPair<QString, QString>, FileRows> fileRows;
  QVector<SearchFileResult> targets;
  for (int fileRow = 0; fileRow < m_result.m_fileResults.size(); ++fileRow) {
    const auto &file = m_result.m_fileResults[fileRow];
    const auto selection = selectedRows.constFind(fileRow);
    if (file.m_type != SearchResultType::File || file.m_lineMatches.isEmpty() ||
        (!p_all && selection == selectedRows.constEnd())) {
      continue;
    }
    const bool wholeFile = p_all || selection.value().contains(-1);
    auto &rows = fileRows[qMakePair(file.m_notebookId, file.m_path)];
    for (int lineRow = 0; lineRow < file.m_lineMatches.size(); ++lineRow) {
      const auto &line = file.m_lineMatches[lineRow];
      if ((!wholeFile && !selection.value().contains(lineRow)) || line.m_segments.isEmpty()) {
        continue;
      }

      if (rows.m_row < 0) {
        rows.m_row = targets.size();
        targets.append(file);
        targets.last().m_lineMatches.clear();
        targets.last().m_matchCount = 0;
      }
      auto &target = targets[rows.m_row];
      // Combining rows must never promote an unsupported snapshot into a replaceable one.
      target.m_replacementSupported = target.m_replacementSupported && file.m_replacementSupported;
      auto &lineRows = rows.m_lines[line.m_lineNumber];
      if (lineRows.m_row < 0) {
        lineRows.m_row = target.m_lineMatches.size();
        target.m_lineMatches.append({line.m_lineNumber, line.m_lineText, {}});
      } else if (target.m_lineMatches[lineRows.m_row].m_lineText != line.m_lineText) {
        // Conflicting snapshots cannot be merged into one trustworthy replacement address.
        return {};
      }
      auto &segments = target.m_lineMatches[lineRows.m_row].m_segments;
      for (const auto &segment : line.m_segments) {
        const auto address = qMakePair(segment.m_columnStart, segment.m_columnEnd);
        if (!lineRows.m_segments.contains(address)) {
          lineRows.m_segments.insert(address);
          segments.append(segment);
          ++target.m_matchCount;
        }
      }
    }
  }
  return targets;
}
