#include "notebooksyncinfocontroller.h"

#include <memory>
#include <utility>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QUuid>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/syncworkqueuemanager.h>

#include <sync/sync_json_keys.h>
#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {

const QString c_archiveJournal = QStringLiteral("webdav/retirement.json");
const QStringList c_gitEntries = {
    QStringLiteral("HEAD"),           QStringLiteral("config"),     QStringLiteral("description"),
    QStringLiteral("hooks"),          QStringLiteral("info"),       QStringLiteral("objects"),
    QStringLiteral("refs"),           QStringLiteral("logs"),       QStringLiteral("index"),
    QStringLiteral("packed-refs"),    QStringLiteral("FETCH_HEAD"), QStringLiteral("ORIG_HEAD"),
    QStringLiteral("COMMIT_EDITMSG"), QStringLiteral("shallow"),    QStringLiteral("rr-cache")};

bool pathPresent(const QString &p_path) {
  const QFileInfo info(p_path);
  return info.exists() || info.isSymLink();
}

bool safeDirectory(const QString &p_path) {
  const QFileInfo info(p_path);
  if (!info.exists()) {
    return !info.isSymLink();
  }
  return info.isDir() && !info.isSymLink() && info.isReadable() &&
         QDir::cleanPath(info.canonicalFilePath())
                 .compare(QDir::cleanPath(info.absoluteFilePath()),
#ifdef Q_OS_WIN
                          Qt::CaseInsensitive
#else
                          Qt::CaseSensitive
#endif
                          ) == 0;
}

bool readObject(const QString &p_path, QJsonObject &p_object) {
  QFile file(p_path);
  if (QFileInfo(p_path).isSymLink() || !file.open(QIODevice::ReadOnly) ||
      file.size() > 128 * 1024 * 1024) {
    return false;
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return false;
  }
  p_object = document.object();
  return true;
}

bool writeObject(const QString &p_path, const QJsonObject &p_object) {
  QSaveFile file(p_path);
  const auto bytes = QJsonDocument(p_object).toJson(QJsonDocument::Compact);
  return !QFileInfo(p_path).isSymLink() && file.open(QIODevice::WriteOnly) &&
         file.write(bytes) == bytes.size() && file.commit();
}

bool safeLeaf(const QString &p_name) {
  return !p_name.isEmpty() && p_name != QLatin1String(".") && p_name != QLatin1String("..") &&
         !p_name.contains(QLatin1Char('/')) && !p_name.contains(QLatin1Char('\\')) &&
         !p_name.contains(QLatin1Char(':')) && !p_name.contains(QChar(0)) &&
         p_name != QLatin1String("retired") && p_name != QLatin1String("retirement.json");
}

bool sameEndpoint(const SyncSettings &p_old, const SyncSettings &p_new) {
  if (p_old.m_backend != p_new.m_backend) {
    return false;
  }
  if (canonicalSyncRemoteUrl(p_old) == canonicalSyncRemoteUrl(p_new)) {
    return true;
  }
  if (p_old.m_backend != QLatin1String("git")) {
    return false;
  }
  const QUrl oldUrl(p_old.m_remoteUrl);
  const QUrl newUrl(p_new.m_remoteUrl);
  return oldUrl.isValid() && newUrl.isValid() && oldUrl.scheme() == QLatin1String("https") &&
         oldUrl.password().isEmpty() && newUrl.password().isEmpty() &&
         oldUrl.adjusted(QUrl::RemoveUserInfo) == newUrl.adjusted(QUrl::RemoveUserInfo);
}

