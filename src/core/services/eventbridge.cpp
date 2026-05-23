#include "eventbridge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QStringList>

using namespace vnotex;

namespace {

const char *c_syncEvents[] = {"sync.started", "sync.finished", "sync.conflict", "sync.should_run"};

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

  qInfo() << "EventBridge::onVxCoreEvent: received event=" << evName << "notebookId=" << notebookId;

  qInfo() << "EventBridge::onVxCoreEvent: received event=" << evName
          << " notebookId=" << notebookId;

  if (evName == QStringLiteral("sync.started")) {
    QMetaObject::invokeMethod(
        self, [self, notebookId]() { emit self->syncStarted(notebookId); }, Qt::QueuedConnection);
  } else if (evName == QStringLiteral("sync.finished")) {
    const auto result = static_cast<VxCoreError>(payload.value(QStringLiteral("result")).toInt());
    QMetaObject::invokeMethod(
        self, [self, notebookId, result]() { emit self->syncFinished(notebookId, result); },
        Qt::QueuedConnection);
  } else if (evName == QStringLiteral("sync.conflict")) {
    QStringList files;
    const QJsonValue filesVal = payload.value(QStringLiteral("files"));
    if (filesVal.isArray()) {
      const QJsonArray arr = filesVal.toArray();
      files.reserve(arr.size());
      for (const QJsonValue &v : arr) {
        if (v.isString()) {
          files.append(v.toString());
        }
      }
    }
    QMetaObject::invokeMethod(
        self, [self, notebookId]() { emit self->syncConflict(notebookId); }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(
        self, [self, notebookId, files]() { emit self->syncConflictFiles(notebookId, files); },
        Qt::QueuedConnection);
  } else if (evName == QStringLiteral("sync.should_run")) {
    qInfo() << "EventBridge: routing syncShouldRun for" << notebookId;
    QMetaObject::invokeMethod(
        self, [self, notebookId]() { emit self->syncShouldRun(notebookId); }, Qt::QueuedConnection);
  } else {
    QMetaObject::invokeMethod(
        self, [self, evName, notebookId]() { emit self->fileChanged(evName, notebookId); },
        Qt::QueuedConnection);
  }
}
