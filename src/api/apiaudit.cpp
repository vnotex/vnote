#include "apiaudit.h"

#include <QDebug>
#include <QJsonDocument>
#include <QLoggingCategory>

using namespace vnotex;

namespace {
Q_LOGGING_CATEGORY(lcApiAudit, "vnote.api.audit")

QJsonValue optional(const QString &p_value) {
  return p_value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(p_value);
}

QJsonObject context(const HttpAuditContext &p_context) {
  return {
      {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
      {QStringLiteral("requestId"), optional(p_context.requestId)},
      {QStringLiteral("tokenId"), optional(p_context.principalId)},
      {QStringLiteral("method"), optional(QString::fromLatin1(p_context.method))},
      {QStringLiteral("route"), optional(p_context.routeTemplate)}};
}

void write(QJsonObject p_record) {
  qCInfo(lcApiAudit).noquote().nospace() << QJsonDocument(p_record).toJson(QJsonDocument::Compact);
}
} // namespace

void vnotex::logApiAccess(const HttpAccessRecord &p_record) {
  if (!lcApiAudit().isInfoEnabled()) {
    return;
  }
  if (p_record.rejection != HttpAccessRecord::Rejection::None) {
    const char *reason = "socket_setup_failed";
    if (p_record.rejection == HttpAccessRecord::Rejection::QueueFull) {
      reason = "queue_full";
    } else if (p_record.rejection == HttpAccessRecord::Rejection::Stopping) {
      reason = "stopping";
    }
    write(
        {{QStringLiteral("event"), QStringLiteral("api.connection_rejected")},
         {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
         {QStringLiteral("reason"), QLatin1String(reason)}});
    return;
  }
  auto record = context(p_record.context);
  record.insert(QStringLiteral("event"), QStringLiteral("api.access"));
  record.insert(QStringLiteral("status"),
                p_record.status ? QJsonValue(p_record.status) : QJsonValue(QJsonValue::Null));
  record.insert(QStringLiteral("durationMs"), p_record.durationMs);
  record.insert(QStringLiteral("responseWritten"), p_record.responseWritten);
  write(std::move(record));
}

void vnotex::logApiOperation(const HttpAuditContext &p_context, const QString &p_operationId,
                             qint64 p_durationMs, const QString &p_state, const ApiResult &p_result,
                             bool p_outcomeKnown, bool p_interrupted) {
  if (!lcApiAudit().isInfoEnabled()) {
    return;
  }
  auto record = context(p_context);
  record.insert(QStringLiteral("event"), p_interrupted ? QStringLiteral("api.operation_interrupted")
                                                       : QStringLiteral("api.operation"));
  record.insert(QStringLiteral("operationId"), p_operationId);
  record.insert(QStringLiteral("durationMs"), p_durationMs);
  record.insert(QStringLiteral("state"), p_outcomeKnown ? p_state : QStringLiteral("interrupted"));
  record.insert(QStringLiteral("status"), p_result.error.status ? p_result.error.status : 200);
  record.insert(QStringLiteral("outcomeKnown"), p_outcomeKnown);
  record.insert(QStringLiteral("committed"),
                p_outcomeKnown ? QJsonValue(p_result.committed) : QJsonValue(QJsonValue::Null));
  record.insert(QStringLiteral("saved"),
                p_outcomeKnown ? QJsonValue(p_result.saved) : QJsonValue(QJsonValue::Null));
  write(std::move(record));
}