// Journal contains only validated relative names. It is written before any move;
// on a failed/incomplete archive every moved entry is restored before any enable.
// Committed archives are retained indefinitely and are never mixed into a new binding.
bool recoverArchive(const QString &p_syncDir, const QString &p_notebookId) {
  QDir sync(p_syncDir);
  const auto journalPath = sync.filePath(c_archiveJournal);
  if (!pathPresent(journalPath)) {
    return true;
  }
  QJsonObject journal;
  if (!safeDirectory(sync.filePath(QStringLiteral("webdav"))) ||
      !readObject(journalPath, journal) || journal.value(QStringLiteral("version")).toInt() != 1 ||
      journal.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString() != p_notebookId ||
      !journal.value(QStringLiteral("entries")).isArray()) {
    return false;
  }
  const auto operationId = journal.value(QStringLiteral("operationId")).toString();
  if (QUuid(operationId).isNull() ||
      QUuid(operationId).toString(QUuid::WithoutBraces) != operationId) {
    return false;
  }
  const auto phase = journal.value(QStringLiteral("phase")).toString();
  if (phase != QLatin1String("prepared") && phase != QLatin1String("archived") &&
      phase != QLatin1String("disabled")) {
    return false;
  }
  const QString archive = sync.filePath(QStringLiteral("webdav/retired/") + operationId);
  if (!safeDirectory(sync.filePath(QStringLiteral("webdav/retired"))) || !safeDirectory(archive)) {
    return false;
  }
  const auto entries = journal.value(QStringLiteral("entries")).toArray();
  QSet<QString> seen;
  for (const auto &value : entries) {
    if (!value.isObject()) {
      return false;
    }
    const auto entry = value.toObject();
    const auto backend = entry.value(QLatin1String(vxcore::kJsonKeyBackend)).toString();
    const auto name = entry.value(QStringLiteral("name")).toString();
    if (!safeLeaf(name) || backend != QLatin1String("webdav") || seen.contains(name)) {
      return false;
    }
    seen.insert(name);
  }
  if (phase == QLatin1String("disabled")) {
    return QFile::remove(journalPath);
  }
  for (auto it = entries.constEnd(); it != entries.constBegin();) {
    const auto entry = (*--it).toObject();
    const auto name = entry.value(QStringLiteral("name")).toString();
    const auto source = sync.filePath(QStringLiteral("webdav/") + name);
    const auto target = QDir(archive).filePath(name);
    const bool hasSource = pathPresent(source);
    const bool hasTarget = pathPresent(target);
    if (hasSource == hasTarget || (hasTarget && !QDir().rename(target, source))) {
      return false;
    }
  }
  if (!QFile::remove(journalPath)) {
    return false;
  }
  QDir().rmdir(archive);
  return true;
}

// Discover bindings from private state even after Disable cleared portable routing.
// The core's read-only reconfiguration check owns conflict/recovery validation.
bool inspectBindings(const QString &p_syncDir, const QString &p_notebookId,
                     QList<SyncSettings> &p_bindings) {
  QDir sync(p_syncDir);
  if (!safeDirectory(p_syncDir) || !safeDirectory(sync.filePath(QStringLiteral("webdav")))) {
    return false;
  }
  if (!sync.exists()) {
    return true;
  }
  const QDir dav(sync.filePath(QStringLiteral("webdav")));
  if (pathPresent(dav.filePath(QStringLiteral("state.json")))) {
    QJsonObject state;
    if (!readObject(dav.filePath(QStringLiteral("state.json")), state) ||
        state.value(QStringLiteral("version")).toInt() != 1 ||
        state.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString() != p_notebookId ||
        !state.value(QStringLiteral("entries")).isObject() ||
        !state.value(QStringLiteral("conflicts")).isObject()) {
      return false;
    }
    SyncSettings binding;
    binding.m_backend = QStringLiteral("webdav");
    binding.m_remoteUrl = state.value(QLatin1String(vxcore::kJsonKeyRemoteUrl)).toString();
    if (!validateSyncSettings(binding, false).isEmpty()) {
      return false;
    }
    p_bindings.append(binding);
  } else if (dav.exists()) {
    const auto entries =
        dav.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const auto &entry : entries) {
      if (entry != QLatin1String("retired")) {
        return false;
      }
    }
  }
  const auto entries =
      sync.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
  bool hasGit = false;
  for (const auto &entry : entries) {
    if (entry == QLatin1String("webdav")) {
      continue;
    }
    if (entry.endsWith(QLatin1String(".lock")) || entry.startsWith(QLatin1String("rebase-")) ||
        entry == QLatin1String("MERGE_HEAD") || entry == QLatin1String("REBASE_HEAD") ||
        entry == QLatin1String("CHERRY_PICK_HEAD") || entry == QLatin1String("sequencer")) {
      continue;
    }
    if (!c_gitEntries.contains(entry)) {
      return false;
    }
    hasGit = true;
  }
  if (hasGit) {
    QFile config(sync.filePath(QStringLiteral("config")));
    if (!QFileInfo(sync.filePath(QStringLiteral("HEAD"))).isFile() || config.size() > 1024 * 1024 ||
        !config.open(QIODevice::ReadOnly)) {
      return false;
    }
    const auto text = QString::fromUtf8(config.readAll());
    const QRegularExpression origin(QStringLiteral("\\[remote\\s+\"origin\"\\]([^\\[]*)"));
    const auto section = origin.match(text);
    const QRegularExpression url(QStringLiteral("(?:^|\\n)\\s*url\\s*=\\s*([^\\r\\n]+)"));
    const auto match = url.match(section.captured(1));
    if (!section.hasMatch() || !match.hasMatch()) {
      return false;
    }
    SyncSettings binding;
    binding.m_remoteUrl = match.captured(1).trimmed();
    if (binding.m_remoteUrl.startsWith(QLatin1Char('"')) &&
        binding.m_remoteUrl.endsWith(QLatin1Char('"'))) {
      binding.m_remoteUrl = binding.m_remoteUrl.mid(1, binding.m_remoteUrl.size() - 2);
    }
    if (!validateSyncSettings(binding, false).isEmpty()) {
      return false;
    }
    p_bindings.append(binding);
  }
  return true;
}

