#include "notebookcoreservice.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaObject>
#include <QVariantMap>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookiogate.h>
#include <core/services/synclog.h>
#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

NotebookCoreService::NotebookCoreService(VxCoreContextHandle p_context, QObject *p_parent)
    : QObject(p_parent), m_context(p_context) {}

void NotebookCoreService::setHookManager(HookManager *p_hookMgr) { m_hookMgr = p_hookMgr; }

void NotebookCoreService::setNotebookIoGate(NotebookIoGate *p_ioGate) { m_ioGate = p_ioGate; }

NotebookCoreService::~NotebookCoreService() {}

// Notebook operations.
QString NotebookCoreService::createNotebook(const QString &p_path, const QString &p_configJson,
                                            NotebookType p_type) {
  if (!checkContext()) {
    return QString();
  }

  char *notebookId = nullptr;
  VxCoreError err = vxcore_notebook_create(m_context, p_path.toUtf8().constData(),
                                           p_configJson.toUtf8().constData(),
                                           static_cast<VxCoreNotebookType>(p_type), &notebookId);
  if (err != VXCORE_OK) {
    qWarning() << "createNotebook failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(notebookId);
}

QString NotebookCoreService::openNotebook(const QString &p_path) {
  // Back-compat shim: delegate to openNotebookEx with empty options.
  // Mirrors the vxcore_notebook_open -> vxcore_notebook_open_ex relationship
  // established by T14 so the hook-firing logic lives in exactly one place.
  return openNotebookEx(p_path, QStringLiteral("{}"));
}

QString NotebookCoreService::openNotebookEx(const QString &p_path, const QString &p_optionsJson) {
  if (!checkContext()) {
    return QString();
  }

  // vxcore_notebook_open_ex treats nullptr / empty options the same as "{}".
  // Forward the raw bytes when present; pass nullptr when the QString is
  // empty so we don't allocate a stale temporary.
  const QByteArray pathBytes = p_path.toUtf8();
  const QByteArray optsBytes = p_optionsJson.toUtf8();
  const char *optsCstr = p_optionsJson.isEmpty() ? nullptr : optsBytes.constData();

  char *notebookId = nullptr;
  VxCoreError err =
      vxcore_notebook_open_ex(m_context, pathBytes.constData(), optsCstr, &notebookId);
  if (err != VXCORE_OK) {
    qWarning() << "openNotebookEx failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  const QString resolvedId = cstrToQString(notebookId);

  // Fire NotebookAfterOpen hook so other modules (e.g., SyncService) can
  // reconcile per-process runtime state with persisted on-disk config.
  // Matches openNotebook's behavior; the underlying notebook ID is the same
  // regardless of whether the caller went through the legacy shim.
  if (!resolvedId.isEmpty() && m_hookMgr) {
    NotebookOpenEvent event;
    event.notebookId = resolvedId;
    event.rootFolder = p_path;
    const QJsonObject cfg = getNotebookConfig(resolvedId);
    event.notebookName = cfg.value(QLatin1String(vxcore::kJsonKeyName)).toString();
    m_hookMgr->doAction(HookNames::NotebookAfterOpen, event);
  }

  return resolvedId;
}

QString NotebookCoreService::cloneNotebookFromUrl(const QString &p_targetDir,
                                                  const QString &p_configJson,
                                                  const QString &p_credentialsJson,
                                                  VxCoreSyncCancellation *p_cancellationToken,
                                                  VxCoreError *p_outErr) {
  if (p_outErr) {
    *p_outErr = VXCORE_OK;
  }
  if (!checkContext()) {
    if (p_outErr) {
      *p_outErr = VXCORE_ERR_NOT_INITIALIZED;
    }
    return QString();
  }

  // Match vxcore_sync_clone's contract for credentials_json: when empty pass
  // nullptr so the C ABI installs a NoOpCredentialProvider (anonymous clone).
  // PAT contents inside p_credentialsJson are NEVER logged.
  const QByteArray targetBytes = p_targetDir.toUtf8();
  const QByteArray configBytes = p_configJson.toUtf8();
  const QByteArray credsBytes = p_credentialsJson.toUtf8();
  const char *credsCstr = p_credentialsJson.isEmpty() ? nullptr : credsBytes.constData();

  // Route through the cancellable variant unconditionally. When
  // p_cancellationToken is null the C ABI behaves identically to the legacy
  // vxcore_sync_clone (by design — vxcore_sync_clone is now itself a shim
  // on top of vxcore_sync_clone_cancellable).
  char *notebookId = nullptr;
  VxCoreError err =
      vxcore_sync_clone_cancellable(m_context, targetBytes.constData(), configBytes.constData(),
                                    credsCstr, p_cancellationToken, &notebookId);
  if (p_outErr) {
    *p_outErr = err;
  }
  if (err != VXCORE_OK) {
    qWarning() << "cloneNotebookFromUrl failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "targetDir:" << p_targetDir;
    return QString();
  }
  const QString resolvedId = cstrToQString(notebookId);
  if (resolvedId.isEmpty()) {
    return QString();
  }

  // Determine read-only state for the hook payload. The clone path itself
  // does NOT set RO (caller's responsibility per snapshot-only MVP); we
  // query vxcore so the hook reflects the runtime truth even if a future
  // caller flips RO between clone and hook delivery.
  bool readOnly = false;
  const QByteArray idBytes = resolvedId.toUtf8();
  vxcore_notebook_is_read_only(m_context, idBytes.constData(), &readOnly);

  // Fire NotebookAfterClone hook only on success so plugins never receive
  // a false-positive clone notification (per T20 acceptance criteria).
  if (m_hookMgr) {
    NotebookCloneEvent event;
    event.notebookId = resolvedId;
    event.targetDir = p_targetDir;
    event.isReadOnly = readOnly;
    m_hookMgr->doAction(HookNames::NotebookAfterClone, event);
  }

  return resolvedId;
}

bool NotebookCoreService::closeNotebook(const QString &p_notebookId) {
  if (!checkContext()) {
    return false;
  }

  // Fire NotebookBeforeClose hook (cancellable).
  if (m_hookMgr) {
    NotebookCloseEvent event;
    event.notebookId = p_notebookId;
    if (m_hookMgr->doAction(HookNames::NotebookBeforeClose, event)) {
      return false;
    }
  }

  VxCoreError err = vxcore_notebook_close(m_context, p_notebookId.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "closeNotebook failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }

  // Fire NotebookAfterClose hook so other modules can react.
  if (m_hookMgr) {
    NotebookCloseEvent event;
    event.notebookId = p_notebookId;
    m_hookMgr->doAction(HookNames::NotebookAfterClose, event);
  }

  return true;
}

QJsonArray NotebookCoreService::listNotebooks() const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_notebook_list(m_context, &json);
  if (err != VXCORE_OK) {
    qWarning() << "listNotebooks failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonArray();
  }
  return parseJsonArrayFromCStr(json);
}

QJsonObject NotebookCoreService::getNotebookConfig(const QString &p_notebookId) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_notebook_get_config(m_context, p_notebookId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getNotebookConfig failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool NotebookCoreService::updateNotebookConfig(const QString &p_notebookId,
                                               const QString &p_configJson) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_notebook_update_config(m_context, p_notebookId.toUtf8().constData(),
                                                  p_configJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateNotebookConfig failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool NotebookCoreService::rebuildNotebookCache(const QString &p_notebookId) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_notebook_rebuild_cache(m_context, p_notebookId.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "rebuildNotebookCache failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonArray NotebookCoreService::getHistoryResolved(const QString &p_notebookId) const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err =
      vxcore_notebook_history_get_resolved(m_context, p_notebookId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getHistoryResolved failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonArray();
  }
  return parseJsonArrayFromCStr(json);
}

QJsonObject NotebookCoreService::resolvePathToNotebook(const QString &p_absolutePath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *notebookId = nullptr;
  char *relativePath = nullptr;
  VxCoreError err = vxcore_path_resolve(m_context, p_absolutePath.toUtf8().constData(), &notebookId,
                                        &relativePath);
  if (err != VXCORE_OK) {
    // Not finding the path is not necessarily an error - just return empty.
    if (err != VXCORE_ERR_NOT_FOUND) {
      qWarning() << "resolvePathToNotebook failed:" << QString::fromUtf8(vxcore_error_message(err));
    }
    return QJsonObject();
  }

  QJsonObject result;
  result[QLatin1String(vxcore::kJsonKeyNotebookId)] = QString::fromUtf8(notebookId);
  result["relativePath"] = QString::fromUtf8(relativePath);

  vxcore_string_free(notebookId);
  vxcore_string_free(relativePath);

  return result;
}

QString NotebookCoreService::buildAbsolutePath(const QString &p_notebookId,
                                               const QString &p_relativePath) const {
  if (!checkContext()) {
    return QString();
  }

  char *absPath = nullptr;
  VxCoreError err = vxcore_path_build_absolute(m_context, p_notebookId.toUtf8().constData(),
                                               p_relativePath.toUtf8().constData(), &absPath);
  if (err != VXCORE_OK) {
    qWarning() << "buildAbsolutePath failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }

  QString result = QString::fromUtf8(absPath);
  vxcore_string_free(absPath);
  return result;
}

QString NotebookCoreService::getNodePathById(const QString &p_notebookId,
                                             const QString &p_nodeId) const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err = vxcore_node_get_path_by_id(m_context, p_notebookId.toUtf8().constData(),
                                               p_nodeId.toUtf8().constData(), &path);
  if (err != VXCORE_OK) {
    qWarning() << "getNodePathById failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "nodeId:" << p_nodeId;
    return QString();
  }
  return cstrToQString(path);
}

bool NotebookCoreService::unindexNode(const QString &p_notebookId, const QString &p_nodePath) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_unindex(m_context, p_notebookId.toUtf8().constData(),
                                        p_nodePath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "unindexNode failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "nodePath:" << p_nodePath;
    return false;
  }
  return true;
}

QString NotebookCoreService::getRecycleBinPath(const QString &p_notebookId) const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err =
      vxcore_notebook_get_recycle_bin_path(m_context, p_notebookId.toUtf8().constData(), &path);
  if (err != VXCORE_OK) {
    // VXCORE_ERR_UNSUPPORTED is expected for raw notebooks.
    if (err != VXCORE_ERR_UNSUPPORTED) {
      qWarning() << "getRecycleBinPath failed:" << QString::fromUtf8(vxcore_error_message(err));
    }
    return QString();
  }
  return cstrToQString(path);
}

bool NotebookCoreService::emptyRecycleBin(const QString &p_notebookId) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_notebook_empty_recycle_bin(m_context, p_notebookId.toUtf8().constData());
  if (err != VXCORE_OK) {
    // VXCORE_ERR_UNSUPPORTED is expected for raw notebooks.
    if (err != VXCORE_ERR_UNSUPPORTED) {
      qWarning() << "emptyRecycleBin failed:" << QString::fromUtf8(vxcore_error_message(err));
    }
    return false;
  }
  return true;
}

bool NotebookCoreService::isNotebookReadOnly(const QString &p_notebookId) const {
  if (!checkContext() || p_notebookId.isEmpty()) {
    return false;
  }
  bool readOnly = false;
  VxCoreError err =
      vxcore_notebook_is_read_only(m_context, p_notebookId.toUtf8().constData(), &readOnly);
  if (err != VXCORE_OK) {
    // Notebook may not exist (e.g. just-closed or never-opened); a read-only
    // hint that fails to resolve must NOT crash the UI. Log at debug level
    // because this is expected for some lifecycle races.
    qDebug() << "isNotebookReadOnly query failed for" << p_notebookId
             << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return readOnly;
}

// Sync operations - thin wrappers around vxcore C sync APIs.
// Credential JSON contents are intentionally not logged.
VxCoreError NotebookCoreService::enableSync(const QString &p_notebookId,
                                            const QString &p_configJson,
                                            const QString &p_credentialsJson) {
  if (!checkContext()) {
    return VXCORE_ERR_NOT_INITIALIZED;
  }
  const QByteArray nbId = p_notebookId.toUtf8();
  const QByteArray cfg = p_configJson.toUtf8();
  // Wave 7.1 (sync-backend-phase4): unified vxcore_sync_enable. Pass
  // nullptr for credentials when caller has none — the C ABI installs a
  // NoOpCredentialProvider in that case. Otherwise forward the JSON bytes.
  if (p_credentialsJson.isEmpty()) {
    return vxcore_sync_enable(m_context, nbId.constData(), cfg.constData(), nullptr);
  }
  const QByteArray creds = p_credentialsJson.toUtf8();
  return vxcore_sync_enable(m_context, nbId.constData(), cfg.constData(), creds.constData());
}

VxCoreError NotebookCoreService::disableSync(const QString &p_notebookId) {
  if (!checkContext()) {
    return VXCORE_ERR_NOT_INITIALIZED;
  }
  const QByteArray nbId = p_notebookId.toUtf8();
  return vxcore_sync_disable(m_context, nbId.constData());
}

VxCoreError NotebookCoreService::unregisterSyncRuntime(const QString &p_notebookId) {
  if (!checkContext()) {
    return VXCORE_ERR_NOT_INITIALIZED;
  }
  const QByteArray nbId = p_notebookId.toUtf8();
  return vxcore_sync_unregister_notebook(m_context, nbId.constData());
}

VxCoreError NotebookCoreService::triggerSync(const QString &p_notebookId) {
  if (!checkContext()) {
    return VXCORE_ERR_NOT_INITIALIZED;
  }
  const QByteArray nbId = p_notebookId.toUtf8();
  return vxcore_sync_trigger(m_context, nbId.constData());
}

VxCoreError
NotebookCoreService::triggerSyncCancellable(const QString &p_notebookId,
                                            struct VxCoreSyncCancellation_ *p_cancellationHandle) {
  if (!checkContext()) {
    return VXCORE_ERR_NOT_INITIALIZED;
  }
  const QByteArray nbId = p_notebookId.toUtf8();
  return vxcore_sync_trigger_cancellable(
      m_context, nbId.constData(),
      reinterpret_cast<VxCoreSyncCancellation *>(p_cancellationHandle));
}

VxCoreError NotebookCoreService::syncStageOnly(const QString &p_notebookId,
                                               VxCoreSyncCancellation *p_cancellationToken,
                                               bool *p_didCommit) {
  if (!checkContext()) {
    return VXCORE_ERR_NOT_INITIALIZED;
  }
  int didCommitInt = 0;
  const QByteArray nbId = p_notebookId.toUtf8();
  const VxCoreError err =
      vxcore_sync_stage_only(m_context, nbId.constData(), p_cancellationToken, &didCommitInt);
  if (p_didCommit) {
    *p_didCommit = (didCommitInt != 0);
  }
  return err;
}

VxCoreError NotebookCoreService::syncNetworkPhase(const QString &p_notebookId,
                                                  VxCoreSyncCancellation *p_cancellationToken) {
  if (!checkContext()) {
    return VXCORE_ERR_NOT_INITIALIZED;
  }
  const QByteArray nbId = p_notebookId.toUtf8();
  return vxcore_sync_network_phase(m_context, nbId.constData(), p_cancellationToken);
}

VxCoreError NotebookCoreService::getSyncStatus(const QString &p_notebookId,
                                               QString &p_outStatusJson) {
  p_outStatusJson.clear();
  if (!checkContext()) {
    return VXCORE_ERR_NOT_INITIALIZED;
  }
  const QByteArray nbId = p_notebookId.toUtf8();
  char *out = nullptr;
  VxCoreError err = vxcore_sync_get_status(m_context, nbId.constData(), &out);
  if (err == VXCORE_OK && out) {
    p_outStatusJson = QString::fromUtf8(out);
  }
  if (out) {
    vxcore_string_free(out);
  }
  return err;
}

VxCoreError NotebookCoreService::getSyncConflicts(const QString &p_notebookId,
                                                  QString &p_outConflictsJson) {
  p_outConflictsJson.clear();
  if (!checkContext()) {
    return VXCORE_ERR_NOT_INITIALIZED;
  }
  const QByteArray nbId = p_notebookId.toUtf8();
  char *out = nullptr;
  VxCoreError err = vxcore_sync_get_conflicts(m_context, nbId.constData(), &out);
  if (err == VXCORE_OK && out) {
    p_outConflictsJson = QString::fromUtf8(out);
  }
  if (out) {
    vxcore_string_free(out);
  }
  return err;
}

VxCoreError NotebookCoreService::resolveSyncConflict(const QString &p_notebookId,
                                                     const QString &p_filePath,
                                                     const QString &p_resolution) {
  if (!checkContext()) {
    return VXCORE_ERR_NOT_INITIALIZED;
  }
  const QByteArray nbId = p_notebookId.toUtf8();
  const QByteArray path = p_filePath.toUtf8();
  const QByteArray res = p_resolution.toUtf8();
  return vxcore_sync_resolve_conflict(m_context, nbId.constData(), path.constData(),
                                      res.constData());
}

VxCoreError NotebookCoreService::setSyncCredentials(const QString &p_notebookId,
                                                    const QString &p_credentialsJson) {
  if (!checkContext()) {
    return VXCORE_ERR_NOT_INITIALIZED;
  }
  const QByteArray nbId = p_notebookId.toUtf8();
  const QByteArray creds = p_credentialsJson.toUtf8();
  return vxcore_sync_set_credentials(m_context, nbId.constData(), creds.constData());
}

bool NotebookCoreService::isSyncReady(const QString &p_notebookId) const {
  if (!checkContext()) {
    return false;
  }
  int ready = 0;
  VxCoreError err = vxcore_sync_is_ready(m_context, p_notebookId.toUtf8().constData(), &ready);
  const bool result = (err == VXCORE_OK && ready != 0);
  qCDebug(syncCategory) << "NotebookCoreService::isSyncReady: query notebookId:" << p_notebookId
                        << "vxResult:" << static_cast<int>(err) << "ready:" << ready;
  return result;
}

bool NotebookCoreService::isSyncRegistered(const QString &p_notebookId) const {
  if (!checkContext()) {
    return false;
  }
  int registered = 0;
  const QByteArray nbId = p_notebookId.toUtf8();
  const VxCoreError err = vxcore_sync_is_registered(m_context, nbId.constData(), &registered);
  const bool result = (err == VXCORE_OK && registered != 0);
  qCDebug(syncCategory) << "NotebookCoreService::isSyncRegistered: query notebookId:"
                        << p_notebookId << "vxResult:" << static_cast<int>(err)
                        << "registered:" << registered;
  return result;
}

qint64 NotebookCoreService::getLastSyncUtc(const QString &p_notebookId) const {
  if (!checkContext()) {
    return 0;
  }
  int64_t out = 0;
  const QByteArray nbId = p_notebookId.toUtf8();
  VxCoreError err = vxcore_sync_get_last_sync_utc(m_context, nbId.constData(), &out);
  qCDebug(syncCategory) << "NotebookCoreService::getLastSyncUtc: query notebookId:" << p_notebookId
                        << "vxResult:" << static_cast<int>(err)
                        << "utc:" << static_cast<qlonglong>(out);
  if (err != VXCORE_OK) {
    return 0;
  }
  return static_cast<qint64>(out);
}

// Folder operations.
QString NotebookCoreService::createFolder(const QString &p_notebookId, const QString &p_parentPath,
                                          const QString &p_folderName) {
  qCDebug(syncCategory) << "NotebookCoreService::createFolder: entry notebookId:" << p_notebookId
                        << "parentPath:" << p_parentPath << "name:" << p_folderName;
  if (!checkContext()) {
    return QString();
  }

  char *folderId = nullptr;
  VxCoreError err = vxcore_folder_create(m_context, p_notebookId.toUtf8().constData(),
                                         p_parentPath.toUtf8().constData(),
                                         p_folderName.toUtf8().constData(), &folderId);
  qCDebug(syncCategory) << "NotebookCoreService::createFolder: vxResult:" << static_cast<int>(err)
                        << "newFolderId:"
                        << (folderId ? QString::fromUtf8(folderId) : QStringLiteral("(null)"));
  if (err != VXCORE_OK) {
    qWarning() << "createFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(folderId);
}

QString NotebookCoreService::createFolderPath(const QString &p_notebookId,
                                              const QString &p_folderPath) {
  if (!checkContext()) {
    return QString();
  }

  char *folderId = nullptr;
  VxCoreError err = vxcore_folder_create_path(m_context, p_notebookId.toUtf8().constData(),
                                              p_folderPath.toUtf8().constData(), &folderId);
  if (err != VXCORE_OK) {
    qWarning() << "createFolderPath failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(folderId);
}

bool NotebookCoreService::deleteFolder(const QString &p_notebookId, const QString &p_folderPath) {
  if (!checkContext()) {
    return false;
  }

  // Build the operation event once; reused for both before- and after-hooks.
  NodeOperationEvent event;
  event.notebookId = p_notebookId;
  event.relativePath = p_folderPath;
  event.isFolder = true;
  event.name = p_folderPath.mid(p_folderPath.lastIndexOf(QLatin1Char('/')) + 1);
  event.operation = QStringLiteral("delete");

  // Fire NodeBeforeDelete hook (cancellable).
  if (m_hookMgr) {
    if (m_hookMgr->doAction(HookNames::NodeBeforeDelete, event)) {
      return false;
    }
  }

  VxCoreError err = vxcore_node_delete(m_context, p_notebookId.toUtf8().constData(),
                                       p_folderPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "deleteFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }

  // Fire NodeAfterDelete hook so subscribers (e.g. ViewAreaController) can
  // close any open view windows referencing the just-deleted folder.
  if (m_hookMgr) {
    m_hookMgr->doAction(HookNames::NodeAfterDelete, event);
  }
  return true;
}

QJsonObject NotebookCoreService::getFolderConfig(const QString &p_notebookId,
                                                 const QString &p_folderPath,
                                                 VxCoreError *p_outErr) const {
  if (p_outErr) {
    *p_outErr = VXCORE_OK;
  }
  if (!checkContext()) {
    if (p_outErr) {
      *p_outErr = VXCORE_ERR_NOT_INITIALIZED;
    }
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_node_get_config(m_context, p_notebookId.toUtf8().constData(),
                                           p_folderPath.toUtf8().constData(), &json);
  if (p_outErr) {
    *p_outErr = err;
  }
  if (err != VXCORE_OK) {
    qWarning() << "getFolderConfig failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "folderPath:" << p_folderPath;
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool NotebookCoreService::updateFolderMetadata(const QString &p_notebookId,
                                               const QString &p_folderPath,
                                               const QString &p_metadataJson) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_update_metadata(m_context, p_notebookId.toUtf8().constData(),
                                                p_folderPath.toUtf8().constData(),
                                                p_metadataJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateFolderMetadata failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonObject NotebookCoreService::getFolderMetadata(const QString &p_notebookId,
                                                   const QString &p_folderPath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_node_get_metadata(m_context, p_notebookId.toUtf8().constData(),
                                             p_folderPath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getFolderMetadata failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool NotebookCoreService::renameFolder(const QString &p_notebookId, const QString &p_folderPath,
                                       const QString &p_newName) {
  if (!checkContext()) {
    return false;
  }

  // Fire NodeBeforeRename hook (cancellable).
  if (m_hookMgr) {
    NodeRenameEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_folderPath;
    event.isFolder = true;
    event.oldName = p_folderPath.mid(p_folderPath.lastIndexOf(QLatin1Char('/')) + 1);
    event.newName = p_newName;
    if (m_hookMgr->doAction(HookNames::NodeBeforeRename, event)) {
      return false;
    }
  }

  VxCoreError err =
      vxcore_node_rename(m_context, p_notebookId.toUtf8().constData(),
                         p_folderPath.toUtf8().constData(), p_newName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "renameFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }

  // Fire NodeAfterRename hook so other modules can react.
  if (m_hookMgr) {
    NodeRenameEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_folderPath;
    event.isFolder = true;
    event.oldName = p_folderPath.mid(p_folderPath.lastIndexOf(QLatin1Char('/')) + 1);
    event.newName = p_newName;
    m_hookMgr->doAction(HookNames::NodeAfterRename, event);
  }

  return true;
}

bool NotebookCoreService::moveFolder(const QString &p_notebookId, const QString &p_srcPath,
                                     const QString &p_destParentPath) {
  if (!checkContext()) {
    return false;
  }

  // Fire NodeBeforeMove hook (cancellable).
  if (m_hookMgr) {
    NodeOperationEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_srcPath;
    event.isFolder = true;
    event.name = p_srcPath.mid(p_srcPath.lastIndexOf(QLatin1Char('/')) + 1);
    event.operation = QStringLiteral("move");
    if (m_hookMgr->doAction(HookNames::NodeBeforeMove, event)) {
      return false;
    }
  }

  VxCoreError err =
      vxcore_node_move(m_context, p_notebookId.toUtf8().constData(), p_srcPath.toUtf8().constData(),
                       p_destParentPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "moveFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }

  // Fire NodeAfterMove hook so subscribers (e.g. ViewAreaController) can
  // refresh open view windows to point at the new path.
  if (m_hookMgr) {
    const QString basename = p_srcPath.mid(p_srcPath.lastIndexOf(QLatin1Char('/')) + 1);
    QString newPath;
    if (p_destParentPath.isEmpty() || p_destParentPath == QStringLiteral(".")) {
      newPath = basename;
    } else {
      newPath = p_destParentPath + QLatin1Char('/') + basename;
    }
    NodeMoveEvent moveEvent;
    moveEvent.notebookId = p_notebookId;
    moveEvent.oldRelativePath = p_srcPath;
    moveEvent.newRelativePath = newPath;
    moveEvent.isFolder = true;
    m_hookMgr->doAction(HookNames::NodeAfterMove, moveEvent);
  }
  return true;
}

QString NotebookCoreService::copyFolder(const QString &p_notebookId, const QString &p_srcPath,
                                        const QString &p_destParentPath, const QString &p_newName) {
  if (!checkContext()) {
    return QString();
  }

  char *folderId = nullptr;
  VxCoreError err =
      vxcore_node_copy(m_context, p_notebookId.toUtf8().constData(), p_srcPath.toUtf8().constData(),
                       p_destParentPath.isEmpty() ? "." : p_destParentPath.toUtf8().constData(),
                       p_newName.toUtf8().constData(), &folderId);
  if (err != VXCORE_OK) {
    qWarning() << "copyFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(folderId);
}

QJsonObject NotebookCoreService::listFolderChildren(const QString &p_notebookId,
                                                    const QString &p_folderPath,
                                                    VxCoreError *p_outErr) const {
  if (p_outErr) {
    *p_outErr = VXCORE_OK;
  }
  if (!checkContext()) {
    if (p_outErr) {
      *p_outErr = VXCORE_ERR_NOT_INITIALIZED;
    }
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_folder_list_children(
      m_context, p_notebookId.toUtf8().constData(),
      p_folderPath.isEmpty() ? "." : p_folderPath.toUtf8().constData(), &json);
  if (p_outErr) {
    *p_outErr = err;
  }
  if (err != VXCORE_OK) {
    qWarning() << "listFolderChildren failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  // The per-child transient "exists" field (vxcore::kJsonKeyNodeExists) emitted
  // by vxcore for bundled notebooks rides along in this JSON untouched.
  return parseJsonObjectFromCStr(json);
}

// T6 (notebook-explorer-drag-reorder): async reorder with hooks + IoGate.
//
// Threading layout:
//   Caller thread (typically GUI):
//     1. Pre-validate args.
//     2. Snapshot current order via listFolderChildren.
//     3. No-op short-circuit (no hooks, no worker).
//     4. Fire before_reorder synchronously (gives plugins a cancel slot
//        BEFORE any disk work is queued).
//     5. Dispatch to QtConcurrent worker; return.
//   Worker thread:
//     6. Hold NotebookIoGate::ScopedLock(notebookId) for the duration of
//        the vxcore_folder_set_children_order call ONLY.
//     7. Release the gate by leaving the inner { } scope (RAII).
//     8. Queue completion (after-hook + reorderCompleted signal) onto the
//        service's owning thread via QMetaObject::invokeMethod /
//        Qt::QueuedConnection. Releasing the gate BEFORE queuing the
//        after-hook is what makes it safe for plugins to re-enter the gate
//        from inside the after-hook callback (e.g., to stage a follow-up
//        write) without deadlocking.
void NotebookCoreService::reorderFolderChildren(const QString &p_notebookId,
                                                const QString &p_folderRelPath,
                                                const QStringList &p_orderedFolders,
                                                const QStringList &p_orderedFiles) {
  if (p_notebookId.isEmpty() || (p_orderedFolders.isEmpty() && p_orderedFiles.isEmpty()) ||
      !m_hookMgr || !m_ioGate) {
    emit reorderCompleted(p_notebookId, p_folderRelPath, false, tr("Invalid arguments"));
    return;
  }

  // Snapshot current order so the event payload carries the "previous" state
  // and the no-op check has something to compare against.
  const QJsonObject children = listFolderChildren(p_notebookId, p_folderRelPath);
  QStringList previousFolderOrder;
  for (const QJsonValue &v : children.value(QStringLiteral("folders")).toArray()) {
    previousFolderOrder.append(v.toObject().value(QStringLiteral("name")).toString());
  }
  QStringList previousFileOrder;
  for (const QJsonValue &v : children.value(QStringLiteral("files")).toArray()) {
    previousFileOrder.append(v.toObject().value(QStringLiteral("name")).toString());
  }

  // No-op short-circuit: an absent sub-array means "do not reorder this kind";
  // a sub-array equal to the current order means "no effective change". When
  // BOTH kinds satisfy one of those, neither vxcore nor the hooks need to fire.
  const bool foldersUnchanged =
      p_orderedFolders.isEmpty() || p_orderedFolders == previousFolderOrder;
  const bool filesUnchanged = p_orderedFiles.isEmpty() || p_orderedFiles == previousFileOrder;
  if (foldersUnchanged && filesUnchanged) {
    emit reorderCompleted(p_notebookId, p_folderRelPath, true, QString());
    return;
  }

  NodeReorderEvent event;
  event.notebookId = p_notebookId;
  event.folderRelPath = p_folderRelPath;
  event.previousFolderOrder = previousFolderOrder;
  event.previousFileOrder = previousFileOrder;
  event.newFolderOrder = p_orderedFolders;
  event.newFileOrder = p_orderedFiles;

  // Synchronous before-hook lets plugins cancel BEFORE any worker dispatch.
  // The typed doAction overload returns true when a callback called cancel().
  if (m_hookMgr->doAction(HookNames::NodeBeforeReorder, event)) {
    emit reorderCompleted(p_notebookId, p_folderRelPath, false, tr("Cancelled by hook"));
    return;
  }

  // Capture by value so the worker has stable copies (Qt strings are CoW so
  // this is cheap). Capturing `this` is safe — the service outlives all in-
  // flight reorders by construction (it is destroyed only at shutdown after
  // QThreadPool::waitForDone via ~QtConcurrent::run cleanup).
  const QString notebookId = p_notebookId;
  const QString folderRelPath = p_folderRelPath;
  const QStringList orderedFolders = p_orderedFolders;
  const QStringList orderedFiles = p_orderedFiles;

  (void)QtConcurrent::run([this, notebookId, folderRelPath, orderedFolders, orderedFiles, event]() {
    int rc = VXCORE_ERR_UNKNOWN;
    {
      // Acquire-release window: hold the gate for the working-tree write ONLY.
      // Mirrors the SyncOps::triggerSync stage-phase pattern.
      NotebookIoGate::ScopedLock lock(*m_ioGate, notebookId);
      const std::string orderedJson = buildOrderedJson(orderedFolders, orderedFiles);
      // THREADING: vxcore's FolderManager is NOT thread-safe — its folder-config
      // cache is shared with main-thread reads (NotebookNodeModel::fetchMore ->
      // listFolderChildren). Run the actual vxcore folder write on the MAIN
      // thread so it is serialized with all other main-thread vxcore folder
      // access; otherwise a concurrent main-thread cache reload can free the
      // cached FolderConfig under the worker, yielding the current=0
      // PERMUTATION_MISMATCH that made this reorder spuriously fail. The
      // NotebookIoGate stays held HERE on the worker (async acquisition that
      // never blocks the UI thread) to keep serializing the working-tree write
      // against save/sync workers; BlockingQueuedConnection keeps the gate held
      // across the write without releasing it early.
      QMetaObject::invokeMethod(
          this,
          [this, &rc, &notebookId, &folderRelPath, &orderedJson]() {
            rc = static_cast<int>(vxcore_folder_set_children_order(
                m_context, notebookId.toUtf8().constData(),
                folderRelPath.isEmpty() ? "." : folderRelPath.toUtf8().constData(),
                orderedJson.c_str()));
          },
          Qt::BlockingQueuedConnection);
    } // Gate released here BEFORE queuing the after-hook.

    QMetaObject::invokeMethod(
        this,
        [this, notebookId, folderRelPath, event, rc]() {
          if (rc == VXCORE_OK) {
            if (m_hookMgr) {
              m_hookMgr->doAction(HookNames::NodeAfterReorder, event);
            }
            emit reorderCompleted(notebookId, folderRelPath, true, QString());
          } else {
            emit reorderCompleted(notebookId, folderRelPath, false, reorderErrorToString(rc));
          }
        },
        Qt::QueuedConnection);
  });
}

std::string NotebookCoreService::buildOrderedJson(const QStringList &p_orderedFolders,
                                                  const QStringList &p_orderedFiles) {
  // Either sub-array is OPTIONAL; vxcore treats a missing key as "do not touch
  // this kind" (per vxcore_folder_set_children_order contract).
  nlohmann::json j = nlohmann::json::object();
  if (!p_orderedFolders.isEmpty()) {
    nlohmann::json arr = nlohmann::json::array();
    for (const QString &name : p_orderedFolders) {
      arr.push_back(name.toStdString());
    }
    j["folders"] = arr;
  }
  if (!p_orderedFiles.isEmpty()) {
    nlohmann::json arr = nlohmann::json::array();
    for (const QString &name : p_orderedFiles) {
      arr.push_back(name.toStdString());
    }
    j["files"] = arr;
  }
  return j.dump();
}

QString NotebookCoreService::reorderErrorToString(int p_rc) {
  switch (p_rc) {
  case VXCORE_OK:
    return tr("OK");
  case VXCORE_ERR_INVALID_PARAM:
    return tr("Invalid argument");
  case VXCORE_ERR_NOT_FOUND:
    return tr("Folder not found");
  case VXCORE_ERR_INVALID_STATE:
    return tr("Invalid state");
  case VXCORE_ERR_PERMUTATION_MISMATCH:
    return tr("Submitted order is not a valid permutation of the folder's children");
  case VXCORE_ERR_NOT_IMPLEMENTED:
    return tr("Reorder is not supported for this notebook type");
  case VXCORE_ERR_UNKNOWN:
    return tr("Unknown error while reordering");
  default:
    return tr("vxcore error %1 while reordering").arg(p_rc);
  }
}

QJsonObject NotebookCoreService::listFolderExternal(const QString &p_notebookId,
                                                    const QString &p_folderPath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_folder_list_external(
      m_context, p_notebookId.toUtf8().constData(),
      p_folderPath.isEmpty() ? "." : p_folderPath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "listFolderExternal failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool NotebookCoreService::indexNode(const QString &p_notebookId, const QString &p_nodePath) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_index(m_context, p_notebookId.toUtf8().constData(),
                                      p_nodePath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "indexNode failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "nodePath:" << p_nodePath;
    return false;
  }
  return true;
}

QString NotebookCoreService::getAvailableName(const QString &p_notebookId,
                                              const QString &p_folderPath,
                                              const QString &p_desiredName) const {
  if (!checkContext()) {
    return QString();
  }

  char *availableName = nullptr;
  VxCoreError err = vxcore_folder_get_available_name(
      m_context, p_notebookId.toUtf8().constData(),
      p_folderPath.isEmpty() ? "." : p_folderPath.toUtf8().constData(),
      p_desiredName.toUtf8().constData(), &availableName);
  if (err != VXCORE_OK) {
    qWarning() << "getAvailableName failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(availableName);
}

// File operations.
QString NotebookCoreService::createFile(const QString &p_notebookId, const QString &p_folderPath,
                                        const QString &p_fileName) {
  if (!checkContext()) {
    return QString();
  }

  char *fileId = nullptr;
  VxCoreError err = vxcore_file_create(m_context, p_notebookId.toUtf8().constData(),
                                       p_folderPath.toUtf8().constData(),
                                       p_fileName.toUtf8().constData(), &fileId);
  if (err != VXCORE_OK) {
    qWarning() << "createFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(fileId);
}

bool NotebookCoreService::deleteFile(const QString &p_notebookId, const QString &p_filePath) {
  if (!checkContext()) {
    return false;
  }

  // Build the operation event once; reused for both before- and after-hooks.
  NodeOperationEvent event;
  event.notebookId = p_notebookId;
  event.relativePath = p_filePath;
  event.isFolder = false;
  event.name = p_filePath.mid(p_filePath.lastIndexOf(QLatin1Char('/')) + 1);
  event.operation = QStringLiteral("delete");

  // Fire NodeBeforeDelete hook (cancellable).
  if (m_hookMgr) {
    if (m_hookMgr->doAction(HookNames::NodeBeforeDelete, event)) {
      return false;
    }
  }

  VxCoreError err = vxcore_node_delete(m_context, p_notebookId.toUtf8().constData(),
                                       p_filePath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "deleteFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }

  // Fire NodeAfterDelete hook so subscribers (e.g. ViewAreaController) can
  // close any open view windows referencing the just-deleted file.
  if (m_hookMgr) {
    m_hookMgr->doAction(HookNames::NodeAfterDelete, event);
  }
  return true;
}

QJsonObject NotebookCoreService::getFileInfo(const QString &p_notebookId, const QString &p_filePath,
                                             VxCoreError *p_outErr) const {
  if (p_outErr) {
    *p_outErr = VXCORE_OK;
  }
  if (!checkContext()) {
    if (p_outErr) {
      *p_outErr = VXCORE_ERR_NOT_INITIALIZED;
    }
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_node_get_config(m_context, p_notebookId.toUtf8().constData(),
                                           p_filePath.toUtf8().constData(), &json);
  if (p_outErr) {
    *p_outErr = err;
  }
  if (err != VXCORE_OK) {
    qWarning() << "getFileInfo failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

QJsonObject NotebookCoreService::getFileMetadata(const QString &p_notebookId,
                                                 const QString &p_filePath) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_node_get_metadata(m_context, p_notebookId.toUtf8().constData(),
                                             p_filePath.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getFileMetadata failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

bool NotebookCoreService::updateFileMetadata(const QString &p_notebookId, const QString &p_filePath,
                                             const QString &p_metadataJson) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_node_update_metadata(m_context, p_notebookId.toUtf8().constData(),
                                                p_filePath.toUtf8().constData(),
                                                p_metadataJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateFileMetadata failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool NotebookCoreService::renameFile(const QString &p_notebookId, const QString &p_filePath,
                                     const QString &p_newName) {
  if (!checkContext()) {
    return false;
  }

  // Fire NodeBeforeRename hook (cancellable).
  if (m_hookMgr) {
    NodeRenameEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_filePath;
    event.isFolder = false;
    event.oldName = p_filePath.mid(p_filePath.lastIndexOf(QLatin1Char('/')) + 1);
    event.newName = p_newName;
    if (m_hookMgr->doAction(HookNames::NodeBeforeRename, event)) {
      return false;
    }
  }

  VxCoreError err =
      vxcore_node_rename(m_context, p_notebookId.toUtf8().constData(),
                         p_filePath.toUtf8().constData(), p_newName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "renameFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }

  // Fire NodeAfterRename hook so other modules can react.
  if (m_hookMgr) {
    NodeRenameEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_filePath;
    event.isFolder = false;
    event.oldName = p_filePath.mid(p_filePath.lastIndexOf(QLatin1Char('/')) + 1);
    event.newName = p_newName;
    m_hookMgr->doAction(HookNames::NodeAfterRename, event);
  }

  return true;
}

bool NotebookCoreService::moveFile(const QString &p_notebookId, const QString &p_srcFilePath,
                                   const QString &p_destFolderPath) {
  if (!checkContext()) {
    return false;
  }

  // Fire NodeBeforeMove hook (cancellable).
  if (m_hookMgr) {
    NodeOperationEvent event;
    event.notebookId = p_notebookId;
    event.relativePath = p_srcFilePath;
    event.isFolder = false;
    event.name = p_srcFilePath.mid(p_srcFilePath.lastIndexOf(QLatin1Char('/')) + 1);
    event.operation = QStringLiteral("move");
    if (m_hookMgr->doAction(HookNames::NodeBeforeMove, event)) {
      return false;
    }
  }

  VxCoreError err =
      vxcore_node_move(m_context, p_notebookId.toUtf8().constData(),
                       p_srcFilePath.toUtf8().constData(), p_destFolderPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "moveFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }

  // Fire NodeAfterMove hook so subscribers (e.g. ViewAreaController) can
  // refresh open view windows to point at the new path.
  if (m_hookMgr) {
    const QString basename = p_srcFilePath.mid(p_srcFilePath.lastIndexOf(QLatin1Char('/')) + 1);
    QString newPath;
    if (p_destFolderPath.isEmpty() || p_destFolderPath == QStringLiteral(".")) {
      newPath = basename;
    } else {
      newPath = p_destFolderPath + QLatin1Char('/') + basename;
    }
    NodeMoveEvent moveEvent;
    moveEvent.notebookId = p_notebookId;
    moveEvent.oldRelativePath = p_srcFilePath;
    moveEvent.newRelativePath = newPath;
    moveEvent.isFolder = false;
    m_hookMgr->doAction(HookNames::NodeAfterMove, moveEvent);
  }
  return true;
}

QString NotebookCoreService::copyFile(const QString &p_notebookId, const QString &p_srcFilePath,
                                      const QString &p_destFolderPath, const QString &p_newName) {
  if (!checkContext()) {
    return QString();
  }

  char *fileId = nullptr;
  VxCoreError err = vxcore_node_copy(
      m_context, p_notebookId.toUtf8().constData(), p_srcFilePath.toUtf8().constData(),
      p_destFolderPath.isEmpty() ? "." : p_destFolderPath.toUtf8().constData(),
      p_newName.toUtf8().constData(), &fileId);
  if (err != VXCORE_OK) {
    qWarning() << "copyFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(fileId);
}

QString NotebookCoreService::importFile(const QString &p_notebookId, const QString &p_folderPath,
                                        const QString &p_externalFilePath) {
  if (!checkContext()) {
    return QString();
  }

  char *fileId = nullptr;
  VxCoreError err = vxcore_file_import(m_context, p_notebookId.toUtf8().constData(),
                                       p_folderPath.toUtf8().constData(),
                                       p_externalFilePath.toUtf8().constData(), &fileId);
  if (err != VXCORE_OK) {
    qWarning() << "importFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(fileId);
}
QString NotebookCoreService::importFolder(const QString &p_notebookId,
                                          const QString &p_destFolderPath,
                                          const QString &p_externalFolderPath,
                                          const QString &p_suffixAllowlist) {
  if (!checkContext()) {
    return QString();
  }

  char *folderId = nullptr;
  VxCoreError err = vxcore_folder_import(
      m_context, p_notebookId.toUtf8().constData(), p_destFolderPath.toUtf8().constData(),
      p_externalFolderPath.toUtf8().constData(),
      p_suffixAllowlist.isEmpty() ? nullptr : p_suffixAllowlist.toUtf8().constData(), &folderId);
  if (err != VXCORE_OK) {
    qWarning() << "importFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(folderId);
}

bool NotebookCoreService::createTag(const QString &p_notebookId, const QString &p_tagName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_tag_create(m_context, p_notebookId.toUtf8().constData(),
                                      p_tagName.toUtf8().constData());
  if (err != VXCORE_OK && err != VXCORE_ERR_ALREADY_EXISTS) {
    qWarning() << "createTag failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "tagName:" << p_tagName;
    return false;
  }
  return true;
}

bool NotebookCoreService::updateFileTags(const QString &p_notebookId, const QString &p_filePath,
                                         const QStringList &p_tags) {
  if (!checkContext()) {
    return false;
  }

  QJsonArray tagsArray;
  for (const auto &tag : p_tags) {
    tagsArray.append(tag);
  }
  QString tagsJson = QString::fromUtf8(QJsonDocument(tagsArray).toJson(QJsonDocument::Compact));

  VxCoreError err =
      vxcore_file_update_tags(m_context, p_notebookId.toUtf8().constData(),
                              p_filePath.toUtf8().constData(), tagsJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateFileTags failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "filePath:" << p_filePath;
    return false;
  }
  return true;
}

bool NotebookCoreService::updateNodeTimestamps(const QString &p_notebookId,
                                               const QString &p_nodePath, qint64 p_createdUtc,
                                               qint64 p_modifiedUtc) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err =
      vxcore_node_update_timestamps(m_context, p_notebookId.toUtf8().constData(),
                                    p_nodePath.toUtf8().constData(), p_createdUtc, p_modifiedUtc);
  if (err != VXCORE_OK) {
    qWarning() << "updateNodeTimestamps failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "nodePath:" << p_nodePath;
    return false;
  }
  return true;
}

bool NotebookCoreService::updateFileAttachments(const QString &p_notebookId,
                                                const QString &p_filePath,
                                                const QStringList &p_attachments) {
  if (!checkContext()) {
    return false;
  }

  QJsonArray attachArray;
  for (const auto &att : p_attachments) {
    attachArray.append(att);
  }
  QString attachJson = QString::fromUtf8(QJsonDocument(attachArray).toJson(QJsonDocument::Compact));

  VxCoreError err = vxcore_file_update_attachments(m_context, p_notebookId.toUtf8().constData(),
                                                   p_filePath.toUtf8().constData(),
                                                   attachJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateFileAttachments failed:" << QString::fromUtf8(vxcore_error_message(err))
               << "notebookId:" << p_notebookId << "filePath:" << p_filePath;
    return false;
  }
  return true;
}

QString NotebookCoreService::peekFile(const QString &p_notebookId, const QString &p_filePath,
                                      int p_maxChars) const {
  if (!checkContext()) {
    return QString();
  }

  char *content = nullptr;
  VxCoreError err =
      vxcore_file_peek(m_context, p_notebookId.toUtf8().constData(),
                       p_filePath.toUtf8().constData(), static_cast<size_t>(p_maxChars), &content);
  if (err != VXCORE_OK) {
    // Not found is expected for some files, don't warn
    if (err != VXCORE_ERR_NOT_FOUND) {
      qWarning() << "peekFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    }
    return QString();
  }
  return cstrToQString(content);
}

QString NotebookCoreService::getAttachmentsFolder(const QString &p_notebookId,
                                                  const QString &p_filePath) const {
  if (!checkContext()) {
    return QString();
  }

  char *outPath = nullptr;
  VxCoreError err = vxcore_node_get_attachments_folder(m_context, p_notebookId.toUtf8().constData(),
                                                       p_filePath.toUtf8().constData(), &outPath);
  if (err != VXCORE_OK) {
    qWarning() << "getAttachmentsFolder failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QString();
  }
  return cstrToQString(outPath);
}

QJsonArray NotebookCoreService::listAttachments(const QString &p_notebookId,
                                                const QString &p_filePath) const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *outJson = nullptr;
  VxCoreError err = vxcore_node_list_attachments(m_context, p_notebookId.toUtf8().constData(),
                                                 p_filePath.toUtf8().constData(), &outJson);
  if (err != VXCORE_OK) {
    qWarning() << "listAttachments failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonArray();
  }
  return parseJsonArrayFromCStr(outJson);
}

// Private methods.
bool NotebookCoreService::checkContext() const {
  if (!m_context) {
    qWarning() << "NotebookCoreService: VxCore context not initialized";
    return false;
  }
  return true;
}

// Private helper methods.
QString NotebookCoreService::cstrToQString(char *p_str) {
  if (!p_str) {
    return QString();
  }
  QString result = QString::fromUtf8(p_str);
  vxcore_string_free(p_str);
  return result;
}

const char *NotebookCoreService::qstringToCStr(const QString &p_str) {
  if (p_str.isEmpty()) {
    return nullptr;
  }
  // Note: Returns pointer to temporary data. Only safe for immediate use.
  return p_str.toUtf8().constData();
}

QJsonObject NotebookCoreService::parseJsonObjectFromCStr(char *p_str) {
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

QJsonArray NotebookCoreService::parseJsonArrayFromCStr(char *p_str) {
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
