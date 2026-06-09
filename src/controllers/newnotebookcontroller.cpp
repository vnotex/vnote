#include "newnotebookcontroller.h"

#include <memory>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressDialog>
#include <QThread>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/synclog.h>
#include <core/services/syncservice.h>
#include <utils/pathutils.h>

#include <sync/sync_json_keys.h>
#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

NewNotebookController::NewNotebookController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

ValidationResult NewNotebookController::validateName(const QString &p_name) const {
  ValidationResult result;

  if (p_name.trimmed().isEmpty()) {
    result.valid = false;
    result.message = tr("Please specify a name for the notebook.");
  }

  return result;
}

ValidationResult NewNotebookController::validateRootFolder(const QString &p_path,
                                                           NotebookType p_type) const {
  ValidationResult result;
  QString rootFolderPath = p_path.trimmed();

  // Check if path is legal.
  if (!PathUtils::isLegalPath(rootFolderPath)) {
    result.valid = false;
    result.message = tr("Please specify a valid root folder for the notebook.");
    return result;
  }

  // Check if path exists and is valid.
  QFileInfo finfo(rootFolderPath);
  if (finfo.exists()) {
    if (!finfo.isDir()) {
      result.valid = false;
      result.message = tr("Root folder should be a directory.");
      return result;
    }
    // Only require empty directory for bundled notebooks.
    if (p_type == NotebookType::Bundled && !QDir(rootFolderPath).isEmpty()) {
      result.valid = false;
      result.message = tr("Root folder of the notebook must be empty. "
                          "If you want to import existing data, please try other operations.");
      return result;
    }
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
        result.message =
            tr("There already exists a notebook (%1) with the same root folder.").arg(existingName);
        return result;
      }
    }
  }

  return result;
}

ValidationResult NewNotebookController::validateAll(const NewNotebookInput &p_input) const {
  ValidationResult result;

  // Validate name.
  result = validateName(p_input.name);
  if (!result.valid) {
    return result;
  }

  // Validate root folder.
  result = validateRootFolder(p_input.rootFolderPath, p_input.type);
  if (!result.valid) {
    return result;
  }

  return result;
}

NewNotebookResult NewNotebookController::createNotebook(const NewNotebookInput &p_input) {
  qCDebug(syncCategory) << "NewNotebookController::createNotebook: entry syncMethod:"
                        << p_input.syncMethod << "remoteUrl:notInInputStruct";

  NewNotebookResult result;

  // Validate first.
  ValidationResult validation = validateAll(p_input);
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

  // Build config JSON for vxcore.
  QString configJson = buildConfigJson(p_input);

  // Create notebook via service.
  QString notebookId =
      notebookService->createNotebook(p_input.rootFolderPath.trimmed(), configJson, p_input.type);

  if (notebookId.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("Failed to create notebook in (%1).").arg(p_input.rootFolderPath);
    return result;
  }

  qCDebug(syncCategory) << "NewNotebookController::createNotebook: created notebookId:"
                        << notebookId << "success:" << !notebookId.isEmpty();

  result.success = true;
  result.notebookId = notebookId;
  return result;
}

QString NewNotebookController::buildConfigJson(const NewNotebookInput &p_input) {
  QJsonObject configObj;
  configObj[QLatin1String(vxcore::kJsonKeyName)] = p_input.name.trimmed();
  if (!p_input.description.trimmed().isEmpty()) {
    configObj[QLatin1String(vxcore::kJsonKeyDescription)] = p_input.description.trimmed();
  }
  QString assetsFolderTrimmed = p_input.assetsFolder.trimmed();
  if (!assetsFolderTrimmed.isEmpty() && assetsFolderTrimmed != QStringLiteral("vx_assets")) {
    configObj[QLatin1String(vxcore::kJsonKeyAssetsFolder)] = assetsFolderTrimmed;
  }
  // Per ADR-8: sync configuration uses FLAT keys on the notebook config
  // (syncEnabled, syncBackend), not a nested "sync" object. syncRemoteUrl is
  // intentionally NOT set here — it stays empty until T14 bootstrap runs
  // enableSync against the empty root (per ADR-7: create-then-enable).
  if (p_input.syncMethod == QStringLiteral("git")) {
    configObj[QLatin1String(vxcore::kJsonKeySyncEnabled)] = true;
    configObj[QLatin1String(vxcore::kJsonKeySyncBackend)] = QStringLiteral("git");
  }
  return QString::fromUtf8(QJsonDocument(configObj).toJson(QJsonDocument::Compact));
}

