#include "opennotebookcontroller.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRegularExpression>
#include <QString>
#include <QThread>
#include <QtConcurrentRun>
#include <memory>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <utils/fileutils2.h>
#include <utils/pathutils.h>

#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace {

// MUST match OpenNotebookDialog2::kRemoteUrlSchemeRegex character-for-character
// (per T24 learnings.md, the dialog is the single source of truth, but the
// controller's validator MUST agree so the dialog and a direct controller
// caller produce identical accept/reject outcomes for the same input).
const char *const kRemoteUrlSchemeRegex = "^(https://|file:///)\\S+$";

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

// Helper: tear down a notebook root that the controller CREATED (never an
// existing user dir). Mirrors NewNotebookController::bootstrapSync rollback
// (newnotebookcontroller.cpp:266-279) verbatim: 20 x 100ms QDir::removeRecursively
// retries to dodge the Windows libgit2 file-handle race.
void teardownCreatedDir(const QString &p_dir) {
  if (p_dir.isEmpty()) {
    return;
  }
  QDir dir(p_dir);
  if (!dir.exists()) {
    return;
  }
  for (int attempt = 0; attempt < 20 && dir.exists(); ++attempt) {
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

  const QString url = p_input.remoteUrl.trimmed();
  if (url.isEmpty()) {
    result.valid = false;
    result.message = tr("Remote URL must not be empty.");
    return result;
  }

  // URL-scheme guard — identical regex to OpenNotebookDialog2 per T24
  // contract.
  static const QRegularExpression scheme(QString::fromLatin1(kRemoteUrlSchemeRegex));
  if (!scheme.match(url).hasMatch()) {
    result.valid = false;
    result.message = tr("Remote URL must use HTTPS or file:// scheme (got: %1).").arg(url);
    return result;
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

  // Dest contract (post refine-open-notebook-dialog): the local root folder
  // may EITHER not exist (we create it via the staging rename) OR be an
  // existing empty directory. cloneAndOpen's worker thread converts the
  // existing-empty case back to the "does not exist" precondition via a
  // single pre-rename rmdir hop. Anything else (file, non-empty dir,
  // unwritable parent for the non-existing case) is rejected.
  const QFileInfo finalInfo(finalDir);
  if (finalInfo.exists()) {
    if (!finalInfo.isDir()) {
      result.valid = false;
      result.message = tr("Local root folder must be a directory.");
      return result;
    }
    const QDir destDir(finalDir);
    const QStringList entries =
        destDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    if (!entries.isEmpty()) {
      result.valid = false;
      result.message =
          tr("Local root folder must be empty (contains %1 item(s)).").arg(entries.size());
      return result;
    }
    // Existing-empty dir: parent's writability is implied by the dir's own
    // existence (we wouldn't be able to read it otherwise on any sane FS).
    // We still need to be able to create the staging sibling, which the
    // existing FileUtils2::generateCloneStagingDir path verifies separately.
  } else {
    // Non-existing: parent must exist + be a writable directory so we can
    // create the staging dir AND (eventually) the final dir alongside.
    const QFileInfo parentInfo(finalInfo.absolutePath());
    if (!parentInfo.exists() || !parentInfo.isDir()) {
      result.valid = false;
      result.message = tr("Parent folder of destination does not exist or is not a directory: %1.")
                           .arg(parentInfo.absoluteFilePath());
      return result;
    }
    if (!parentInfo.isWritable()) {
      result.valid = false;
      result.message = tr("Parent folder of destination is not writable: %1.")
                           .arg(parentInfo.absoluteFilePath());
      return result;
    }
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
  // SyncService / SyncCredentialsStore are required only for the PAT path.
  // We resolve them up-front so the worker thread never touches the
  // ServiceLocator (no DI overhead inside the closure).
  const bool needsSync = !p_input.pat.isEmpty();
  auto *syncService = needsSync ? m_services.get<SyncService>() : nullptr;
  auto *credStore = needsSync ? m_services.get<SyncCredentialsStore>() : nullptr;
  if (needsSync && (!syncService || !credStore)) {
    CloneAndOpenResult result;
    result.success = false;
    result.errorMessage = tr("Sync services not available; cannot use a PAT.");
    QMetaObject::invokeMethod(
        this, [this, result]() { emit cloneFinished(result); }, Qt::QueuedConnection);
    return;
  }

  const QString finalDir = QFileInfo(p_input.finalDestDir.trimmed()).absoluteFilePath();
  const QFileInfo finalInfo(finalDir);
  const QString parentDir = finalInfo.absolutePath();
  const QString leafName = finalInfo.fileName();

  // Step 2: stage on the SAME filesystem as the final destination so the
  // QDir::rename in step 5 is atomic (cross-filesystem rename fails on
  // Windows).
  QString stagingErr;
  const QString stagingDir = FileUtils2::generateCloneStagingDir(parentDir, leafName, &stagingErr);
  if (stagingDir.isEmpty()) {
    CloneAndOpenResult result;
    result.success = false;
    result.errorMessage = tr("Failed to create staging directory: %1").arg(stagingErr);
    QMetaObject::invokeMethod(
        this, [this, result]() { emit cloneFinished(result); }, Qt::QueuedConnection);
    return;
  }

  // Build config + credentials JSON once on the caller thread so the worker
  // closure captures simple QStrings. PAT contents are NEVER logged.
  QJsonObject configObj;
  configObj[QStringLiteral("backend")] = p_input.backend;
  configObj[QStringLiteral("remoteUrl")] = p_input.remoteUrl.trimmed();
  configObj[QStringLiteral("autoSyncEnabled")] = p_input.autoSyncEnabled;
  const QString configJson =
      QString::fromUtf8(QJsonDocument(configObj).toJson(QJsonDocument::Compact));

  QString credentialsJson;
  if (!p_input.pat.isEmpty()) {
    QJsonObject credsObj;
    credsObj[QStringLiteral("pat")] = p_input.pat;
    credentialsJson = QString::fromUtf8(QJsonDocument(credsObj).toJson(QJsonDocument::Compact));
  }

  const bool patEmpty = p_input.pat.isEmpty();
  const QString remoteUrl = p_input.remoteUrl.trimmed();
  const QString pat = p_input.pat; // capture by value -- lambda lifetime is bounded by QFuture

  // openurl-followups Item 2: create the cancellation token on the GUI
  // thread BEFORE spawning the worker. The token outlives the worker
  // (freed below in the GUI-thread tail), so the captured raw pointer the
  // worker uses is guaranteed valid for the entire clone duration.
  //
  // If a previous clone left a stale token (shouldn't happen — cloneAndOpen
  // is expected to be called serially per controller instance — but be
  // defensive), free it first.
  if (m_currentCloneToken) {
    vxcore_sync_free_cancellation(m_currentCloneToken);
    m_currentCloneToken = nullptr;
  }
  m_currentCloneToken = vxcore_sync_create_cancellation();
  VxCoreSyncCancellation *cancellationToken = m_currentCloneToken;

  // Capture pointers + values for the worker; no ServiceLocator access from
  // the worker thread (the resolution above is the last DI access).
  // Progress signal fired immediately so the dialog can show indeterminate
  // feedback.
  QMetaObject::invokeMethod(
      this, [this]() { emit cloneProgressUpdated(0, 100, tr("Cloning...")); },
      Qt::QueuedConnection);

  // We deliberately discard the returned QFuture: the worker's only
  // observable outputs are the queued-signal emissions, and cancellation is
  // routed through the cancellation token (not the QFuture).
  (void)QtConcurrent::run([this, notebookService, syncService, credStore, stagingDir, finalDir,
                           configJson, credentialsJson, remoteUrl, pat, patEmpty,
                           cancellationToken]() {
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
                                 "Verify the URL is reachable, the PAT (if any) is valid, "
                                 "and the remote is an actual VNote notebook.");
      }
      emitFinished(result);
      return;
    }

    // Step 4: rename staging -> final. Vxcore's NotebookManager already
    // recorded root_folder=stagingDir in session.json, but that's fine: step
    // 5 below closes + re-opens against finalDir to refresh the record.
    //
    // refine-open-notebook-dialog: if finalDir exists (validation guarantees
    // it is an empty directory), remove it first so the atomic rename can
    // proceed. Windows QDir::rename cannot overwrite even an empty dir, so
    // this pre-rmdir hop is mandatory on Windows; on POSIX it's harmless.
    // The 20x100ms retry loop mirrors teardownCreatedDir's defensive pattern
    // for transient antivirus / Explorer-preview handles.
    if (QFileInfo::exists(finalDir)) {
      QDir d(finalDir);
      bool removed = false;
      for (int attempt = 0; attempt < 20 && !removed; ++attempt) {
        if (d.removeRecursively()) {
          removed = true;
          break;
        }
        QThread::msleep(100);
      }
      if (!removed) {
        // We never modified finalDir's contents (it was empty going in and
        // rmdir failed). The user's original folder is intact; only the
        // staging dir needs cleanup.
        notebookService->closeNotebook(stagingNotebookId);
        QString rmErr;
        FileUtils2::removeStagingDir(stagingDir, &rmErr);
        CloneAndOpenResult result;
        result.success = false;
        result.errorMessage = tr("Could not prepare local root folder %1.").arg(finalDir);
        emitFinished(result);
        return;
      }
    }

    QString renameErr;
    if (!FileUtils2::renameStagingToFinal(stagingDir, finalDir, &renameErr)) {
      // Rename failed: rollback. Close the staging notebook so vxcore drops
      // the stale registration. removeStagingDir handles the dir; the
      // (non-existent) finalDir needs no cleanup.
      notebookService->closeNotebook(stagingNotebookId);
      QString rmErr;
      FileUtils2::removeStagingDir(stagingDir, &rmErr);
      CloneAndOpenResult result;
      result.success = false;
      result.errorMessage =
          tr("Failed to move cloned notebook into destination: %1").arg(trimDiagnostic(renameErr));
      emitFinished(result);
      return;
    }

    // Step 5: close + re-open with finalDir so vxcore's NotebookRecord
    // reflects the true root path. This is THE CRITICAL step that the plan
    // calls out: without it, session restore later cannot find the notebook
    // because root_folder still points at the obsolete staging dir.
    notebookService->closeNotebook(stagingNotebookId);
    const QString optionsJson =
        patEmpty ? QStringLiteral("{\"readOnly\":true}") : QStringLiteral("{}");
    const QString finalNotebookId = notebookService->openNotebookEx(finalDir, optionsJson);
    if (finalNotebookId.isEmpty()) {
      // Re-open failed: best-effort cleanup of the just-created finalDir
      // (which we own — user didn't have anything there before us).
      teardownCreatedDir(finalDir);
      CloneAndOpenResult result;
      result.success = false;
      result.errorMessage = tr("Cloned notebook could not be re-opened from %1.").arg(finalDir);
      emitFinished(result);
      return;
    }

    const QString notebookName = notebookNameFromConfig(notebookService, finalNotebookId);

    // Step 6: if PAT was supplied, persist + register sync. RO snapshot
    // path skips this entirely (snapshot-only MVP).
    if (patEmpty) {
      CloneAndOpenResult result;
      result.success = true;
      result.notebookId = finalNotebookId;
      result.notebookName = notebookName;
      result.isReadOnly = true;
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
        [this, syncService, credStore, finalNotebookId, finalDir, notebookName, remoteUrl, pat,
         emitFinished]() {
          auto conn = std::make_shared<QMetaObject::Connection>();
          *conn = connect(
              syncService, &SyncService::enableFinished, this,
              [this, conn, credStore, finalNotebookId, finalDir, notebookName, emitFinished](
                  const QString &p_resultId, VxCoreError p_result, const QString &p_message) {
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
                teardownCreatedDir(finalDir);
                CloneAndOpenResult fail;
                fail.success = false;
                fail.errorMessage = tr("Cloned notebook but failed to enable sync: %1")
                                        .arg(trimDiagnostic(p_message));
                emitFinished(fail);
              },
              Qt::QueuedConnection);
          syncService->enableSyncForNotebook(finalNotebookId, remoteUrl, pat);
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
