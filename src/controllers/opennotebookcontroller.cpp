#include "opennotebookcontroller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QtConcurrentRun>
#include <memory>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <utils/fileutils2.h>
#include <utils/pathutils.h>

#include <sync/sync_json_keys.h>
#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace {

// Helper: extract the leaf notebook name from the just-cloned config.json so
// the cloneFinished signal can carry a human-readable name. Returns empty on
// any error -- callers should treat as advisory only.
QString notebookNameFromConfig(NotebookCoreService *p_svc, const QString &p_notebookId) {
  if (!p_svc || p_notebookId.isEmpty()) {
    return QString();
  }
  const QJsonObject cfg = p_svc->getNotebookConfig(p_notebookId);
  return cfg.value(QLatin1String(vxcore::kJsonKeyName)).toString();
}

bool isOwnedClone(const QString &p_root, const QString &p_marker, const QByteArray &p_owner) {
  const QFileInfo root(p_root);
  const QFileInfo marker(QDir(p_root).filePath(p_marker));
  if (!root.isDir() || root.isSymLink() || marker.isSymLink() ||
      QFileInfo(marker.absolutePath()).isSymLink() ||
      QDir::cleanPath(marker.canonicalFilePath()) !=
          QDir::cleanPath(QDir(root.canonicalFilePath()).filePath(p_marker))) {
    return false;
  }
  QFile file(marker.absoluteFilePath());
  return file.open(QIODevice::ReadOnly) && file.size() == p_owner.size() &&
         file.readAll() == p_owner;
}

bool markOwnedClone(const QString &p_root, const QString &p_marker, const QByteArray &p_owner) {
  const QDir metadata(QDir(p_root).filePath(QStringLiteral("vx_notebook")));
  const auto folder = metadata.filePath(QStringLiteral("vx_transfer"));
  if (QFileInfo(folder).isSymLink() ||
      (!QDir(folder).exists() && !metadata.mkdir(QStringLiteral("vx_transfer"))) ||
      QDir::cleanPath(QFileInfo(folder).canonicalFilePath()) !=
          QDir::cleanPath(QDir(QFileInfo(p_root).canonicalFilePath())
                              .filePath(QStringLiteral("vx_notebook/vx_transfer")))) {
    return false;
  }
  QFile file(QDir(p_root).filePath(p_marker));
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    return false;
  }
  const bool written = file.write(p_owner) == p_owner.size() && file.flush();
  file.close();
  return written && isOwnedClone(p_root, p_marker, p_owner);
}

void removeCloneMarker(const QString &p_root, const QString &p_marker, const QByteArray &p_owner) {
  if (isOwnedClone(p_root, p_marker, p_owner)) {
    QFile::remove(QDir(p_root).filePath(p_marker));
  }
}

// Helper: tear down a notebook root that the controller CREATED (never an
// existing user dir). Mirrors NewNotebookController::bootstrapSync rollback
// (newnotebookcontroller.cpp:266-279) verbatim: 20 x 100ms QDir::removeRecursively
// retries to dodge the Windows libgit2 file-handle race.
void teardownCreatedDir(const QString &p_dir, const QString &p_marker, const QByteArray &p_owner) {
  if (p_dir.isEmpty()) {
    return;
  }
  QDir dir(p_dir);
  if (!dir.exists()) {
    return;
  }
  for (int attempt = 0; attempt < 20 && dir.exists(); ++attempt) {
    if (!isOwnedClone(p_dir, p_marker, p_owner)) {
      qWarning() << "OpenNotebookController: clone ownership changed; preserving destination";
      return;
    }
    if (dir.removeRecursively()) {
      break;
    }
    QThread::msleep(100);
  }
  if (dir.exists()) {
    qWarning() << "OpenNotebookController: failed to remove created dir after retries:" << p_dir;
  }
}

