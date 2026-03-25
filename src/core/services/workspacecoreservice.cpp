#include "workspacecoreservice.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonParseError>

#include <core/hooknames.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

WorkspaceCoreService::WorkspaceCoreService(VxCoreContextHandle p_context, QObject *p_parent)
    : QObject(p_parent), m_context(p_context) {
}

WorkspaceCoreService::~WorkspaceCoreService() {
}

void WorkspaceCoreService::setHookManager(HookManager *p_hookMgr) {
  m_hookMgr = p_hookMgr;
}

// Workspace lifecycle.
QString WorkspaceCoreService::createWorkspace(const QString &p_name) {
  if (!checkContext()) {
    return QString();
  }

  char *workspaceId = nullptr;
  VxCoreError err = vxcore_workspace_create(m_context, p_name.toUtf8().constData(), &workspaceId);
  if (err != VXCORE_OK) {
    qWarning() << "createWorkspace failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(workspaceId);
}

bool WorkspaceCoreService::deleteWorkspace(const QString &p_id) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_workspace_delete(m_context, p_id.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "deleteWorkspace failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonObject WorkspaceCoreService::getWorkspace(const QString &p_id) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_workspace_get(m_context, p_id.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getWorkspace failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

QJsonArray WorkspaceCoreService::listWorkspaces() const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_workspace_list(m_context, &json);
  if (err != VXCORE_OK) {
    qWarning() << "listWorkspaces failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonArray();
  }
  return parseJsonArrayFromCStr(json);
}

bool WorkspaceCoreService::renameWorkspace(const QString &p_id, const QString &p_name) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_workspace_rename(m_context, p_id.toUtf8().constData(), p_name.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "renameWorkspace failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

// Current workspace.
QString WorkspaceCoreService::getCurrentWorkspace() const {
  if (!checkContext()) {
    return QString();
  }

  char *workspaceId = nullptr;
  VxCoreError err = vxcore_workspace_get_current(m_context, &workspaceId);
  if (err != VXCORE_OK) {
    qWarning() << "getCurrentWorkspace failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(workspaceId);
}

bool WorkspaceCoreService::setCurrentWorkspace(const QString &p_id) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_workspace_set_current(m_context, p_id.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "setCurrentWorkspace failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

// Workspace buffer management.
bool WorkspaceCoreService::addBuffer(const QString &p_workspaceId, const QString &p_bufferId) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_workspace_add_buffer(m_context, p_workspaceId.toUtf8().constData(),
                                                p_bufferId.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "addBuffer failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool WorkspaceCoreService::removeBuffer(const QString &p_workspaceId, const QString &p_bufferId) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_workspace_remove_buffer(m_context, p_workspaceId.toUtf8().constData(),
                                                   p_bufferId.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "removeBuffer failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool WorkspaceCoreService::setCurrentBuffer(const QString &p_workspaceId, const QString &p_bufferId) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_workspace_set_current_buffer(m_context, p_workspaceId.toUtf8().constData(),
                                                        p_bufferId.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "setCurrentBuffer failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool WorkspaceCoreService::setBufferOrder(const QString &p_workspaceId,
                                          const QStringList &p_bufferIds) {
  if (!checkContext()) {
    return false;
  }

  QJsonArray arr;
  for (const auto &id : p_bufferIds) {
    arr.append(id);
  }
  QByteArray json = QJsonDocument(arr).toJson(QJsonDocument::Compact);

  VxCoreError err = vxcore_workspace_set_buffer_order(
      m_context, p_workspaceId.toUtf8().constData(), json.constData());
  if (err != VXCORE_OK) {
    qWarning() << "setBufferOrder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool WorkspaceCoreService::setWorkspaceMetadata(const QString &p_workspaceId,
                                                const QJsonObject &p_metadata) {
  if (!checkContext()) {
    return false;
  }

  QByteArray json = QJsonDocument(p_metadata).toJson(QJsonDocument::Compact);

  VxCoreError err = vxcore_workspace_set_metadata(
      m_context, p_workspaceId.toUtf8().constData(), json.constData());
  if (err != VXCORE_OK) {
    qWarning() << "setWorkspaceMetadata failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonObject WorkspaceCoreService::getBufferMetadata(const QString &p_workspaceId,
                                                     const QString &p_bufferId) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_workspace_get_buffer_metadata(
      m_context, p_workspaceId.toUtf8().constData(),
      p_bufferId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getBufferMetadata failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool WorkspaceCoreService::setBufferMetadata(const QString &p_workspaceId,
                                              const QString &p_bufferId,
                                              const QJsonObject &p_metadata) {
  if (!checkContext()) {
    return false;
  }

  QByteArray json = QJsonDocument(p_metadata).toJson(QJsonDocument::Compact);

  VxCoreError err = vxcore_workspace_set_buffer_metadata(
      m_context, p_workspaceId.toUtf8().constData(),
      p_bufferId.toUtf8().constData(), json.constData());
  if (err != VXCORE_OK) {
    qWarning() << "setBufferMetadata failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

// Hook-firing methods for ViewArea events.
bool WorkspaceCoreService::fireViewWindowBeforeOpen(const ViewWindowOpenEvent &p_event) {
  return m_hookMgr && m_hookMgr->doAction(HookNames::ViewWindowBeforeOpen, p_event);
}

void WorkspaceCoreService::fireViewWindowAfterOpen(const ViewWindowOpenEvent &p_event) {
  if (m_hookMgr) {
    m_hookMgr->doAction(HookNames::ViewWindowAfterOpen, p_event);
  }
}

bool WorkspaceCoreService::fireViewWindowBeforeClose(const ViewWindowCloseEvent &p_event) {
  return m_hookMgr && m_hookMgr->doAction(HookNames::ViewWindowBeforeClose, p_event);
}

void WorkspaceCoreService::fireViewWindowAfterClose(const ViewWindowCloseEvent &p_event) {
  if (m_hookMgr) {
    m_hookMgr->doAction(HookNames::ViewWindowAfterClose, p_event);
  }
}

bool WorkspaceCoreService::fireViewWindowBeforeMove(const ViewWindowMoveEvent &p_event) {
  return m_hookMgr && m_hookMgr->doAction(HookNames::ViewWindowBeforeMove, p_event);
}

void WorkspaceCoreService::fireViewWindowAfterMove(const ViewWindowMoveEvent &p_event) {
  if (m_hookMgr) {
    m_hookMgr->doAction(HookNames::ViewWindowAfterMove, p_event);
  }
}

bool WorkspaceCoreService::fireViewSplitBeforeCreate(const ViewSplitCreateEvent &p_event) {
  return m_hookMgr && m_hookMgr->doAction(HookNames::ViewSplitBeforeCreate, p_event);
}

void WorkspaceCoreService::fireViewSplitAfterCreate(const ViewSplitCreateEvent &p_event) {
  if (m_hookMgr) {
    m_hookMgr->doAction(HookNames::ViewSplitAfterCreate, p_event);
  }
}

bool WorkspaceCoreService::fireViewSplitBeforeRemove(const ViewSplitRemoveEvent &p_event) {
  return m_hookMgr && m_hookMgr->doAction(HookNames::ViewSplitBeforeRemove, p_event);
}

void WorkspaceCoreService::fireViewSplitAfterRemove(const ViewSplitRemoveEvent &p_event) {
  if (m_hookMgr) {
    m_hookMgr->doAction(HookNames::ViewSplitAfterRemove, p_event);
  }
}

bool WorkspaceCoreService::fireViewSplitBeforeActivate(const ViewSplitActivateEvent &p_event) {
  return m_hookMgr && m_hookMgr->doAction(HookNames::ViewSplitBeforeActivate, p_event);
}

void WorkspaceCoreService::fireViewSplitAfterActivate(const ViewSplitActivateEvent &p_event) {
  if (m_hookMgr) {
    m_hookMgr->doAction(HookNames::ViewSplitAfterActivate, p_event);
  }
}

// Private methods.
bool WorkspaceCoreService::checkContext() const {
  if (!m_context) {
    qWarning() << "WorkspaceCoreService: VxCore context not initialized";
    return false;
  }
  return true;
}

QString WorkspaceCoreService::cstrToQString(char *p_str) {
  if (!p_str) {
    return QString();
  }
  QString result = QString::fromUtf8(p_str);
  vxcore_string_free(p_str);
  return result;
}

QJsonObject WorkspaceCoreService::parseJsonObjectFromCStr(char *p_str) {
  if (!p_str) {
    return QJsonObject();
  }
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(QByteArray(p_str), &error);
  vxcore_string_free(p_str);
  if (error.error != QJsonParseError::NoError) {
    qWarning() << "Failed to parse JSON object:" << error.errorString();
    return QJsonObject();
  }
  return doc.object();
}

QJsonArray WorkspaceCoreService::parseJsonArrayFromCStr(char *p_str) {
  if (!p_str) {
    return QJsonArray();
  }
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(QByteArray(p_str), &error);
  vxcore_string_free(p_str);
  if (error.error != QJsonParseError::NoError) {
    qWarning() << "Failed to parse JSON array:" << error.errorString();
    return QJsonArray();
  }
  return doc.array();
}
