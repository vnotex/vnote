#ifndef VNOTEX_APIOPERATIONS_H
#define VNOTEX_APIOPERATIONS_H

#include "apitypes.h"
#include <QHash>
#include <QMutex>

namespace vnotex {
// Private protocol registry. Active records are never evicted; terminal records hold no note text.
class ApiOperations {
public:
  explicit ApiOperations(QString p_instance);
  QString admit(const QString &p_owner, std::function<bool()> p_authorized,
                std::shared_ptr<std::atomic_bool> p_cancelled, ApiError *p_error);
  bool running(const QString &p_id);
  void complete(const QString &p_id, ApiResult p_result);
  ApiResult lookup(const QString &p_id, const QString &p_owner);
  void stop();

private:
  struct Entry {
    QString id, owner, state;
    std::function<bool()> authorized;
    std::shared_ptr<std::atomic_bool> cancelled;
    QDateTime createdAt, completedAt;
    quint64 completionOrder = 0;
    ApiResult result;
  };
  void prune();
  static QJsonObject json(const Entry &p_entry);
  QString m_instance;
  QMutex m_mutex;
  QHash<QString, Entry> m_entries;
  quint64 m_completionOrder = 0;
  bool m_stopped = false;
};
} // namespace vnotex
#endif
