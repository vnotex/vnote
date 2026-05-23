#include "notebooksyncinfocontroller.h"

#include <memory>

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/synclog.h>
#include <core/services/syncservice.h>

#include <sync/sync_json_keys.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

NotebookSyncInfoController::NotebookSyncInfoController(ServiceLocator &p_services,
                                                       const QString &p_notebookId,
                                                       QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_notebookId(p_notebookId) {
  // Connect once to SyncService signals; filtering by notebookId happens in
  // each slot. Both ends are GUI-thread (SyncService re-emits on the GUI
  // thread); default Qt::AutoConnection resolves to DirectConnection.
  if (auto *syncSvc = m_services.get<SyncService>()) {
    connect(syncSvc, &SyncService::credentialsSetFinished, this,
            &NotebookSyncInfoController::onCredentialsSetFinished);
    connect(syncSvc, &SyncService::disableFinished, this,
            &NotebookSyncInfoController::onDisableFinished);
  }
}

QString NotebookSyncInfoController::notebookName() const {
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    return QString();
  }
  const QJsonObject cfg = notebookSvc->getNotebookConfig(m_notebookId);
  return cfg.value(QStringLiteral("name")).toString();
}

QString NotebookSyncInfoController::remoteUrl() const {
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    return QString();
  }
  const QJsonObject cfg = notebookSvc->getNotebookConfig(m_notebookId);
  // ADR-8: use the FLAT key, not a nested "sync.remoteUrl".
  return cfg.value(QLatin1String(vxcore::kJsonKeySyncRemoteUrl)).toString();
}

QString NotebookSyncInfoController::lastSyncTime() const {
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    return QString();
  }
  const qint64 millis = notebookSvc->getLastSyncUtc(m_notebookId);
  if (millis <= 0) {
    return QString();
  }
  return QLocale::system().toString(QDateTime::fromMSecsSinceEpoch(millis), QLocale::ShortFormat);
}

void NotebookSyncInfoController::loadInitialData() {
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    emit error(tr("Notebook service not available."));
    return;
  }

  const QJsonObject cfg = notebookSvc->getNotebookConfig(m_notebookId);
  const QString name = cfg.value(QStringLiteral("name")).toString();
  const QString url = cfg.value(QLatin1String(vxcore::kJsonKeySyncRemoteUrl)).toString();
  // Last sync timestamp is per-device and lives in metadata.db (NOT in the
  // notebook config JSON, which would self-sync). Read via vxcore C API.
  const qint64 lastSyncMillis = notebookSvc->getLastSyncUtc(m_notebookId);
  QString lastSync;
  if (lastSyncMillis > 0) {
    lastSync = QLocale::system().toString(QDateTime::fromMSecsSinceEpoch(lastSyncMillis),
                                          QLocale::ShortFormat);
  }

  // Cache the URL so applyChanges() can detect whether the user changed it.
  m_currentRemoteUrl = url;

  emit dataLoaded(name, url, lastSync);
}

bool NotebookSyncInfoController::persistRemoteUrl(const QString &p_newRemoteUrl) {
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    emit error(tr("Notebook service not available."));
    return false;
  }

  // Read-modify-write the notebook config. Per ADR-8 the FLAT
  // "syncRemoteUrl" key is the source of truth.
  QJsonObject cfg = notebookSvc->getNotebookConfig(m_notebookId);
  cfg[QLatin1String(vxcore::kJsonKeySyncRemoteUrl)] = p_newRemoteUrl;
  const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
  if (!notebookSvc->updateNotebookConfig(m_notebookId, cfgJson)) {
    emit error(tr("Failed to update notebook configuration."));
    return false;
  }
  m_currentRemoteUrl = p_newRemoteUrl;
  return true;
}

