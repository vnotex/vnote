#include "recyclebincontroller.h"

#include <QDateTime>
#include <QDir>
#include <QJsonObject>

#include <QMetaObject>
#include <QtConcurrent>

#include <utility>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>

#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

RecycleBinController::RecycleBinController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services),
      m_nowProvider([]() { return QDateTime::currentMSecsSinceEpoch(); }),
      m_stopToken(std::make_shared<std::atomic_bool>(false)) {}

RecycleBinController::~RecycleBinController() {
  if (auto *hookMgr = m_services.get<HookManager>()) {
    if (m_afterStartHookId != -1) {
      hookMgr->removeAction(m_afterStartHookId);
    }
    if (m_afterOpenHookId != -1) {
      hookMgr->removeAction(m_afterOpenHookId);
    }
  }

  m_stopToken->store(true, std::memory_order_relaxed);
  if (m_activePrepared) {
    m_activePrepared->cancel();
  }
  m_cleanupFuture.waitForFinished();
  m_activePrepared.reset();
}

void RecycleBinController::startAutomaticCleanup() {
  if (m_automaticCleanupStarted) {
    return;
  }

  auto *hookMgr = m_services.get<HookManager>();
  if (!hookMgr) {
    return;
  }

  m_afterStartHookId = hookMgr->addAction(
      HookNames::MainWindowAfterStart,
      [this](HookContext &, const QVariantMap &) {
        auto *notebookService = m_services.get<NotebookCoreService>();
        if (!notebookService) {
          return;
        }
        const QJsonArray notebooks = notebookService->listNotebooks();
        for (const auto &value : notebooks) {
          queueAutomaticCleanup(
              value.toObject().value(QLatin1String(vxcore::kJsonKeyId)).toString());
        }
      },
      /*p_priority=*/0);
  m_afterOpenHookId = hookMgr->addAction<NotebookOpenEvent>(
      HookNames::NotebookAfterOpen,
      [this](HookContext &, const NotebookOpenEvent &p_event) {
        queueAutomaticCleanup(p_event.notebookId);
      },
      /*p_priority=*/0);
  m_automaticCleanupStarted = m_afterStartHookId != -1 && m_afterOpenHookId != -1;
}

void RecycleBinController::setNowProviderForTesting(std::function<qint64()> p_provider) {
  m_nowProvider =
      p_provider ? std::move(p_provider)
                 : std::function<qint64()>([]() { return QDateTime::currentMSecsSinceEpoch(); });
}

void RecycleBinController::queueAutomaticCleanup(const QString &p_notebookId) {
  if (p_notebookId.isEmpty() || m_stopToken->load(std::memory_order_relaxed) ||
      m_queuedNotebookIds.contains(p_notebookId)) {
    return;
  }
  m_queuedNotebookIds.insert(p_notebookId);
  m_pendingNotebookIds.enqueue(p_notebookId);
  processNextAutomaticCleanup();
}

