#include "firstruncontroller.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QResource>
#include <QScopeGuard>

#include <core/configmgr2.h>
#include <core/error.h>
#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/sessionconfig.h>
#include <utils/fileutils2.h>
#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

FirstRunController::FirstRunController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  auto *hookMgr = m_services.get<HookManager>();
  if (hookMgr) {
    m_hookId = hookMgr->addAction(
        HookNames::MainWindowAfterStart,
        [this](HookContext &, const QVariantMap &) { onMainWindowAfterStart(); },
        /*priority=*/5);
  }
}

FirstRunController::~FirstRunController() {
  if (m_hookId != -1) {
    // Guard null in case the HookManager was already torn down during shutdown.
    auto *hookMgr = m_services.get<HookManager>();
    if (hookMgr) {
      hookMgr->removeAction(m_hookId);
    }
  }
}

void FirstRunController::setParentDirOverrideForTesting(const QString &p_dir) {
  m_parentDirOverride = p_dir;
}

void FirstRunController::setSourceDirOverrideForTesting(const QString &p_dir) {
  m_sourceDirOverride = p_dir;
}

bool FirstRunController::shouldCreateDefaultNotebook(bool p_versionChanged) const {
  if (!p_versionChanged) {
    return false;
  }

  auto *nb = m_services.get<NotebookCoreService>();
  if (!nb) {
    return false;
  }

  return nb->listNotebooks().size() == 0;
}

void FirstRunController::clearReadOnlyRecursively(const QString &p_dirPath) {
  // Files copied out of a Qt .rcc inherit the resource's read-only
  // permissions. Add owner/user write permission to every file so vxcore can
  // rewrite notebook metadata on open and the user can edit the notes.
  QDirIterator it(p_dirPath, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString filePath = it.next();
    QFile file(filePath);
    const QFileDevice::Permissions perms = file.permissions();
    const QFileDevice::Permissions writable =
        perms | QFileDevice::WriteOwner | QFileDevice::WriteUser;
    if (writable != perms) {
      file.setPermissions(writable);
    }
  }
}

bool FirstRunController::createDefaultNotebook() {
  const QString root = ConfigMgr2::getDefaultNotebookPath(m_parentDirOverride);

  const QFileInfo fi(root);
  if (fi.exists() && !fi.isDir()) {
    qInfo() << "FirstRunController: skip FRE, target is not a directory:" << root;
    return false;
  }

  bool createdLocally = false;

  // Only materialize a new notebook into an absent or empty destination.
  // Existing content is opened below without copying or changing permissions.
  if (!fi.exists() || QDir(root).isEmpty()) {
    // Resolve the bundled-notebook source. In production it lives inside the
    // separately-packaged vnote_extra.rcc (mounted via the "app:" prefix); tests
    // point at a writable on-disk fixture and skip the rcc entirely.
    QString sourceDir = m_sourceDirOverride;
    const QString extraRcc(QStringLiteral("app:vnote_extra.rcc"));
    bool rccRegistered = false;
    auto rccCleanup = qScopeGuard([&extraRcc, &rccRegistered] {
      if (rccRegistered) {
        QResource::unregisterResource(extraRcc);
      }
    });
    if (sourceDir.isEmpty()) {
      if (!QResource::registerResource(extraRcc)) {
        qWarning() << "FirstRunController: failed to register resource" << extraRcc;
        return false;
      }
      rccRegistered = true;
      sourceDir = QStringLiteral(":/vnotex/data/extra/default-notebook");
    }

    if (!QFileInfo::exists(sourceDir)) {
      qWarning() << "FirstRunController: bundled notebook source missing:" << sourceDir;
      return false;
    }

    // Copy the bundled notebook tree into the target root.
    const Error copyErr = FileUtils2::copyDir(sourceDir, root);
    if (copyErr) {
      qWarning() << "FirstRunController: failed to copy bundled notebook:" << copyErr.what();
      return false;
    }
    createdLocally = true;

    // Files copied from the rcc are read-only; make them writable on disk.
    clearReadOnlyRecursively(root);
  }

  // Open the new or existing notebook (fires NotebookAfterOpen).
  auto *nb = m_services.get<NotebookCoreService>();
  if (!nb) {
    qWarning()
        << "FirstRunController: NotebookCoreService unavailable; cannot open default notebook";
    return false;
  }

  const QString notebookId = nb->openNotebook(root);
  if (notebookId.isEmpty()) {
    qWarning() << "FirstRunController: failed to open default notebook at" << root;
    return false;
  }

  if (createdLocally && !nb->setNotebookTrusted(notebookId, true)) {
    qWarning() << tr("Notebook was created, but its trust setting could not be saved");
  }

  initializeQuickAccess(notebookId);
  emit defaultNotebookCreated(notebookId);
  return true;
}

void FirstRunController::initializeQuickAccess(const QString &p_notebookId) {
  auto *cfg = m_services.get<ConfigMgr2>();
  if (!cfg || !cfg->getSessionConfig().getQuickAccessItems().isEmpty()) {
    return;
  }

  if (!cfg->isSessionConfigLoaded()) {
    // A missing session is safe to initialize; an unreadable one is not.
    auto *configService = m_services.get<ConfigCoreService>();
    if (!configService) {
      return;
    }
    bool readOk = false;
    configService->getConfigByName(DataLocation::Local, QStringLiteral("session"), &readOk);
    if (!readOk) {
      return;
    }
  }

  auto *nb = m_services.get<NotebookCoreService>();
  const QString noteName = QStringLiteral("all-in-one.md");
  VxCoreError error = VXCORE_OK;
  const auto fileInfo = nb->getFileInfo(p_notebookId, noteName, &error);
  if (error != VXCORE_OK) {
    return;
  }
  SessionConfig::QuickAccessItem item;
  item.m_path = nb->buildAbsolutePath(p_notebookId, noteName);
  item.m_uuid = fileInfo.value(QLatin1String(vxcore::kJsonKeyId)).toString();
  if (item.m_path.isEmpty() || item.m_uuid.isEmpty()) {
    return;
  }
  cfg->getSessionConfig().setQuickAccessItems({item});
}

void FirstRunController::onMainWindowAfterStart() {
  auto *cfg = m_services.get<ConfigMgr2>();
  if (!cfg) {
    return;
  }

  if (shouldCreateDefaultNotebook(cfg->isVersionChanged())) {
    createDefaultNotebook();
  }
}