void NotebookSyncInfoController::applyChanges(const QString &p_newRemoteUrl,
                                              const QString &p_newPat) {
  // W3.T3 — URL-change-on-S5 atomic disable+re-enable detection.
  //
  // Per W1.T1 evidence: re-calling vxcore_sync_enable (with credentials) with a
  // different URL updates the in-memory configs_ map but does NOT rewrite the
  // on-disk git remote.origin.url; subsequent Sync() would silently push to
  // the OLD remote (split-brain). The only correct way to change the URL on
  // a registered notebook is the atomic disable+wipe+re-enable sequence.
  //
  // Conditions for routing through the confirmation flow:
  //   1. isSyncRegistered(id) == true   (notebook is registered at runtime).
  //   2. p_newRemoteUrl != m_currentRemoteUrl   (URL actually changed).
  //   3. !p_newRemoteUrl.isEmpty()   (clearing the URL via this path is not
  //      supported — disableSync should be used instead).
  auto *syncSvc = m_services.get<SyncService>();
  const bool urlChanged = (p_newRemoteUrl != m_currentRemoteUrl);
  const bool isRegistered = syncSvc && syncSvc->isSyncRegistered(m_notebookId);
  const bool newUrlNonEmpty = !p_newRemoteUrl.isEmpty();

  if (urlChanged && isRegistered && newUrlNonEmpty) {
    // Defer the actual write until the user confirms the destructive flow
    // via the QMessageBox shown by the dialog. The user-provided new PAT (if
    // any) is cached here so confirmUrlChange() can use it without a second
    // round-trip through the dialog. NEVER logged.
    qCDebug(syncCategory) << "NotebookSyncInfoController::applyChanges: URL change on "
                             "registered notebook detected; deferring to confirmUrlChange. id="
                          << m_notebookId;
    m_pendingUrlChange = true;
    m_pendingUrlChangeNewUrl = p_newRemoteUrl;
    m_pendingUrlChangeProvidedPat = p_newPat;
    emit confirmUrlChangeRequested(m_currentRemoteUrl, p_newRemoteUrl);
    return;
  }

  // Diff URL against cached value; only write when the user actually changed
  // it. Empty new URL is allowed (clears the field) only if it differs from
  // the cached value.
  bool urlOk = true;
  if (urlChanged) {
    urlOk = persistRemoteUrl(p_newRemoteUrl);
  }

  // Snapshot the URL write outcome so the credentialsSetFinished slot can
  // combine it with the PAT result before emitting applyComplete.
  m_pendingUrlWriteOk = urlOk;

  if (!p_newPat.isEmpty()) {
    if (!syncSvc) {
      emit error(tr("Sync service not available."));
      m_pendingPatUpdate = false;
      emit applyComplete(false);
      return;
    }
    m_pendingPatUpdate = true;
    syncSvc->updateCredentials(m_notebookId, p_newPat);
    return;
  }

  // No PAT update requested; applyComplete fires synchronously based on the
  // URL-write outcome alone.
  m_pendingPatUpdate = false;
  if (urlOk) {
    // URL-only completion path: PAT unchanged, runtime state may be empty
    // for previously-partial notebooks. Trigger init now.
    if (syncSvc) {
      syncSvc->ensureSyncEnabled(m_notebookId);
    }
  }
  emit applyComplete(urlOk);
}

void NotebookSyncInfoController::disableSync() {
  auto *syncSvc = m_services.get<SyncService>();
  if (!syncSvc) {
    emit error(tr("Sync service not available."));
    return;
  }
  syncSvc->disableSyncForNotebook(m_notebookId);
}

void NotebookSyncInfoController::bootstrapApply(const QString &p_url, const QString &p_pat) {
  // W3.T1 / Task 13.4 (F1.6): atomic enable+persist on an EXISTING partial
  // notebook (S1/S2/S3/S4 -> S5). Delegates to SyncService::bootstrapAndPersist
  // which wraps enable + flat-key persist atomically with rollback on persist
  // failure. Previously this controller did enable-then-persist in two steps;
  // if persist failed after enable success the notebook ended up in S5 on disk
  // but missing syncRemoteUrl -> next reconcile saw S4 and bailed.
  //
  // Unlike NewNotebookController::bootstrapSync, we do NOT delete the
  // notebook on failure (the notebook already exists with user content; the
  // partial sync config is left in place so the user can retry).
  qCDebug(syncCategory) << "NotebookSyncInfoController::bootstrapApply: start id=" << m_notebookId;

  auto *syncSvc = m_services.get<SyncService>();
  if (!syncSvc) {
    qCWarning(syncCategory)
        << "NotebookSyncInfoController::bootstrapApply: SyncService not available; id="
        << m_notebookId;
    emit error(tr("Sync service not available."));
    emit applyComplete(false);
    return;
  }

  const QString notebookId = m_notebookId;

  // One-shot connection on bootstrapAndPersistFinished, filtered by notebookId.
  auto conn = std::make_shared<QMetaObject::Connection>();
  *conn = connect(
      syncSvc, &SyncService::bootstrapAndPersistFinished, this,
      [this, conn, notebookId, p_url](const QString &p_resultId, VxCoreError p_result,
                                      const QString &p_message) {
        if (p_resultId != notebookId) {
          return;
        }
        QObject::disconnect(*conn);

        qCDebug(syncCategory)
            << "NotebookSyncInfoController::bootstrapApply: bootstrapAndPersistFinished result="
            << static_cast<int>(p_result) << "id=" << notebookId;

        if (p_result == VXCORE_OK) {
          // Sync the cached URL so subsequent applyChanges() URL-diff detects
          // no-op for the same URL.
          m_currentRemoteUrl = p_url;
          emit applyComplete(true);
          return;
        }

        // Failure path: KEEP the notebook (no closeNotebook, no
        // removeRecursively). Partial sync config stays so the user can
        // retry from the Sync Info dialog.
        qCWarning(syncCategory)
            << "NotebookSyncInfoController::bootstrapApply: failed id=" << notebookId
            << "result:" << static_cast<int>(p_result) << "message:" << p_message;
        emit error(p_message.isEmpty() ? tr("Failed to enable sync for notebook.") : p_message);
        emit applyComplete(false);
      });

  // PAT is forwarded to SyncService and NEVER logged here (per ADR-9).
  syncSvc->bootstrapAndPersist(notebookId, p_url, p_pat);
}

