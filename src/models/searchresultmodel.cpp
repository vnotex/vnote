#include "searchresultmodel.h"

#include <QFileInfo>

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
    case IsFileResultRole:
      return true;
    case AbsolutePathRole:
      return fileResult.m_absolutePath;
    case MatchCountRole:
      return fileResult.m_lineMatches.size();
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
    return QStringLiteral("  %1: %2").arg(lineMatch.m_lineNumber + 1).arg(lineMatch.m_lineText);
  case NodeIdRole: {
    NodeIdentifier nodeId;
    nodeId.notebookId = fileResult.m_notebookId;
    nodeId.relativePath = fileResult.m_path;
    return QVariant::fromValue(nodeId);
  }
  case LineNumberRole:
    return lineMatch.m_lineNumber;
  case ColumnStartRole:
    return lineMatch.m_columnStart;
  case ColumnEndRole:
    return lineMatch.m_columnEnd;
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
  beginResetModel();
  m_result = p_result;
  endResetModel();
}

void SearchResultModel::clear() {
  beginResetModel();
  m_result = SearchResult();
  endResetModel();
}

int SearchResultModel::totalMatchCount() const { return m_result.m_matchCount; }

bool SearchResultModel::isTruncated() const { return m_result.m_truncated; }