// Helper: convert a free-form vxcore error message into a user-facing
// sentence. Strips trailing newlines so the dialog renders cleanly.
QString trimDiagnostic(QString p_msg) {
  while (p_msg.endsWith(QLatin1Char('\n')) || p_msg.endsWith(QLatin1Char(' '))) {
    p_msg.chop(1);
  }
  return p_msg;
}

} // namespace

OpenNotebookController::OpenNotebookController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  // T22: enable cross-thread marshalling of cloneFinished's struct payload.
  // qRegisterMetaType is idempotent so multiple controller instances are safe.
  qRegisterMetaType<CloneAndOpenResult>("vnotex::CloneAndOpenResult");
  qRegisterMetaType<CloneAndOpenResult>("CloneAndOpenResult");
}

OpenNotebookValidationResult
OpenNotebookController::validateRootFolder(const QString &p_path) const {
  OpenNotebookValidationResult result;
  QString rootFolderPath = p_path.trimmed();

  // Check if path is provided.
  if (rootFolderPath.isEmpty()) {
    result.valid = false;
    result.message = tr("Please specify a folder path.");
    return result;
  }

  // Check if path is legal.
  if (!PathUtils::isLegalPath(rootFolderPath)) {
    result.valid = false;
    result.message = tr("Please specify a valid folder path.");
    return result;
  }

  // Check if path exists and is a directory.
  QFileInfo finfo(rootFolderPath);
  if (!finfo.exists()) {
    result.valid = false;
    result.message = tr("The specified folder does not exist.");
    return result;
  }

  if (!finfo.isDir()) {
    result.valid = false;
    result.message = tr("The specified path is not a folder.");
    return result;
  }

  // Check for duplicate notebook with same root folder via NotebookService.
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (notebookService) {
    QJsonArray notebooks = notebookService->listNotebooks();
    for (const auto &nb : notebooks) {
      QJsonObject nbObj = nb.toObject();
      QString existingPath = nbObj.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
      if (QDir(existingPath) == QDir(rootFolderPath)) {
        QString existingName = nbObj.value(QLatin1String(vxcore::kJsonKeyName)).toString();
        result.valid = false;
        result.message = tr("This notebook (%1) is already open.").arg(existingName);
        return result;
      }
    }
  }

  return result;
}

OpenNotebookResult OpenNotebookController::openNotebook(const OpenNotebookInput &p_input) {
  OpenNotebookResult result;

  // Validate first.
  OpenNotebookValidationResult validation = validateRootFolder(p_input.rootFolderPath);
  if (!validation.valid) {
    result.success = false;
    result.errorMessage = validation.message;
    return result;
  }

  // Get NotebookService.
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    result.success = false;
    result.errorMessage = tr("NotebookService not available.");
    return result;
  }

  // T23: Route through openNotebookEx so the readOnly flag (default false)
  // reaches vxcore. Existing callers that leave readOnly default produce the
  // identical byte sequence ("{}") that openNotebook(path) -> openNotebookEx
  // back-compat shim would emit, so no behavior change for legacy paths.
  const QString optionsJson =
      p_input.readOnly ? QStringLiteral("{\"readOnly\":true}") : QStringLiteral("{}");
  QString notebookId =
      notebookService->openNotebookEx(p_input.rootFolderPath.trimmed(), optionsJson);

  if (notebookId.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("Failed to open notebook from (%1). "
                             "The folder may not be a valid VNote notebook.")
                              .arg(p_input.rootFolderPath);
    return result;
  }

  // Get notebook name from config for display.
  result.notebookName = notebookNameFromConfig(notebookService, notebookId);

  result.success = true;
  result.notebookId = notebookId;
  return result;
}