void NotebookSyncInfoController::confirmUrlChange(bool p_confirmed) {
  // W3.T3 — Confirmation callback from the dialog's QMessageBox routing the
  // user's decision about the URL change on a registered notebook.
  if (!m_pendingUrlChange) {
    // Idempotent / out-of-order: no URL change is pending. Do nothing.
    qCDebug(syncCategory)
        << "NotebookSyncInfoController::confirmUrlChange: no pending URL change; ignored. id="
        << m_notebookId;
    return;
  }

  // Snapshot + clear pending state immediately so a reentrant call from a
  // slot does not double-execute.
  const QString newUrl = m_pendingUrlChangeNewUrl;
  const QString providedPat = m_pendingUrlChangeProvidedPat;
  m_pendingUrlChange = false;
  m_pendingUrlChangeNewUrl.clear();
  m_pendingUrlChangeProvidedPat.clear();

  if (!p_confirmed) {
    // User cancelled. Per spec: no-op (the dialog is responsible for
    // resetting its URL field). Do NOT emit applyComplete — the dialog did
    // not actually call applyChanges in the destructive sense.
    qCDebug(syncCategory)
        << "NotebookSyncInfoController::confirmUrlChange: user cancelled URL change. id="
        << m_notebookId;
    return;
  }

  qCDebug(syncCategory)
      << "NotebookSyncInfoController::confirmUrlChange: user confirmed URL change. id="
      << m_notebookId;

  if (!providedPat.isEmpty()) {
    // User provided a new PAT in the dialog; use it directly without going
    // through the keychain. PAT is forwarded into the chain via lambda
    // captures and NEVER logged.
    performAtomicUrlReChange(newUrl, providedPat);
    return;
  }

  // No new PAT provided. Read the EXISTING PAT from the keychain BEFORE
  // calling disableSyncForNotebook, because W2.T5's post-disable cleanup
  // deletes the keychain entry. If we waited until after disable, the PAT
  // would be gone and we could not re-enable atomically.
  auto *credStore = m_services.get<SyncCredentialsStore>();
  if (!credStore) {
    emit error(tr("Credentials store not available."));
    emit applyComplete(false);
    return;
  }

  const QString notebookId = m_notebookId;
  auto retConn = std::make_shared<QMetaObject::Connection>();
  auto errConn = std::make_shared<QMetaObject::Connection>();

  *retConn = connect(
      credStore, &SyncCredentialsStore::credentialsRetrieved, this,
      [this, retConn, errConn, notebookId, newUrl](const QString &p_id, const QString &p_pat) {
        if (p_id != notebookId) {
          return;
        }
        QObject::disconnect(*retConn);
        QObject::disconnect(*errConn);
        // PAT received from keychain; chain into the
        // atomic disable+re-enable flow. NEVER logged.
        performAtomicUrlReChange(newUrl, p_pat);
      });

  *errConn = connect(
      credStore, &SyncCredentialsStore::credentialsError, this,
      [this, retConn, errConn, notebookId](const QString &p_id, const QString &p_msg) {
        if (p_id != notebookId) {
          return;
        }
        QObject::disconnect(*retConn);
        QObject::disconnect(*errConn);
        qCWarning(syncCategory) << "NotebookSyncInfoController::confirmUrlChange: failed "
                                   "to read existing PAT from keychain; id="
                                << notebookId << "message:" << p_msg;
        emit error(tr("URL change failed: cannot read existing credentials. Please retry."));
        emit applyComplete(false);
      });

  credStore->retrieveCredentials(notebookId);
}