bool archiveWebDavBinding(const QString &p_syncDir, const QString &p_notebookId,
                          const SyncSettings &p_settings) {
  QDir sync(p_syncDir);
  QList<SyncSettings> bindings;
  if (!inspectBindings(p_syncDir, p_notebookId, bindings)) {
    return false;
  }
  bool retire = false;
  for (const auto &binding : bindings) {
    retire = retire ||
             (binding.m_backend == QLatin1String("webdav") && !sameEndpoint(binding, p_settings));
  }
  if (!retire) {
    return true;
  }
  const auto operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto archive = sync.filePath(QStringLiteral("webdav/retired/") + operationId);
  if (!safeDirectory(sync.filePath(QStringLiteral("webdav/retired")))) {
    return false;
  }
  QJsonArray entries;
  const QDir source(sync.filePath(QStringLiteral("webdav")));
  for (const auto &info : source.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                               QDir::Hidden | QDir::System)) {
    const auto name = info.fileName();
    if (name == QLatin1String("retired")) {
      continue;
    }
    if (!safeLeaf(name) || info.isSymLink() ||
        (info.isDir() && !safeDirectory(info.absoluteFilePath()))) {
      return false;
    }
    entries.append(QJsonObject{{QLatin1String(vxcore::kJsonKeyBackend), QStringLiteral("webdav")},
                               {QStringLiteral("name"), name}});
  }
  QJsonObject journal{{QStringLiteral("version"), 1},
                      {QLatin1String(vxcore::kJsonKeyNotebookId), p_notebookId},
                      {QStringLiteral("operationId"), operationId},
                      {QStringLiteral("phase"), QStringLiteral("prepared")},
                      {QStringLiteral("entries"), entries}};
  const auto journalPath = sync.filePath(c_archiveJournal);
  if (!writeObject(journalPath, journal)) {
    return false;
  }
  bool ok = QDir().mkpath(archive);
  for (const auto &value : entries) {
    if (!ok) {
      break;
    }
    const auto name = value.toObject().value(QStringLiteral("name")).toString();
    ok = QDir().rename(source.filePath(name), QDir(archive).filePath(name));
  }
  if (ok) {
    journal[QStringLiteral("phase")] = QStringLiteral("archived");
    ok = writeObject(journalPath, journal);
  }
  if (!ok) {
    recoverArchive(p_syncDir, p_notebookId);
  }
  return ok;
}

