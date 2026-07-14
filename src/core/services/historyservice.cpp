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
      info.modifiedTimeUtc =
          QDateTime::fromMSecsSinceEpoch(
              static_cast<qint64>(entry.value(QStringLiteral("openedUtc")).toDouble()));

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
