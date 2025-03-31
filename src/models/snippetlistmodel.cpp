#include "snippetlistmodel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>

#include <core/services/snippetcoreservice.h>

using namespace vnotex;

SnippetListModel::SnippetListModel(SnippetCoreService *p_snippetService, QObject *p_parent)
    : QAbstractListModel(p_parent), m_snippetService(p_snippetService) {
}

int SnippetListModel::rowCount(const QModelIndex &p_parent) const {
  if (p_parent.isValid()) {
    return 0;
  }

  return m_displaySnippets.size();
}

QVariant SnippetListModel::data(const QModelIndex &p_index, int p_role) const {
  if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_displaySnippets.size()) {
    return QVariant();
  }

  const SnippetInfo &info = m_displaySnippets[p_index.row()];

  switch (p_role) {
  case Qt::DisplayRole:
    if (info.shortcut >= 0) {
      return QStringLiteral("[%1] %2")
          .arg(info.shortcut, 2, 10, QLatin1Char('0'))
          .arg(info.name);
    }
    return info.name;

  case NameRole:
    return info.name;

  case DescriptionRole:
    return info.description;

  case TypeRole:
    return info.type;

  case IsBuiltinRole:
    return info.isBuiltin;

  case ShortcutRole:
    return QVariant::fromValue(info.shortcut);

  case Qt::ToolTipRole:
    return info.description;

  default:
    return QVariant();
  }
}

void SnippetListModel::refresh() {
  m_allSnippets.clear();

  if (!m_snippetService) {
    applyFilter();
    return;
  }

  const QJsonArray snippets = m_snippetService->listSnippets();
  m_allSnippets.reserve(snippets.size());

  for (const auto &snippetVal : snippets) {
    const QJsonObject obj = snippetVal.toObject();

    SnippetInfo info;
    info.name = obj.value(QStringLiteral("name")).toString();
    info.description = obj.value(QStringLiteral("description")).toString();
    info.type = obj.value(QStringLiteral("type")).toString();
    info.isBuiltin = obj.value(QStringLiteral("isBuiltin")).toBool();
    info.shortcut = m_shortcuts.value(info.name, -1);

    m_allSnippets.append(info);
  }

  applyFilter();
}

void SnippetListModel::setShowBuiltIn(bool p_show) {
  if (m_showBuiltIn == p_show) {
    return;
  }

  m_showBuiltIn = p_show;
  applyFilter();
}

QString SnippetListModel::snippetAt(int p_row) const {
  if (p_row < 0 || p_row >= m_displaySnippets.size()) {
    return QString();
  }

  return m_displaySnippets[p_row].name;
}

void SnippetListModel::setShortcuts(const QMap<QString, int> &p_shortcuts) {
  m_shortcuts = p_shortcuts;

  for (auto &snippet : m_allSnippets) {
    snippet.shortcut = m_shortcuts.value(snippet.name, -1);
  }

  applyFilter();
}

void SnippetListModel::applyFilter() {
  beginResetModel();

  m_displaySnippets.clear();
  m_displaySnippets.reserve(m_allSnippets.size());

  for (const auto &snippet : m_allSnippets) {
    if (!m_showBuiltIn && snippet.isBuiltin) {
      continue;
    }

    m_displaySnippets.append(snippet);
  }

  endResetModel();
}