// Called only after successful disable has released libgit2's mapped objects.
// Never remove the sync root recursively: WebDAV state and retained payloads
// belong to a separate backend and must survive every Git URL change.
bool removeGitBinding(const QString &p_syncDir) {
  const QDir sync(p_syncDir);
  const auto entries =
      sync.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
  for (const auto &entry : entries) {
    if (entry.fileName() == QLatin1String("webdav")) {
      continue;
    }
    if (!c_gitEntries.contains(entry.fileName()) || entry.isSymLink() ||
        (entry.isDir() && !safeDirectory(entry.absoluteFilePath()))) {
      return false;
    }
  }
  for (const auto &entry : entries) {
    if (entry.fileName() != QLatin1String("webdav") &&
        !(entry.isDir() ? QDir(entry.absoluteFilePath()).removeRecursively()
                        : QFile::remove(entry.absoluteFilePath()))) {
      return false;
    }
  }
  QDir().rmdir(p_syncDir); // Nonrecursive: preserves a remaining WebDAV subtree.
  return true;
}

bool commitArchive(const QString &p_syncDir) {
  const auto path = QDir(p_syncDir).filePath(c_archiveJournal);
  if (!pathPresent(path)) {
    return true;
  }
  QJsonObject journal;
  if (!readObject(path, journal)) {
    return false;
  }
  journal[QStringLiteral("phase")] = QStringLiteral("disabled");
  return writeObject(path, journal) && QFile::remove(path);
}

} // namespace