void RecycleBinController::processNextAutomaticCleanup() {
  if (m_activePrepared || m_stopToken->load(std::memory_order_relaxed)) {
    return;
  }

  auto *configMgr = m_services.get<ConfigMgr2>();
  auto *notebookService = m_services.get<NotebookCoreService>();
  auto *ioGate = m_services.get<NotebookIoGate>();
  while (!m_pendingNotebookIds.isEmpty()) {
    const QString notebookId = m_pendingNotebookIds.dequeue();
    if (!configMgr || !notebookService || !ioGate) {
      m_queuedNotebookIds.remove(notebookId);
      continue;
    }

    CoreConfig &config = configMgr->getCoreConfig();
    if (!config.isRecycleBinAutoCleanupEnabled()) {
      m_queuedNotebookIds.remove(notebookId);
      continue;
    }

    const qint64 nowUtcMs = qMax<qint64>(1, m_nowProvider());
    const qint64 retentionMs =
        static_cast<qint64>(config.getRecycleBinRetentionDays()) * 24 * 60 * 60 * 1000;
    const qint64 enabledSinceUtc = config.getRecycleBinCleanupEnabledSinceUtc();
    if (enabledSinceUtc <= 0 || enabledSinceUtc > nowUtcMs) {
      config.setRecycleBinCleanupEnabledSinceUtc(nowUtcMs);
      m_queuedNotebookIds.remove(notebookId);
      continue;
    }
    if (nowUtcMs - enabledSinceUtc < retentionMs) {
      m_queuedNotebookIds.remove(notebookId);
      continue;
    }

    auto prepared = notebookService->prepareRecycleBinCleanup(notebookId, nowUtcMs - retentionMs);
    if (!prepared.isValid()) {
      RecycleBinCleanupResult result;
      result.m_error = prepared.m_error;
      result.m_errorMessage = prepared.m_errorMessage;
      finishAutomaticCleanup(notebookId, result);
      continue;
    }

    auto sharedPrepared = std::make_shared<PreparedRecycleBinCleanup>(std::move(prepared));
    m_activePrepared = sharedPrepared;
    const auto stopToken = m_stopToken;
    m_cleanupFuture =
        QtConcurrent::run([this, notebookService, ioGate, notebookId, sharedPrepared, stopToken]() {
          RecycleBinCleanupResult result;
          for (;;) {
            if (stopToken->load(std::memory_order_relaxed)) {
              sharedPrepared->cancel();
              result.m_error = VXCORE_ERR_CANCELLED;
              result.m_errorMessage = QString::fromUtf8(vxcore_error_message(result.m_error));
              break;
            }

            NotebookIoGate::ScopedTryLock lock(*ioGate, notebookId, 100);
            if (!lock.isLocked()) {
              continue;
            }
            if (stopToken->load(std::memory_order_relaxed)) {
              sharedPrepared->cancel();
            }
            result = notebookService->executeRecycleBinCleanup(*sharedPrepared);
            break;
          }

          QMetaObject::invokeMethod(
              this,
              [this, notebookId, result]() {
                finishAutomaticCleanup(notebookId, result);
                processNextAutomaticCleanup();
              },
              Qt::QueuedConnection);
        });
    return;
  }
}

void RecycleBinController::finishAutomaticCleanup(const QString &p_notebookId,
                                                  const RecycleBinCleanupResult &p_result) {
  if (p_result.m_error != VXCORE_OK && p_result.m_error != VXCORE_ERR_READ_ONLY &&
      p_result.m_error != VXCORE_ERR_UNSUPPORTED && p_result.m_error != VXCORE_ERR_CANCELLED) {
    qWarning() << "Automatic recycle bin cleanup failed for" << p_notebookId
               << p_result.m_errorMessage;
  }
  emit automaticCleanupFinished(p_notebookId, p_result.m_error, p_result.m_removedCount);
  m_queuedNotebookIds.remove(p_notebookId);
  m_activePrepared.reset();
}

QString RecycleBinController::getRecycleBinPath(const QString &p_notebookId) const {
  if (p_notebookId.isEmpty()) {
    return QString();
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    return QString();
  }

  return notebookService->getRecycleBinPath(p_notebookId);
}

QString RecycleBinController::getNotebookName(const QString &p_notebookId) const {
  if (p_notebookId.isEmpty()) {
    return QString();
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    return QString();
  }

  QJsonObject config = notebookService->getNotebookConfig(p_notebookId);
  return config.value(QLatin1String(vxcore::kJsonKeyName)).toString();
}

RecycleBinResult RecycleBinController::prepareRecycleBinPath(const QString &p_notebookId) {
  RecycleBinResult result;

  QString recycleBinPath = getRecycleBinPath(p_notebookId);
  if (recycleBinPath.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("Recycle bin is not supported for this notebook type.");
    return result;
  }

  // Ensure the directory exists before opening.
  QDir dir(recycleBinPath);
  if (!dir.exists()) {
    if (!dir.mkpath(".")) {
      result.success = false;
      result.errorMessage = tr("Failed to create recycle bin folder.");
      return result;
    }
  }

  result.success = true;
  result.path = recycleBinPath;
  return result;
}

RecycleBinResult RecycleBinController::emptyRecycleBin(const QString &p_notebookId) {
  RecycleBinResult result;

  QString recycleBinPath = getRecycleBinPath(p_notebookId);
  if (recycleBinPath.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("Recycle bin is not supported for this notebook type.");
    return result;
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    result.success = false;
    result.errorMessage = tr("NotebookService not available.");
    return result;
  }

  if (!notebookService->emptyRecycleBin(p_notebookId)) {
    result.success = false;
    result.errorMessage = tr("Failed to empty recycle bin.");
    return result;
  }

  result.success = true;
  return result;
}
