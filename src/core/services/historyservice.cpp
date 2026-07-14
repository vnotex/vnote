#include "historyservice.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>

#include <core/services/notebookcoreservice.h>

using namespace vnotex;

HistoryService::HistoryService(NotebookCoreService *p_notebookService, QObject *p_parent)
    : QObject(p_parent), m_notebookService(p_notebookService) {
}

QVector<NodeInfo> HistoryService::getAllHistory() const {
  return buildAllHistory();
}

QVector<NodeInfo> HistoryService::getRecentHistory(int p_limit) const {
  QVector<NodeInfo> nodes = buildAllHistory();
  if (p_limit > 0 && nodes.size() > p_limit) {
    nodes.resize(p_limit);
  }
  return nodes;
}

QVector<NodeInfo> HistoryService::getHistoryForDate(const QDate &p_date, int p_limit) const {
  QVector<NodeInfo> filtered;
  if (!p_date.isValid()) {
    return filtered;
  }

  const QVector<NodeInfo> all = buildAllHistory();
  for (const auto &info : all) {
    // Calendar dates are local; compare against the local-time day.
    if (info.modifiedTimeUtc.toLocalTime().date() == p_date) {
      filtered.append(info);
      if (p_limit > 0 && filtered.size() >= p_limit) {
        break;
      }
    }
  }
  return filtered;
}

QVector<NodeInfo> HistoryService::buildAllHistory() const {
  QVector<NodeInfo> nodes;

  if (!m_notebookService) {
    return nodes;
  }

  const QJsonArray notebooks = m_notebookService->listNotebooks();
  for (const auto &nbVal : notebooks) {
    const QJsonObject nbObj = nbVal.toObject();
    const QString notebookId = nbObj.value(QStringLiteral("id")).toString();
    if (notebookId.isEmpty()) {
      continue;
    }

    const QJsonArray history = m_notebookService->getHistoryResolved(notebookId);
    for (const auto &entryVal : history) {
      const QJsonObject entry = entryVal.toObject();

      NodeInfo info;
      info.id.notebookId = notebookId;
      info.id.relativePath = entry.value(QStringLiteral("relativePath")).toString();
      info.name = entry.value(QStringLiteral("name")).toString();
      info.isFolder = false;
      info.isExternal = false;
      // NodeInfo::modifiedTimeUtc is documented UTC; pass Qt::UTC explicitly so
      // downstream local-time conversions (e.g. getHistoryForDate) are correct.
      info.modifiedTimeUtc =
          QDateTime::fromMSecsSinceEpoch(
              static_cast<qint64>(entry.value(QStringLiteral("openedUtc")).toDouble()),
              Qt::UTC);

      nodes.append(info);
    }
  }

  // Sort by openedUtc descending (most recent first).
  std::sort(nodes.begin(), nodes.end(),
            [](const NodeInfo &a, const NodeInfo &b) {
              return a.modifiedTimeUtc > b.modifiedTimeUtc;
            });

  return nodes;
}

QString HistoryService::previewFor(const NodeIdentifier &p_id) const {
  if (!m_notebookService) {
    return QString();
  }
  return m_notebookService->peekFile(p_id.notebookId, p_id.relativePath);
}
