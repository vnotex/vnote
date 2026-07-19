#include "notificationservice.h"

using namespace vnotex;

NotificationService::NotificationService(QObject *p_parent) : QObject(p_parent) {
  // Ensure NotificationMessage can cross thread boundaries via queued signal
  // delivery, in case a future producer calls notify() off the GUI thread.
  qRegisterMetaType<NotificationMessage>("NotificationMessage");
}

quint64 NotificationService::notify(NotificationMessage p_msg) {
  p_msg.m_id = m_nextId++;
  p_msg.m_timestamp = QDateTime::currentDateTime();
  p_msg.m_dismissed = false;

  m_messages.append(p_msg);

  emit messageAdded(p_msg);

  return p_msg.m_id;
}

void NotificationService::dismiss(quint64 p_id) {
  for (auto &msg : m_messages) {
    if (msg.m_id == p_id) {
      if (msg.m_dismissed) {
        return;
      }
      msg.m_dismissed = true;
      emit messageDismissed(p_id);
      return;
    }
  }
}

void NotificationService::clearAll() {
  if (m_messages.isEmpty()) {
    return;
  }
  m_messages.clear();
  emit messagesCleared();
}

const QVector<NotificationMessage> &NotificationService::messages() const { return m_messages; }

int NotificationService::activeCount() const {
  int count = 0;
  for (const auto &msg : m_messages) {
    if (!msg.m_dismissed) {
      ++count;
    }
  }
  return count;
}
