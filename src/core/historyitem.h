#ifndef HISTORYITEM_H
#define HISTORYITEM_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace vnotex {
struct HistoryItem {
  HistoryItem() = default;

  HistoryItem(const QString &p_path, int p_lineNumber, const QDateTime &p_lastAccessedTimeUtc);

  QJsonObject toJson() const;

  void fromJson(const QJsonObject &p_jobj);

  // Relative path if it is a node within a notebook.
  QString m_path;

  // 0-based.
  int m_lineNumber = -1;

  QDateTime m_lastAccessedTimeUtc;
};
} // namespace vnotex

#endif // HISTORYITEM_H