NotebookSyncInfoController::NotebookSyncInfoController(ServiceLocator &p_services,
                                                       const QString &p_notebookId,
                                                       QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_notebookId(p_notebookId) {}

QString NotebookSyncInfoController::notebookName() const {
  auto *service = m_services.get<NotebookCoreService>();
  return service ? service->getNotebookConfig(m_notebookId)
                       .value(QLatin1String(vxcore::kJsonKeyName))
                       .toString()
                 : QString();
}

QString NotebookSyncInfoController::remoteUrl() const {
  auto *service = m_services.get<NotebookCoreService>();
  return service ? service->getNotebookConfig(m_notebookId)
                       .value(QLatin1String(vxcore::kJsonKeySyncRemoteUrl))
                       .toString()
                 : QString();
}

QString NotebookSyncInfoController::backend() const {
  auto *service = m_services.get<NotebookCoreService>();
  return service ? service->getNotebookConfig(m_notebookId)
                       .value(QLatin1String(vxcore::kJsonKeySyncBackend))
                       .toString()
                 : QString();
}

bool NotebookSyncInfoController::syncEnabled() const {
  auto *service = m_services.get<NotebookCoreService>();
  return service && service->getNotebookConfig(m_notebookId)
                        .value(QLatin1String(vxcore::kJsonKeySyncEnabled))
                        .toBool();
}

bool NotebookSyncInfoController::isRawNotebook() const {
  auto *service = m_services.get<NotebookCoreService>();
  return !service || service->getNotebookConfig(m_notebookId)
                             .value(QLatin1String(vxcore::kJsonKeyType))
                             .toString() != QLatin1String("bundled");
}

QString NotebookSyncInfoController::lastSyncTime() const {
  auto *service = m_services.get<NotebookCoreService>();
  const auto millis = service ? service->getLastSyncUtc(m_notebookId) : 0;
  return millis > 0 ? QLocale::system().toString(QDateTime::fromMSecsSinceEpoch(millis),
                                                 QLocale::ShortFormat)
                    : QString();
}

void NotebookSyncInfoController::loadInitialData() {
  const auto method = backend();
  if (isRawNotebook()) {
    emit error(tr("Sync is not available for raw notebooks."));
  } else if (!method.isEmpty() && !isSupportedSyncBackend(method)) {
    emit error(tr("Unsupported sync backend."));
  }
  emit dataLoaded(notebookName(), remoteUrl(), lastSyncTime());
}

void NotebookSyncInfoController::finishFailure(const QString &p_message) {
  m_applying = false;
  m_pendingEndpointChange = {};
  emit error(p_message);
  emit applyComplete(false);
}

void NotebookSyncInfoController::applyChanges(const SyncSettings &p_settings) {
  startApply(p_settings, false);
}

void NotebookSyncInfoController::bootstrapApply(const SyncSettings &p_settings) {
  startApply(p_settings, true);
}

void NotebookSyncInfoController::startApply(const SyncSettings &p_settings, bool p_bootstrap) {
  auto *sync = m_services.get<SyncService>();
  auto *notebooks = m_services.get<NotebookCoreService>();
  if (!sync || !notebooks) {
    finishFailure(tr("Sync services not available."));
    return;
  }
  if (m_applying || sync->isSyncInProgress(m_notebookId)) {
    emit error(tr("Wait for queued or running synchronization to finish, then try again."));
    emit applyComplete(false);
    return;
  }
  if (isRawNotebook()) {
    finishFailure(tr("Sync is not available for raw notebooks."));
    return;
  }
  const auto configuredBackend = backend();
  if (!configuredBackend.isEmpty() && !isSupportedSyncBackend(configuredBackend)) {
    finishFailure(tr("Unsupported sync backend. Disable it before choosing another method."));
    return;
  }
  const auto validation = validateSyncSettings(p_settings, false);
  if (!validation.isEmpty()) {
    finishFailure(validation);
    return;
  }
  auto settings = p_settings;
  settings.m_remoteUrl = canonicalSyncRemoteUrl(settings);
  const auto syncDir =
      notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("vx_notebook/vx_sync"));
  auto lease = sync->workQueueManager()->tryAcquireMaintenance({m_notebookId});
  if (syncDir.isEmpty() || !lease || !safeDirectory(QFileInfo(syncDir).absolutePath()) ||
      !safeDirectory(syncDir) || !recoverArchive(syncDir, m_notebookId)) {
    finishFailure(tr("Cannot restore an incomplete sync-state archive. No settings were changed."));
    return;
  }
  QList<SyncSettings> bindings;
  if (!inspectBindings(syncDir, m_notebookId, bindings)) {
    finishFailure(tr("Sync recovery state is unreadable or invalid. Preserve it and repair it "
                     "before changing settings."));
    return;
  }
  SyncSettings current;
  current.m_backend = configuredBackend;
  current.m_remoteUrl = remoteUrl();
  if (isSupportedSyncBackend(current.m_backend) && !current.m_remoteUrl.isEmpty()) {
    bindings.append(current);
  }
  bool changedEndpoint = false;
  QString previousUrl;
  for (const auto &binding : bindings) {
    if (!sameEndpoint(binding, settings)) {
      changedEndpoint = true;
      previousUrl = binding.m_remoteUrl;
    }
  }
  if (changedEndpoint && notebooks->checkSyncReconfiguration(m_notebookId) != VXCORE_OK) {
    finishFailure(tr(
        "Resolve conflicts and complete pending sync recovery before changing the method or URL."));
    return;
  }
  m_applying = true;
  if (changedEndpoint) {
    m_pendingEndpointChange = [this, settings]() {
      withCredentials(settings,
                      [this](const SyncSettings &resolved) { retireAndEnable(resolved); });
    };
    lease.release();
    emit confirmUrlChangeRequested(previousUrl, settings.m_remoteUrl);
    return;
  }
  lease.release();
  // Partial/disabled enable requires supplied credentials. An active form may
  // leave fields empty to keep credentials, fetched only into this continuation.
  if (p_bootstrap && settings.m_credentials.m_secret.isEmpty()) {
    finishFailure(validateSyncSettings(settings, true));
    return;
  }
  withCredentials(settings, [this, current, p_bootstrap, sync](const SyncSettings &resolved) {
    if (sync->isSyncInProgress(m_notebookId)) {
      finishFailure(tr("Wait for synchronization to finish, then try again."));
      return;
    }
    if (!p_bootstrap && sync->isSyncRegistered(m_notebookId) &&
        current.m_backend == resolved.m_backend &&
        canonicalSyncRemoteUrl(current) == resolved.m_remoteUrl) {
      auto connection = std::make_shared<QMetaObject::Connection>();
      *connection =
          connect(sync, &SyncService::credentialsSetFinished, this,
                  [this, connection](const QString &id, VxCoreError result) {
                    if (id != m_notebookId) {
                      return;
                    }
                    disconnect(*connection);
                    m_applying = false;
                    if (result != VXCORE_OK) {
                      emit error(tr(
                          "Failed to update sync credentials. Existing sync state was preserved."));
                    }
                    emit applyComplete(result == VXCORE_OK);
                  });
      sync->updateCredentials(m_notebookId, resolved.m_credentials);
    } else {
      enable(resolved, false);
    }
  });
}