CloneAndOpenValidationResult
OpenNotebookController::validateCloneInput(const CloneAndOpenInput &p_input) const {
  CloneAndOpenValidationResult result;

  const auto &credentials = p_input.syncSettings.m_credentials;
  const bool suppliedCredentials =
      !credentials.m_username.isEmpty() || !credentials.m_secret.isEmpty();
  const auto message = validateSyncSettings(p_input.syncSettings, suppliedCredentials);
  if (!message.isEmpty()) {
    return {false, message};
  }

  const QString finalDir = p_input.finalDestDir.trimmed();
  if (finalDir.isEmpty()) {
    result.valid = false;
    result.message = tr("Local root folder path must not be empty.");
    return result;
  }
  if (!PathUtils::isLegalPath(finalDir)) {
    result.valid = false;
    result.message = tr("Local root folder path is not valid.");
    return result;
  }

  const QFileInfo finalInfo(finalDir);
  if (finalInfo.exists() || finalInfo.isSymLink()) {
    return {false, tr("Local root folder must not already exist.")};
  }
  const QFileInfo parentInfo(finalInfo.absolutePath());
  if (!parentInfo.exists() || !parentInfo.isDir() || !parentInfo.isWritable()) {
    return {false, tr("Parent folder of destination must exist and be writable.")};
  }

  // Duplicate-open guard against the resolved final dir.
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (notebookService) {
    const QJsonArray notebooks = notebookService->listNotebooks();
    for (const auto &nb : notebooks) {
      const QJsonObject nbObj = nb.toObject();
      const QString existingPath =
          nbObj.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
      if (QDir(existingPath) == QDir(finalDir)) {
        const QString existingName = nbObj.value(QLatin1String(vxcore::kJsonKeyName)).toString();
        result.valid = false;
        result.message =
            tr("A notebook (%1) is already open at this destination.").arg(existingName);
        return result;
      }
    }
  }

  return result;
}

