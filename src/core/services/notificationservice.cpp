#include "notificationservice.h"

using namespace vnotex;

NotificationService::NotificationService(QObject *p_parent) : QObject(p_parent) {
  // Ensure NotificationMessage can cross thread boundaries via queued signal
  // delivery, in case a future producer calls notify() off the GUI thread.
  qRegisterMetaType<NotificationMessage>("NotificationMessage");
}

void NotificationService::eraseDedupIndexIfOwned(const NotificationMessage &p_msg) {
  if (p_msg.m_dedupKey.isEmpty()) {
    return;
  }
  // Conditional by design -- see the header for why an unconditional remove()
  // would strip a live generation's entry.
  if (m_dedupIndex.value(p_msg.m_dedupKey) == p_msg.m_id) {
    m_dedupIndex.remove(p_msg.m_dedupKey);
  }
}

void NotificationService::evictOneExistingMessage() {
  if (m_messages.isEmpty()) {
    return;
  }

  // m_messages is append-ordered, so the first match in each pass is the oldest.
  int victim = -1;

  for (int i = 0; i < m_messages.size(); ++i) {
    if (m_messages.at(i).m_dismissed) {
      victim = i;
      break;
    }
  }

  if (victim < 0) {
    for (int i = 0; i < m_messages.size(); ++i) {
      const auto &msg = m_messages.at(i);
      if (msg.m_actions.isEmpty() && msg.m_duration != NotificationMessage::Duration::Persist) {
        victim = i;
        break;
      }
    }
  }

  if (victim < 0) {
    // Everything left is an actionable Persist message. Something has to go.
    victim = 0;
  }

  const quint64 id = m_messages.at(victim).m_id;
  eraseDedupIndexIfOwned(m_messages.at(victim));
  m_messages.remove(victim);

  // NOT messageDismissed: the message is gone, not dismissed.
  emit messageRemoved(id);
}

quint64 NotificationService::notify(NotificationMessage p_msg) {
  if (!p_msg.m_dedupKey.isEmpty()) {
    const quint64 existingId = m_dedupIndex.value(p_msg.m_dedupKey, 0);
    if (existingId != 0) {
      for (auto &msg : m_messages) {
        if (msg.m_id != existingId || msg.m_dismissed) {
          continue;
        }

        // Same incident: fold the new content into the live message. Identity,
        // lifecycle and key ownership stay with the stored message.
        NotificationMessage updated = p_msg;
        updated.m_id = msg.m_id;
        updated.m_timestamp = msg.m_timestamp;
        updated.m_dismissed = msg.m_dismissed;
        updated.m_category = msg.m_category;
        updated.m_dedupKey = msg.m_dedupKey;

        msg = updated;
        emit messageUpdated(msg);
        return msg.m_id;
      }

      // Index pointed at something that is no longer active: drop the stale
      // entry (ownership-checked, per the invariant) and fall through to post a
      // fresh message.
      if (m_dedupIndex.value(p_msg.m_dedupKey) == existingId) {
        m_dedupIndex.remove(p_msg.m_dedupKey);
      }
    }
  }

  // Make room BEFORE appending, and never let the incoming message be the
  // victim -- otherwise messageAdded could name a message that is not stored.
  // The condition is >=, not >: at exactly c_maxMessages we are not "over cap"
  // yet appending would put us one past it.
  while (m_messages.size() >= c_maxMessages) {
    const int before = m_messages.size();
    evictOneExistingMessage();
    if (m_messages.size() >= before) {
      break; // Defensive: never spin if nothing can be evicted.
    }
  }

  p_msg.m_id = m_nextId++;
  p_msg.m_timestamp = QDateTime::currentDateTime();
  p_msg.m_dismissed = false;

  m_messages.append(p_msg);

  if (!p_msg.m_dedupKey.isEmpty()) {
    m_dedupIndex.insert(p_msg.m_dedupKey, p_msg.m_id);
  }

  emit messageAdded(p_msg);

  return p_msg.m_id;
}

quint64 NotificationService::renotify(NotificationMessage p_msg) {
  if (!p_msg.m_dedupKey.isEmpty()) {
    const quint64 existingId = m_dedupIndex.value(p_msg.m_dedupKey, 0);
    if (existingId != 0) {
      for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages.at(i).m_id != existingId) {
          continue;
        }
        // Erase ownership BEFORE emitting, then never touch the index again in
        // this function: messageRemoved is delivered synchronously, so a
        // receiver may call notify() for this same key and become the new
        // owner before control returns here. An unconditional remove() after
        // the emit would strip that new generation's entry, and the next
        // notify() would append a duplicate active message.
        eraseDedupIndexIfOwned(m_messages.at(i));
        m_messages.remove(i);
        emit messageRemoved(existingId);
        break;
      }
    }
  }

  return notify(p_msg);
}

bool NotificationService::update(quint64 p_id, const NotificationMessage &p_msg) {
  for (auto &msg : m_messages) {
    if (msg.m_id != p_id) {
      continue;
    }

    // Identity and lifecycle stay with the stored message: update() must never
    // renumber, re-stamp, resurrect a dismissed notification, or move a dedup
    // key to a different message.
    NotificationMessage updated = p_msg;
    updated.m_id = msg.m_id;
    updated.m_timestamp = msg.m_timestamp;
    updated.m_dismissed = msg.m_dismissed;
    updated.m_category = msg.m_category;
    updated.m_dedupKey = msg.m_dedupKey;

    msg = updated;
    emit messageUpdated(msg);
    return true;
  }
  return false;
}

bool NotificationService::isActive(quint64 p_id) const {
  for (const auto &msg : m_messages) {
    if (msg.m_id == p_id) {
      return !msg.m_dismissed;
    }
  }
  return false;
}

void NotificationService::dismiss(quint64 p_id) {
  for (auto &msg : m_messages) {
    if (msg.m_id == p_id) {
      if (msg.m_dismissed) {
        return;
      }
      msg.m_dismissed = true;
      eraseDedupIndexIfOwned(msg);
      emit messageDismissed(p_id);
      return;
    }
  }
}

bool NotificationService::dismissByDedupKey(const QString &p_key) {
  if (p_key.isEmpty()) {
    return false;
  }

  const quint64 id = m_dedupIndex.value(p_key, 0);
  if (id == 0) {
    return false;
  }

  const bool wasActive = isActive(id);
  dismiss(id);
  return wasActive;
}

void NotificationService::clearAll() {
  if (m_messages.isEmpty()) {
    m_dedupIndex.clear();
    return;
  }
  m_messages.clear();
  // Everything is gone, so no per-message ownership check is meaningful here.
  m_dedupIndex.clear();
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