void NotebookSyncInfoController::withCredentials(SyncSettings p_settings,
                                                 std::function<void(const SyncSettings &)> p_next) {
  const auto &credentials = p_settings.m_credentials;
  const bool webdav = p_settings.m_backend == QLatin1String("webdav");
  if (webdav && !credentials.m_username.isEmpty() && credentials.m_secret.isEmpty()) {
    finishFailure(tr("Supply a new password or app password when entering a username."));
    return;
  }
  if (!credentials.m_secret.isEmpty() && (!webdav || !credentials.m_username.isEmpty())) {
    const auto message = validateSyncSettings(p_settings, true);
    if (!message.isEmpty()) {
      finishFailure(message);
      return;
    }
    p_next(p_settings);
    return;
  }
  auto *store = m_services.get<SyncCredentialsStore>();
  if (!store) {
    finishFailure(tr("Credentials store not available."));
    return;
  }
  auto received = std::make_shared<QMetaObject::Connection>();
  auto failed = std::make_shared<QMetaObject::Connection>();
  *received = connect(
      store, &SyncCredentialsStore::credentialsRetrieved, this,
      [this, received, failed, p_settings, p_next](const QString &id,
                                                   const SyncCredential &saved) mutable {
        if (id != m_notebookId) {
          return;
        }
        disconnect(*received);
        disconnect(*failed);
        if (saved.m_backend != p_settings.m_backend) {
          finishFailure(
              tr("Saved credentials do not match the selected backend. Supply new credentials."));
          return;
        }
        auto replacement = saved;
        if (!p_settings.m_credentials.m_secret.isEmpty()) {
          replacement.m_secret = p_settings.m_credentials.m_secret;
        }
        p_settings.m_credentials = replacement;
        const auto message = validateSyncSettings(p_settings, true);
        if (!message.isEmpty()) {
          finishFailure(message);
          return;
        }
        p_next(p_settings);
      });
  *failed = connect(store, &SyncCredentialsStore::credentialsError, this,
                    [this, received, failed](const QString &id, const QString &) {
                      if (id != m_notebookId) {
                        return;
                      }
                      disconnect(*received);
                      disconnect(*failed);
                      finishFailure(
                          tr("Cannot read saved credentials. Supply credentials and try again."));
                    });
  store->retrieveCredentials(m_notebookId);
}

void NotebookSyncInfoController::confirmUrlChange(bool p_confirmed) {
  if (!m_pendingEndpointChange) {
    return;
  }
  auto continuation = std::move(m_pendingEndpointChange);
  m_pendingEndpointChange = {};
  if (!p_confirmed) {
    m_applying = false;
    emit applyComplete(false);
    return;
  }
  continuation();
}