void OpenNotebookController::cloneAndOpen(const CloneAndOpenInput &p_input) {
  if (m_currentCloneToken) {
    CloneAndOpenResult result;
    result.errorMessage = tr("A remote notebook is already being opened.");
    emit cloneFinished(result);
    return;
  }
  // Step 1: pre-validate on the caller thread so dialog dismissal happens
  // synchronously when the user typed something obviously wrong. Returning
  // early via cloneFinished keeps the contract simple: every call emits
  // cloneFinished exactly once.
  const CloneAndOpenValidationResult validation = validateCloneInput(p_input);
  if (!validation.valid) {
    CloneAndOpenResult result;
    result.success = false;
    result.errorMessage = validation.message;
    QMetaObject::invokeMethod(
        this, [this, result]() { emit cloneFinished(result); }, Qt::QueuedConnection);
    return;
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    CloneAndOpenResult result;
    result.success = false;
    result.errorMessage = tr("NotebookService not available.");
    QMetaObject::invokeMethod(
        this, [this, result]() { emit cloneFinished(result); }, Qt::QueuedConnection);
    return;
  }
  // Resolve services before dispatch; anonymous downloads need no vault access.
  const bool needsSync = !p_input.syncSettings.m_credentials.m_secret.isEmpty();
  auto *syncService = needsSync ? m_services.get<SyncService>() : nullptr;
  auto *credStore = needsSync ? m_services.get<SyncCredentialsStore>() : nullptr;
  if (needsSync && (!syncService || !credStore)) {
    CloneAndOpenResult result;
    result.success = false;
    result.errorMessage = tr("Sync services not available; cannot save credentials.");
    QMetaObject::invokeMethod(
        this, [this, result]() { emit cloneFinished(result); }, Qt::QueuedConnection);
    return;
  }

  const QString finalDir = QFileInfo(p_input.finalDestDir.trimmed()).absoluteFilePath();
  const QFileInfo finalInfo(finalDir);
  const QString parentDir = finalInfo.absolutePath();
  const QString leafName = finalInfo.fileName();

  // Allocate exclusively beside the final destination. WebDAV Clone requires a
  // genuinely empty target, so no ownership file is written before the download.
  QTemporaryDir staging(
      QDir(parentDir).filePath(QStringLiteral(".%1.vnote-clone-pending-XXXXXX").arg(leafName)));
  if (!staging.isValid()) {
    CloneAndOpenResult result;
    result.success = false;
    result.errorMessage = tr("Failed to create staging directory: %1").arg(staging.errorString());
    QMetaObject::invokeMethod(
        this, [this, result]() { emit cloneFinished(result); }, Qt::QueuedConnection);
    return;
  }
  const QString stagingDir = staging.path();
  staging.setAutoRemove(false); // Owned by the worker until rename or cleanup.
  const QByteArray cloneOwner = QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();
  const QString cloneMarker =
      QStringLiteral("vx_notebook/vx_transfer/.clone-owner-") + QString::fromLatin1(cloneOwner);

  SyncSettings settings = p_input.syncSettings;
  settings.m_remoteUrl = canonicalSyncRemoteUrl(settings);
  QJsonObject configObj;
  configObj[QLatin1String(vxcore::kJsonKeyBackend)] = settings.m_backend;
  configObj[QLatin1String(vxcore::kJsonKeyRemoteUrl)] = settings.m_remoteUrl;
  configObj[QLatin1String(vxcore::kJsonKeyAutoSyncEnabled)] = p_input.autoSyncEnabled;
  const QString configJson =
      QString::fromUtf8(QJsonDocument(configObj).toJson(QJsonDocument::Compact));
  const QString credentialsJson =
      needsSync ? syncCredentialsJson(settings.m_credentials) : QString();
  const bool autoSyncEnabled = p_input.autoSyncEnabled;

  // openurl-followups Item 2: create the cancellation token on the GUI
  // thread BEFORE spawning the worker. The token outlives the worker
  // (freed below in the GUI-thread tail), so the captured raw pointer the
  // worker uses is guaranteed valid for the entire clone duration.
  m_currentCloneToken = vxcore_sync_create_cancellation();
  VxCoreSyncCancellation *cancellationToken = m_currentCloneToken;

  // Capture pointers + values for the worker; no ServiceLocator access from
  // the worker thread (the resolution above is the last DI access).
  // Progress signal fired immediately so the dialog can show indeterminate
  // feedback.
  emit cloneProgressUpdated(0, 100, tr("Cloning..."));

  // We deliberately discard the returned QFuture: the worker's only
  // observable outputs are the queued-signal emissions, and cancellation is
  // routed through the cancellation token (not the QFuture).
  (void)QtConcurrent::run([this, notebookService, syncService, credStore, stagingDir, finalDir,
                           configJson, credentialsJson, settings, needsSync, autoSyncEnabled,
                           cancellationToken, cloneOwner, cloneMarker]() {
    // emitFinished: bounces back to the GUI thread, frees the cancellation
    // token BEFORE emitting cloneFinished (so any listener calling
    // cancelClone() in response sees nullptr), then emits.
    auto emitFinished = [this](CloneAndOpenResult result) {
      QMetaObject::invokeMethod(
          this,
          [this, result]() {
            if (m_currentCloneToken) {
              vxcore_sync_free_cancellation(m_currentCloneToken);
              m_currentCloneToken = nullptr;
            }
            emit cloneFinished(result);
          },
          Qt::QueuedConnection);
    };

    // Step 3: synchronous clone into the staging dir. This blocks the worker
    // for the full libgit2 fetch + checkout but never touches the UI thread.
    // openurl-followups Item 2: pass the cancellation token so the call can
    // be aborted via cancelClone() on the GUI thread, and capture the
    // underlying VxCoreError so we can distinguish VXCORE_ERR_CANCELLED
    // from generic failures for the user-facing message.
    VxCoreError cloneErr = VXCORE_OK;
    const QString stagingNotebookId = notebookService->cloneNotebookFromUrl(
        stagingDir, configJson, credentialsJson, cancellationToken, &cloneErr);
    if (stagingNotebookId.isEmpty()) {
      // Clone failed: clean up the staging dir; never touch finalDir (it
      // doesn't exist yet). Branch on VXCORE_ERR_CANCELLED for the
      // user-friendly cancellation message; everything else gets the
      // generic "verify URL / PAT / notebook" message.
      QString rmErr;
      FileUtils2::removeStagingDir(stagingDir, &rmErr);
      CloneAndOpenResult result;
      result.success = false;
      if (cloneErr == VXCORE_ERR_CANCELLED) {
        result.errorMessage = tr("Clone cancelled by user.");
      } else {
        result.errorMessage = tr("Failed to clone remote notebook. "
                                 "Verify the URL is reachable, the credentials (if any) are valid, "
                                 "and the remote is an actual VNote notebook.");
      }
      emitFinished(result);
      return;
    }

    // Only completed downloads receive an exclusive ownership marker. Keep it
    // in excluded private storage through reopen/enable so final-root rollback
    // cannot remove a destination whose ownership changed after the rename.
    if (!markOwnedClone(stagingDir, cloneMarker, cloneOwner)) {
      notebookService->closeNotebook(stagingNotebookId);
      FileUtils2::removeStagingDir(stagingDir, nullptr);
      CloneAndOpenResult result;
      result.errorMessage = tr("Could not record ownership of the downloaded notebook.");
      emitFinished(result);
      return;
    }

    // Refuse every foreign destination, including a path created while downloading.
    const QFileInfo destination(finalDir);
    if (destination.exists() || destination.isSymLink() || !QDir().rename(stagingDir, finalDir)) {
      notebookService->closeNotebook(stagingNotebookId);
      FileUtils2::removeStagingDir(stagingDir, nullptr);
      CloneAndOpenResult result;
      result.errorMessage = tr("Failed to move cloned notebook into the destination.");
      emitFinished(result);
      return;
    }

    // Step 5: close + re-open with finalDir so vxcore's NotebookRecord
    // reflects the true root path. This is THE CRITICAL step that the plan
    // calls out: without it, session restore later cannot find the notebook
    // because root_folder still points at the obsolete staging dir.
    notebookService->closeNotebook(stagingNotebookId);
    // Both the with-PAT (-> S5) and no-PAT (-> S2) remote-clone paths open the
    // notebook WRITABLE. The no-PAT path no longer opens read-only; instead it
    // persists partial sync info (S2) below, so the result is a normal, fully
    // editable notebook that simply will not sync until the user supplies a
    // token via the Sync Info dialog (S2 -> S5).
    const QString optionsJson = QStringLiteral("{}");
    const QString finalNotebookId = notebookService->openNotebookEx(finalDir, optionsJson);
    if (finalNotebookId.isEmpty()) {
      // Re-open failed: best-effort cleanup of the just-created finalDir
      // (which we own — user didn't have anything there before us).
      teardownCreatedDir(finalDir, cloneMarker, cloneOwner);
      CloneAndOpenResult result;
      result.success = false;
      result.errorMessage = tr("Cloned notebook could not be re-opened from %1.").arg(finalDir);
      emitFinished(result);
      return;
    }

    const QString notebookName = notebookNameFromConfig(notebookService, finalNotebookId);

    // Anonymous downloads remain writable and silently persist partial S2.
    // Authenticated downloads persist the same routing before registration.
    QJsonObject cfg = notebookService->getNotebookConfig(finalNotebookId);
    cfg[QLatin1String(vxcore::kJsonKeySyncEnabled)] = true;
    cfg[QLatin1String(vxcore::kJsonKeySyncBackend)] = settings.m_backend;
    cfg[QLatin1String(vxcore::kJsonKeySyncRemoteUrl)] = settings.m_remoteUrl;
    cfg[QLatin1String(vxcore::kJsonKeyAutoSyncEnabled)] = autoSyncEnabled;
    if (!notebookService->updateNotebookConfig(
            finalNotebookId,
            QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact)))) {
      notebookService->closeNotebook(finalNotebookId);
      teardownCreatedDir(finalDir, cloneMarker, cloneOwner);
      CloneAndOpenResult failed;
      failed.errorMessage = tr("Failed to save the downloaded notebook's sync settings.");
      emitFinished(failed);
      return;
    }
    if (!needsSync) {

      CloneAndOpenResult result;
      result.success = true;
      result.notebookId = finalNotebookId;
      result.notebookName = notebookName;
      result.isReadOnly = false;
      result.partialSyncMissingCredentials = true;
      removeCloneMarker(finalDir, cloneMarker, cloneOwner);
      emitFinished(result);
      return;
    }

    // PAT path: enable sync. This itself is async on the SyncService side;
    // we bounce back to the GUI thread, install a one-shot listener filtered
    // by notebookId, and emit cloneFinished from the listener so the dialog
    // sees clone + sync registration as a single atomic step. The
    // SyncCredentialsStore::storeCredentials call is fired from inside
    // SyncService::enableSyncForNotebook -- we don't need to duplicate it
    // here.
    QMetaObject::invokeMethod(
        this,
        [this, syncService, credStore, finalNotebookId, finalDir, notebookName, settings,
         emitFinished, cloneMarker, cloneOwner]() {
          auto conn = std::make_shared<QMetaObject::Connection>();
          *conn = connect(
              syncService, &SyncService::enableFinished, this,
              [this, conn, credStore, finalNotebookId, finalDir, notebookName, emitFinished,
               cloneMarker, cloneOwner](const QString &p_resultId, VxCoreError p_result,
                                        const QString &p_message) {
                if (p_resultId != finalNotebookId) {
                  return;
                }
                QObject::disconnect(*conn);
                if (p_result == VXCORE_OK) {
                  CloneAndOpenResult ok;
                  ok.success = true;
                  ok.notebookId = finalNotebookId;
                  ok.notebookName = notebookName;
                  ok.isReadOnly = false;
                  removeCloneMarker(finalDir, cloneMarker, cloneOwner);
                  emitFinished(ok);
                  return;
                }
                // Sync enable failed: full rollback. Delete keychain entry,
                // close notebook in vxcore, delete the on-disk final dir
                // (which we created -- never user pre-existing per
                // validation).
                if (credStore) {
                  credStore->deleteCredentials(finalNotebookId);
                }
                auto *notebookService = m_services.get<NotebookCoreService>();
                if (notebookService) {
                  notebookService->closeNotebook(finalNotebookId);
                }
                teardownCreatedDir(finalDir, cloneMarker, cloneOwner);
                CloneAndOpenResult fail;
                fail.success = false;
                fail.errorMessage = tr("Cloned notebook but failed to enable sync: %1")
                                        .arg(trimDiagnostic(p_message));
                emitFinished(fail);
              },
              Qt::QueuedConnection);
          syncService->enableSyncForNotebook(finalNotebookId, settings);
        },
        Qt::QueuedConnection);
  });
}

void OpenNotebookController::cancelClone() {
  // openurl-followups Item 2: signal cancellation. Lock-free per vxcore docs;
  // safe to call from the GUI thread (or any thread, but the dialog will
  // only call from GUI). No-op when no clone is in flight (token is null).
  //
  // The actual cleanup (free + null) happens on the GUI thread inside the
  // emitFinished lambda BEFORE cloneFinished is emitted (see cloneAndOpen).
  // We deliberately do NOT free the token here — the worker thread may
  // still be reading the cancellation flag through its captured raw
  // pointer; freeing would create a use-after-free race. Just flip the
  // atomic flag and let the worker's cleanup path handle disposal.
  if (m_currentCloneToken) {
    vxcore_sync_cancel(m_currentCloneToken);
  }
}