QProgressDialog *NewNotebookController::createBootstrapModal(QWidget *p_parent) {
  auto *modal = new QProgressDialog(p_parent);
  modal->setWindowTitle(QObject::tr("Setting up sync"));
  modal->setLabelText(QObject::tr("Connecting to remote and syncing... "
                                  "(Sync cannot be cancelled once started.)"));
  modal->setRange(0, 0); // Indeterminate spinner.
  // CRITICAL (per plan rule): no cancel button. setCancelButton(nullptr) removes
  // the cancel button entirely, unlike setCancelButtonText("") which would just
  // blank its text but leave it interactable.
  modal->setCancelButton(nullptr);
  modal->setMinimumDuration(0); // Show immediately, don't wait for default 4s.
  modal->setAttribute(Qt::WA_DeleteOnClose, false);
  modal->setModal(true);
  return modal;
}

void NewNotebookController::bootstrapSync(const QString &p_notebookId, const QString &p_remoteUrl,
                                          const QString &p_pat, QWidget *p_dialogParent) {
  auto *notebookService = m_services.get<NotebookCoreService>();
  auto *syncService = m_services.get<SyncService>();
  if (!notebookService || !syncService) {
    emit bootstrapFailed(p_notebookId,
                         tr("Sync services not available; cannot bootstrap notebook."));
    return;
  }

  // Capture the notebook root path BEFORE invoking enableSync, because on
  // failure we need to closeNotebook (which discards the rootPath in vxcore)
  // and then QDir::removeRecursively() the on-disk directory (per ADR-2).
  // listNotebooks returns camelCase JSON keys (per vxcore convention): the
  // notebook id is "id" and the root folder is "rootFolder".
  QString rootPath;
  {
    const QJsonArray notebooks = notebookService->listNotebooks();
    for (const auto &nbVal : notebooks) {
      const QJsonObject nbObj = nbVal.toObject();
      if (nbObj.value(QLatin1String(vxcore::kJsonKeyId)).toString() == p_notebookId) {
        rootPath = nbObj.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
        break;
      }
    }
  }

  // Show the indeterminate, no-cancel progress modal.
  QProgressDialog *modal = createBootstrapModal(p_dialogParent);
  modal->show();

  // One-shot connection: enableFinished can fire for any notebook, so we filter
  // by id inside the lambda and disconnect manually after handling our id.
  auto conn = std::make_shared<QMetaObject::Connection>();
  *conn =
      connect(syncService, &SyncService::enableFinished, this,
              [this, conn, notebookService, syncService, p_notebookId, p_remoteUrl, rootPath,
               modal](const QString &p_resultId, VxCoreError p_result, const QString &p_message) {
                if (p_resultId != p_notebookId) {
                  return;
                }
                QObject::disconnect(*conn);

                // Close + delete the modal regardless of outcome.
                modal->close();
                modal->deleteLater();

                if (p_result == VXCORE_OK) {
                  // Persist the flat ADR-8 syncRemoteUrl key into the notebook config.
                  QJsonObject cfg = notebookService->getNotebookConfig(p_notebookId);
                  cfg[QLatin1String(vxcore::kJsonKeySyncRemoteUrl)] = p_remoteUrl;
                  const QString cfgJson =
                      QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
                  notebookService->updateNotebookConfig(p_notebookId, cfgJson);

                  // Idempotent initial sync push.
                  syncService->triggerSyncNow(p_notebookId);

                  emit bootstrapSucceeded(p_notebookId);
                  return;
                }

                // Failure path. Per ADR-2: cleanup uses closeNotebook +
                // QDir::removeRecursively (no notebook_delete C API exists).
                qCritical() << "Bootstrap sync failed for notebook" << p_notebookId
                            << "result:" << static_cast<int>(p_result) << "message:" << p_message;

                // T1: Delete the just-stored PAT from the keychain to prevent
                // orphan vault entries. Mirror the pattern from
                // SyncService::bootstrapAndPersist rollback (line 774-776).
                auto *credStore = syncService->credentialsStore();
                if (credStore) {
                  credStore->deleteCredentials(p_notebookId);
                }

                notebookService->closeNotebook(p_notebookId);
                if (!rootPath.isEmpty()) {
                  // SQLite/libgit2 may hold transient file handles on Windows just
                  // after close; retry removeRecursively a few times to give the OS
                  // a chance to release the locks.
                  QDir dir(rootPath);
                  bool removed = false;
                  for (int attempt = 0; attempt < 20 && dir.exists(); ++attempt) {
                    removed = dir.removeRecursively();
                    if (removed && !dir.exists()) {
                      break;
                    }
                    QThread::msleep(100);
                  }
                  if (dir.exists()) {
                    qWarning() << "Bootstrap cleanup: failed to remove root after retries:"
                               << rootPath;
                  }
                }
                emit bootstrapFailed(p_notebookId, p_message);
              });

  // Fire enableSync LAST so the connect is in place before the signal could fire.
  syncService->enableSyncForNotebook(p_notebookId, p_remoteUrl, p_pat);
}