void NotebookSyncInfoController::enable(const SyncSettings &p_settings, bool p_reconfigured) {
  auto *sync = m_services.get<SyncService>();
  auto connection = std::make_shared<QMetaObject::Connection>();
  *connection = connect(
      sync, &SyncService::bootstrapAndPersistFinished, this,
      [this, connection, p_reconfigured](const QString &id, VxCoreError result,
                                         const QString &message) {
        if (id != m_notebookId) {
          return;
        }
        disconnect(*connection);
        m_applying = false;
        if (result != VXCORE_OK) {
          emit error(
              p_reconfigured
                  ? tr("Could not enable the new sync endpoint. The notebook is disabled; use "
                       "Enable Sync to retry. Previous sync state was retained.")
                  : (message.isEmpty() ? tr("Failed to enable sync for notebook.") : message));
        }
        emit applyComplete(result == VXCORE_OK);
      });
  sync->bootstrapAndPersist(m_notebookId, p_settings);
}

void NotebookSyncInfoController::retireAndEnable(const SyncSettings &p_settings) {
  auto *sync = m_services.get<SyncService>();
  auto *notebooks = m_services.get<NotebookCoreService>();
  if (sync->isSyncInProgress(m_notebookId)) {
    finishFailure(tr("Wait for queued or running synchronization to finish, then try again."));
    return;
  }
  auto lease = sync->workQueueManager()->tryAcquireMaintenance({m_notebookId});
  const auto syncDir =
      notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("vx_notebook/vx_sync"));
  if (syncDir.isEmpty() || !lease || !recoverArchive(syncDir, m_notebookId) ||
      notebooks->checkSyncReconfiguration(m_notebookId) != VXCORE_OK) {
    finishFailure(tr("Synchronization or unresolved conflicts prevent changing this endpoint."));
    return;
  }
  QList<SyncSettings> bindings;
  if (!inspectBindings(syncDir, m_notebookId, bindings) ||
      !archiveWebDavBinding(syncDir, m_notebookId, p_settings)) {
    finishFailure(tr("Could not archive the previous sync binding. No new endpoint was enabled; "
                     "preserve the recovery files and retry."));
    return;
  }
  bool retireGit = false;
  for (const auto &binding : bindings) {
    retireGit = retireGit ||
                (binding.m_backend == QLatin1String("git") && !sameEndpoint(binding, p_settings));
  }
  auto connection = std::make_shared<QMetaObject::Connection>();
  *connection = connect(
      sync, &SyncService::disableFinished, this,
      [this, connection, syncDir, p_settings, retireGit](const QString &id, VxCoreError result) {
        if (id != m_notebookId) {
          return;
        }
        disconnect(*connection);
        if (result != VXCORE_OK) {
          const bool restored = recoverArchive(syncDir, m_notebookId);
          finishFailure(restored ? tr("Could not disable sync. Previous sync state was restored.")
                                 : tr("Could not restore the sync archive. Preserve recovery files "
                                      "before retrying."));
          return;
        }
        if (!commitArchive(syncDir)) {
          finishFailure(tr("Sync is disabled, but its archive could not be finalized. Preserve "
                           "recovery files before retrying."));
          return;
        }
        if (retireGit && !removeGitBinding(syncDir)) {
          finishFailure(tr("Sync is disabled, but old Git sync data could not be removed. Working "
                           "files and WebDAV recovery data were preserved."));
          return;
        }
        enable(p_settings, true);
      },
      Qt::QueuedConnection);
  // Enqueue while paused, then release: no unrelated sync can observe moved state
  // before the old backend has been disabled.
  sync->disableSyncForNotebook(m_notebookId);
  lease.release();
}

void NotebookSyncInfoController::disableSync() {
  auto *sync = m_services.get<SyncService>();
  if (!sync || m_applying || sync->isSyncInProgress(m_notebookId)) {
    emit error(tr("Wait for synchronization to finish before disabling sync."));
    return;
  }
  auto connection = std::make_shared<QMetaObject::Connection>();
  *connection =
      connect(sync, &SyncService::disableFinished, this,
              [this, connection](const QString &id, VxCoreError result) {
                if (id != m_notebookId) {
                  return;
                }
                disconnect(*connection);
                if (result == VXCORE_OK) {
                  emit disableComplete(id);
                } else {
                  emit error(tr(
                      "Failed to disable sync. Existing settings and credentials were retained."));
                }
              });
  sync->disableSyncForNotebook(m_notebookId);
}
