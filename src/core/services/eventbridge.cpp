#include "eventbridge.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>

using namespace vnotex;

namespace {

const char *c_syncEvents[] = {"sync.started", "sync.finished", "sync.conflict"};

const char *c_fileEvents[] = {"file.created", "file.saved",     "file.deleted",
                              "file.moved",   "folder.created", "folder.deleted"};

} // namespace

EventBridge::EventBridge(VxCoreContextHandle p_context, QObject *p_parent)
    : QObject(p_parent), m_context(p_context) {
  for (const auto *ev : c_syncEvents) {
    vxcore_on_event(m_context, ev, onVxCoreEvent, this);
  }
  for (const auto *ev : c_fileEvents) {
    vxcore_on_event(m_context, ev, onVxCoreEvent, this);
  }
}

EventBridge::~EventBridge() {
  for (const auto *ev : c_syncEvents) {
    vxcore_off_event(m_context, ev, onVxCoreEvent);
  }
  for (const auto *ev : c_fileEvents) {
    vxcore_off_event(m_context, ev, onVxCoreEvent);
  }
}

void EventBridge::onVxCoreEvent(const char *event_name, const char *json_data, void *userdata) {
  auto *self = static_cast<EventBridge *>(userdata);
  const QString evName = QString::fromUtf8(event_name);
  const QJsonObject payload =
      QJsonDocument::fromJson(
          QByteArray::fromRawData(json_data, static_cast<int>(strlen(json_data))))
          .object();
  const QString notebookId = payload.value(QStringLiteral("notebookId")).toString();

  if (evName == QStringLiteral("sync.started")) {
    QMetaObject::invokeMethod(
        self, [self, notebookId]() { emit self->syncStarted(notebookId); }, Qt::QueuedConnection);
  } else if (evName == QStringLiteral("sync.finished")) {
    const auto result = static_cast<VxCoreError>(payload.value(QStringLiteral("result")).toInt());
    QMetaObject::invokeMethod(
        self, [self, notebookId, result]() { emit self->syncFinished(notebookId, result); },
        Qt::QueuedConnection);
  } else if (evName == QStringLiteral("sync.conflict")) {
    QMetaObject::invokeMethod(
        self, [self, notebookId]() { emit self->syncConflict(notebookId); }, Qt::QueuedConnection);
  } else {
    QMetaObject::invokeMethod(
        self, [self, evName, notebookId]() { emit self->fileChanged(evName, notebookId); },
        Qt::QueuedConnection);
  }
}
