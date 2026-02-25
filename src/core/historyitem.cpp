#include "historyitem.h"

#include <utils/utils.h>

using namespace vnotex;

HistoryItem::HistoryItem(const QString &p_path, int p_lineNumber,
                         const QDateTime &p_lastAccessedTimeUtc)
    : m_path(p_path), m_lineNumber(p_lineNumber), m_lastAccessedTimeUtc(p_lastAccessedTimeUtc) {}

QJsonObject HistoryItem::toJson() const {
  QJsonObject jobj;
  jobj[QStringLiteral("path")] = m_path;
  jobj[QStringLiteral("lineNumber")] = m_lineNumber;
  jobj[QStringLiteral("lastAccessedTime")] = Utils::dateTimeStringUniform(m_lastAccessedTimeUtc);
  return jobj;
}

void HistoryItem::fromJson(const QJsonObject &p_jobj) {
  m_path = p_jobj[QStringLiteral("path")].toString();
  m_lineNumber = p_jobj[QStringLiteral("lineNumber")].toInt();
  m_lastAccessedTimeUtc =
      Utils::dateTimeFromStringUniform(p_jobj[QStringLiteral("lastAccessedTime")].toString());
}
