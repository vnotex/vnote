#include "apioperations.h"

#include "apiaudit.h"

#include <QMutexLocker>
#include <QUuid>
#include <algorithm>

using namespace vnotex;

ApiOperations::ApiOperations(QString p_instance) : m_instance(std::move(p_instance)) {}

void ApiOperations::prune() {
  const auto cutoff = QDateTime::currentDateTimeUtc().addSecs(-15 * 60);
  QList<QPair<quint64, QString>> completed;
  for (auto it = m_entries.begin(); it != m_entries.end();) {
    if (it->completedAt.isValid() && it->completedAt < cutoff) {
      it = m_entries.erase(it);
    } else {
      if (it->completedAt.isValid()) {
        completed.append({it->completionOrder, it.key()});
      }
      ++it;
    }
  }
  std::sort(completed.begin(), completed.end());
  for (qsizetype i = 0; i < completed.size() - 256; ++i) {
    m_entries.remove(completed[i].second);
  }
}

QString ApiOperations::admit(const HttpAuditContext &p_audit, std::function<bool()> p_authorized,
                             std::shared_ptr<std::atomic_bool> p_cancelled, ApiError *p_error) {
  QMutexLocker lock(&m_mutex);
  prune();
  if (m_stopped) {
    *p_error = {503, QStringLiteral("stopping"), QStringLiteral("The API is stopping")};
    return {};
  }
  int active = 0;
  int owned = 0;
  for (const auto &entry : m_entries) {
    if (!entry.completedAt.isValid()) {
      ++active;
      owned += entry.audit.principalId == p_audit.principalId;
    }
  }
  if (active >= 16 || owned >= 4) {
    *p_error = {429, QStringLiteral("operation_limit"),
                QStringLiteral("Too many active operations")};
    return {};
  }
  Entry entry;
  entry.id = m_instance + '.' + QUuid::createUuid().toString(QUuid::Id128);
  entry.audit = p_audit;
  entry.elapsed.start();
  entry.state = QStringLiteral("pending");
  entry.authorized = std::move(p_authorized);
  entry.cancelled = std::move(p_cancelled);
  entry.createdAt = QDateTime::currentDateTimeUtc();
  const auto id = entry.id;
  m_entries.insert(id, std::move(entry));
  return id;
}

bool ApiOperations::running(const QString &p_id) {
  QMutexLocker lock(&m_mutex);
  auto it = m_entries.find(p_id);
  if (it == m_entries.end() || it->completedAt.isValid() || m_stopped) {
    return false;
  }
  if (it->cancelled->load() || !it->authorized()) {
    it->cancelled->store(true);
    it->state = QStringLiteral("cancelled");
    it->completedAt = QDateTime::currentDateTimeUtc();
    it->completionOrder = ++m_completionOrder;
    it->result.error = {401, QStringLiteral("authorization_revoked"),
                        QStringLiteral("The request is no longer authorized")};
    it->auditCompleted = true;
    logApiOperation(it->audit, it->id, it->elapsed.elapsed(), it->state, it->result, true);
    prune();
    return false;
  }
  it->state = QStringLiteral("running");
  it->awaitingResult = true;
  return true;
}

void ApiOperations::complete(const QString &p_id, ApiResult p_result) {
  QMutexLocker lock(&m_mutex);
  auto it = m_entries.find(p_id);
  if (it == m_entries.end() || it->auditCompleted) {
    return;
  }
  if (it->completedAt.isValid() || m_stopped) {
    if (it->awaitingResult) {
      it->awaitingResult = false;
      it->auditCompleted = true;
      // Stop can detach domain work and synthesize an uncommitted result. Only a
      // positive commitment is authoritative after that boundary; never imply rollback.
      const auto state =
          p_result.error.status ? QStringLiteral("failed") : QStringLiteral("succeeded");
      logApiOperation(it->audit, it->id, it->elapsed.elapsed(), state, p_result,
                      p_result.committed);
    }
    return;
  }
  it->awaitingResult = false;
  it->auditCompleted = true;
  it->state = p_result.error.status ? QStringLiteral("failed") : QStringLiteral("succeeded");
  if (!p_result.committed && it->cancelled->load()) {
    it->state = QStringLiteral("cancelled");
  }
  it->result = std::move(p_result);
  it->completedAt = QDateTime::currentDateTimeUtc();
  it->completionOrder = ++m_completionOrder;
  logApiOperation(it->audit, it->id, it->elapsed.elapsed(), it->state, it->result, true);
  prune();
}

QJsonObject ApiOperations::json(const Entry &p_entry) {
  QJsonObject result{{QStringLiteral("id"), p_entry.id},
                     {QStringLiteral("state"), p_entry.state},
                     {QStringLiteral("createdAt"), p_entry.createdAt.toString(Qt::ISODateWithMs)},
                     {QStringLiteral("committed"), p_entry.result.committed},
                     {QStringLiteral("saved"), p_entry.result.saved}};
  if (p_entry.completedAt.isValid()) {
    result.insert(QStringLiteral("completedAt"), p_entry.completedAt.toString(Qt::ISODateWithMs));
    result.insert(QStringLiteral("resource"), p_entry.result.data);
    if (p_entry.result.error.status) {
      result.insert(QStringLiteral("error"),
                    QJsonObject{{QStringLiteral("status"), p_entry.result.error.status},
                                {QStringLiteral("code"), p_entry.result.error.code},
                                {QStringLiteral("title"), p_entry.result.error.message}});
    }
  }
  return result;
}

ApiResult ApiOperations::lookup(const QString &p_id, const QString &p_owner) {
  QMutexLocker lock(&m_mutex);
  prune();
  const auto it = m_entries.constFind(p_id);
  if (it == m_entries.cend()) {
    return {{},
            {410, QStringLiteral("operation_gone"),
             QStringLiteral("The operation is from another instance or is no longer retained")}};
  }
  if (it->audit.principalId != p_owner || !it->authorized()) {
    return {{}, {404, QStringLiteral("not_found"), QStringLiteral("Resource not found")}};
  }
  return {json(*it)};
}

void ApiOperations::stop() {
  QMutexLocker lock(&m_mutex);
  m_stopped = true;
  for (auto &entry : m_entries) {
    if (!entry.completedAt.isValid()) {
      entry.cancelled->store(true);
      entry.authorized = [] { return false; };
      entry.state = QStringLiteral("cancelled");
      entry.completedAt = QDateTime::currentDateTimeUtc();
      entry.completionOrder = ++m_completionOrder;
      entry.result.error = {503, QStringLiteral("stopping"), QStringLiteral("The API is stopping")};
      entry.auditCompleted = !entry.awaitingResult;
      logApiOperation(entry.audit, entry.id, entry.elapsed.elapsed(), entry.state, entry.result,
                      !entry.awaitingResult, entry.awaitingResult);
    }
  }
  prune();
}
