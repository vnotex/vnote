#include "firstruncontroller.h"

#include <QDir>
#include <QFileInfo>

#include <controllers/newnotebookcontroller.h>
#include <core/configmgr2.h>
#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <utils/pathutils.h>

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

QString FirstRunController::resolveParentDir() const {
  return m_parentDirOverride.isEmpty() ? ConfigMgr2::getDocumentOrHomePath() : m_parentDirOverride;
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

bool FirstRunController::createDefaultNotebook() {
  const QString parent = resolveParentDir();
  const QString root = PathUtils::concatenateFilePath(parent, QStringLiteral("my_notebook"));

  // Collision guard: skip when the target path is an existing file or a
  // non-empty directory. An empty existing dir (or a non-existent path) is
  // allowed to proceed; vxcore creates the root dir itself for Bundled.
  // NOTE: PathUtils::isEmptyDir() returns TRUE for a non-existent path, so it
  // is the wrong primitive here — use QFileInfo/QDir directly.
  QFileInfo fi(root);
  if (fi.exists()) {
    if (fi.isFile() || (fi.isDir() && !QDir(root).isEmpty())) {
      qInfo() << "FirstRunController: skip FRE, target not empty:" << root;
      return false; // no signal
    }
    // empty existing dir is allowed to proceed
  }

  NewNotebookInput input;
  input.name = tr("My Notebook");
  input.description = tr("My default notebook in VNote");
  input.rootFolderPath = root;
  input.type = NotebookType::Bundled;
  input.syncMethod = QStringLiteral("none");

  NewNotebookController controller(m_services);
  NewNotebookResult res = controller.createNotebook(input);

  if (res.success) {
    emit defaultNotebookCreated(res.notebookId);
    return true;
  }

  qWarning() << "FirstRunController: failed to create default notebook:" << res.errorMessage;
  return false;
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