void NotebookSyncInfoController::performAtomicUrlReChange(const QString &p_newUrl,
                                                          const QString &p_pat) {
  // W3.T3 — Atomic disable+re-enable for the URL-change-on-S5 flow.
  //
  // Sequence:
  //   1. Install one-shot listener on SyncService::disableFinished filtered
  //      by m_notebookId.
  //   2. Call SyncService::disableSyncForNotebook (which also clears the
  //      flat JSON sync keys via W2.T5 cleanup).
  //   3. On disableFinished VXCORE_OK:
  //        a. Install one-shot listener on SyncService::enableFinished
  //           filtered by m_notebookId.
  //        b. Call SyncService::enableSyncForNotebook with the NEW URL and
  //           the PAT carried in via lambda capture.
  //        c. On enableFinished VXCORE_OK:
  //             - Restore the flat sync keys (syncEnabled / syncBackend /
  //               syncRemoteUrl) since W2.T5 cleared them on disable.
  //             - Update m_currentRemoteUrl.
  //             - triggerSyncNow.
  //             - emit applyComplete(true).
  //        d. On enableFinished failure:
  //             - emit error(URL-change-specific message).
  //             - emit applyComplete(false).
  //             - Notebook is left in clean S0 (the JSON was already cleared
  //               by W2.T5 on disable; we did NOT re-write it pre-enable).
  //   4. On disableFinished failure:
  //        - emit error.
  //        - emit applyComplete(false).
  //        - Notebook stays in S5 (W2.T5 only clears JSON on disable OK).
  //
  // PAT is forwarded via lambda captures and is NEVER logged in any branch.
  auto *syncSvc = m_services.get<SyncService>();
  if (!syncSvc) {
    emit error(tr("Sync service not available."));
    emit applyComplete(false);
    return;
  }

  const QString notebookId = m_notebookId;

  auto disableConn = std::make_shared<QMetaObject::Connection>();
  *disableConn = connect(
      syncSvc, &SyncService::disableFinished, this,
      [this, disableConn, syncSvc, notebookId, p_newUrl, p_pat](const QString &p_id,
                                                                VxCoreError p_result) {
        if (p_id != notebookId) {
          return;
        }
        QObject::disconnect(*disableConn);

        qCDebug(syncCategory)
            << "NotebookSyncInfoController::performAtomicUrlReChange: disableFinished result="
            << static_cast<int>(p_result) << "id=" << notebookId;

        if (p_result != VXCORE_OK) {
          qCWarning(syncCategory)
              << "NotebookSyncInfoController::performAtomicUrlReChange: disable failed; id="
              << notebookId << "result:" << static_cast<int>(p_result);
          emit error(tr("URL change failed: disable error."));
          emit applyComplete(false);
          return;
        }

        // Disable succeeded. SyncService's W2.T5 cleanup has now cleared
        // syncEnabled / syncBackend / syncRemoteUrl from the on-disk JSON.
        // However, vxcore's DisableSync only clears in-memory maps; it does
        // NOT wipe the on-disk vx_notebook/vx_sync/ git working directory
        // (per W1.T2 evidence). If we re-enable now with a NEW URL, vxcore's
        // GitSyncBackend::Initialize would see the OLD remote in the
        // existing .git/config and refuse to switch (returning
        // VXCORE_ERR_INVALID_PARAM). Wipe the vx_sync directory ourselves
        // before re-enable so the new clone runs on a clean tree.
        auto *notebookSvcForWipe = m_services.get<NotebookCoreService>();
        if (notebookSvcForWipe) {
          const QString syncDir = notebookSvcForWipe->buildAbsolutePath(
              notebookId, QStringLiteral("vx_notebook/vx_sync"));
          if (!syncDir.isEmpty()) {
            QDir wipeDir(syncDir);
            if (wipeDir.exists()) {
              if (!wipeDir.removeRecursively()) {
                qCWarning(syncCategory)
                    << "NotebookSyncInfoController::performAtomicUrlReChange: failed to wipe "
                       "vx_sync directory; re-enable may fail; id="
                    << notebookId << "path:" << syncDir;
              } else {
                qCDebug(syncCategory) << "NotebookSyncInfoController::performAtomicUrlReChange: "
                                         "wiped vx_sync directory; id="
                                      << notebookId;
              }
            }
          }
        }

        // Now wire the second one-shot listener for re-enable.
        auto enableConn = std::make_shared<QMetaObject::Connection>();
        *enableConn = connect(
            syncSvc, &SyncService::enableFinished, this,
            [this, enableConn, syncSvc, notebookId,
             p_newUrl](const QString &p_enId, VxCoreError p_enResult, const QString &p_enMsg) {
              if (p_enId != notebookId) {
                return;
              }
              QObject::disconnect(*enableConn);

              qCDebug(syncCategory) << "NotebookSyncInfoController::performAtomicUrlReChange: "
                                       "enableFinished result="
                                    << static_cast<int>(p_enResult) << "id=" << notebookId;

              if (p_enResult == VXCORE_OK) {
                // Re-enable succeeded; restore the flat sync keys (all 3,
                // since W2.T5 cleared them on disable). We write them
                // together so the on-disk config remains internally
                // consistent at all times.
                auto *notebookSvc = m_services.get<NotebookCoreService>();
                if (notebookSvc) {
                  QJsonObject cfg = notebookSvc->getNotebookConfig(notebookId);
                  cfg[QLatin1String(vxcore::kJsonKeySyncEnabled)] = true;
                  cfg[QLatin1String(vxcore::kJsonKeySyncBackend)] = QStringLiteral("git");
                  cfg[QLatin1String(vxcore::kJsonKeySyncRemoteUrl)] = p_newUrl;
                  const QString cfgJson =
                      QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
                  if (notebookSvc->updateNotebookConfig(notebookId, cfgJson)) {
                    m_currentRemoteUrl = p_newUrl;
                  } else {
                    qCWarning(syncCategory)
                        << "NotebookSyncInfoController::performAtomicUrlReChange: failed to "
                           "restore JSON sync fields after re-enable; id="
                        << notebookId;
                  }
                }

                // Idempotent initial sync against the NEW remote.
                syncSvc->triggerSyncNow(notebookId);
                emit applyComplete(true);
                return;
              }

              // Re-enable failed; the on-disk state is clean S0 (W2.T5
              // already cleared the JSON during disable, and we did NOT
              // re-write it before enable). The user can re-enable via the
              // Enable Sync UI (W4.T2) to retry.
              qCWarning(syncCategory)
                  << "NotebookSyncInfoController::performAtomicUrlReChange: re-enable failed; id="
                  << notebookId << "result:" << static_cast<int>(p_enResult)
                  << "message:" << p_enMsg;
              emit error(tr("URL change failed: re-enable error. Notebook now in disabled "
                            "state; use Enable Sync to retry."));
              emit applyComplete(false);
            });

        // PAT is forwarded into the worker via SyncService and NEVER
        // logged here.
        syncSvc->enableSyncForNotebook(notebookId, p_newUrl, p_pat);
      });

  syncSvc->disableSyncForNotebook(notebookId);
}

void NotebookSyncInfoController::onCredentialsSetFinished(const QString &p_notebookId,
                                                          VxCoreError p_result) {
  if (p_notebookId != m_notebookId || !m_pendingPatUpdate) {
    return;
  }
  m_pendingPatUpdate = false;
  const bool patOk = (p_result == VXCORE_OK);
  if (patOk) {
    // URL+PAT completion path: PAT is now in keychain. If runtime state
    // is empty for a previously-partial notebook, trigger init now that
    // reconcile can read the freshly-written PAT.
    if (auto *syncSvc = m_services.get<SyncService>()) {
      syncSvc->ensureSyncEnabled(m_notebookId);
    }
  }
  emit applyComplete(m_pendingUrlWriteOk && patOk);
}

void NotebookSyncInfoController::onDisableFinished(const QString &p_notebookId,
                                                   VxCoreError p_result) {
  if (p_notebookId != m_notebookId) {
    return;
  }
  Q_UNUSED(p_result);
  emit disableComplete(p_notebookId);
}
